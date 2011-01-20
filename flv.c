// http://cekirdek.uludag.org.tr/~ismail/ffmpeg-docs/libavcodec_2utils_8c.html

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <glib.h>

#include "av.h"
#include "cbuffer.h"
#include "flv.h"
#include "kodama.h"
#include "util.h"

static int setup_decode_context(FLVStream *flv, unsigned char formatByte);
static int setup_encode_context(FLVStream *flv);
static FLVStream *create_flv_stream(void);

/* TODO: this is all temporary - just need to get code in place. Clean it up
 * later */
static GHashTable *id_to_flvstream;

void flv_init(void)
{
    id_to_flvstream = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL/*FLVStream_free_func*/);
}

static FLVStream *create_flv_stream(void)
{
    FLVStream *flv = malloc(sizeof(FLVStream));

    flv->d_format_byte = '\0';

    /* TODO: would this be better in the separate setup_encode/decode_context
     * functions */
    flv->d_codec_ctx = avcodec_alloc_context2(CODEC_TYPE_AUDIO);
    flv->d_codec_ctx->codec_id = CODEC_ID_NONE;
    flv->d_resample_ctx = NULL;

    flv->e_codec_ctx = avcodec_alloc_context2(CODEC_TYPE_AUDIO);
    flv->e_codec_ctx->codec_id = CODEC_ID_NONE;
    flv->e_resample_ctx = NULL;

    return flv;
}

void flv_parse_header(void)
{
    /* TODO: */
}

/* Parses an FLV tag (not the stream header) */
/* Caller must free samples */
int flv_parse_tag(const unsigned char *packet_data, const int packet_len,
    const char *stream_name, SAMPLE_BLOCK **sb)
{
    /* For details of this format, see:
       http://osflash.org/flv
    */

    /* TODO: ok, this whole function is an ugly hack. Rewrite all this crap once
     * we have something working */
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    if (!flv)
    {
        g_debug("FLVStream not found for stream %s - creating it", stream_name);
        flv = create_flv_stream();
        g_hash_table_insert(id_to_flvstream, g_strdup(stream_name), flv);
    }

    unsigned char type_code, type;
    int offset = 0;
    int ret=-1;

    g_debug("Packet length: %d", packet_len);

    type_code = packet_data[offset++];
    switch(type_code)
    {
    case 0x08:
        /* Audio */
        type = 'A';
        break;
    case 0x09:
        /* Video */
        type = 'V';
        break;
    case 0x12:
        /* Meta */
        type = 'M';
        break;
    default:
        /* Unknown */
        type = 'U';
        g_debug("Unknown packet type: %c", type_code);
        return -1;
        break;
    }
    g_debug("Type: %c", type);

    /* 3 bytes, big-endian */
    unsigned int bodyLength;
    bodyLength = read_uint24_be(packet_data+offset);
    offset += 3;
    g_debug("BodyLength: %d", bodyLength);

    /* Timestamp - 4 bytes, crazy order */
    unsigned int timestamp;
    timestamp = read_uint24_be(packet_data+offset);
    offset += 3;
    timestamp |= (packet_data[offset++] << 24);
    g_debug("Timestamp: %u  (%#.8x)", timestamp, timestamp);

    /* stream id is 3 bytes, and always zero - skip it */
    offset += 3;

    /* The rest is packet data, except the last 4 bytes, which should contain
     * the size of this packet */

    /* Figure out what we can from the body */
    if (type == 'A')
    {
        unsigned char formatByte = *(packet_data+offset);

        if (!flv->d_format_byte || flv->d_format_byte != formatByte)
        {
            g_debug("Setting up decode context");
            ret = setup_decode_context(flv, formatByte);
            if (ret != 0)
            {
                g_debug("Error setting up decode context: %d", ret);
                return ret;
            }

            g_debug("Setting up encode context");
            ret = setup_encode_context(flv);
            if (ret != 0)
            {
                g_debug("Error setting up encode context: %d", ret);
                return ret;
            }
        }

        /* The codec context should be set at this point. Convert data */

        AVPacket avpkt;
        av_init_packet(&avpkt);
        avpkt.data = packet_data+offset+flv->d_flags_size;
        avpkt.size = bodyLength-flv->d_flags_size;
        avpkt.dts = timestamp;
        avpkt.stream_index = 1;

        SAMPLE sample_array[AVCODEC_MAX_AUDIO_FRAME_SIZE];
        int frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;

        /*
          Warning:
          You must set frame_size_ptr to the allocated size of the output buffer
          before calling avcodec_decode_audio3().

          The input buffer must be FF_INPUT_BUFFER_PADDING_SIZE larger than the
          actual read bytes because some optimized bitstream readers read 32 or
          64 bits at once and could read over the end.

          The end of the input buffer avpkt->data should be set to 0 to ensure
          that no overreading happens for damaged MPEG streams.

          Note:

          You might have to align the input buffer avpkt->data and output buffer
          samples. The alignment requirements depend on the CPU: On some CPUs it
          isn't necessary at all, on others it won't work at all if not aligned
          and on others it will work but it will have an impact on performance.
        */

        int bytesDecoded = avcodec_decode_audio3(flv->d_codec_ctx, sample_array,
                &frame_size, &avpkt);

        g_debug("Bytes decoded: %d", bytesDecoded);

        if (bytesDecoded > 0)
        {
            int numSamples;
            /* TODO: this doesn't need to be this large */
            SAMPLE resampled[AVCODEC_MAX_AUDIO_FRAME_SIZE];
            SAMPLE *sample_buf = sample_array;

            numSamples = frame_size / sizeof(SAMPLE);
            g_debug("Samples decoded: %d", numSamples);

            if (flv->d_resample_ctx)
            {
                /* Need to resample */
                g_debug("Resampling from %d to %d Hz",
                    flv->d_codec_ctx->sample_rate, SAMPLE_RATE);

                int newrate_num_samples;

                newrate_num_samples = audio_resample(flv->d_resample_ctx,
                    resampled, sample_array, numSamples);

                numSamples = newrate_num_samples;
                sample_buf = resampled;
            }

            *sb = sample_block_create(numSamples);
            memcpy((*sb)->s, sample_buf, numSamples*sizeof(SAMPLE));

            ret = 0;
        }
    }
    else if (type == 'V')
    {
        g_warning("Got video frame - I don't know how to handle those yet");
        ret = -1;
    }

    offset = (packet_len - 4);
    /* A full 4-byte integer, big-endian. Read it the hard way since ints are
     * probably 8 bytes for us */
    unsigned int prev_tag_size;
    prev_tag_size = read_uint32_be(packet_data + offset);
    offset += 4;
    g_debug("PrevTagSize: %u  (%#.8x)", prev_tag_size, prev_tag_size);

    return ret;
}

static int setup_decode_context(FLVStream *flv, unsigned char formatByte)
{
    int ret;
    int codecid, sampleRate, channels, sampleSize, flags_size;
    ret = decode_format_byte(formatByte, &codecid, &sampleRate,
        &channels, &sampleSize, &flags_size);
    if (ret)
    {
        /* Something went wrong - we shouldn't attempt to process this
         * tag. (Someone could could be maliciously sending us a bad
         * codec id, for example). Send the data back to wowza untouched
         * and let someone else deal with it */
        g_warning("Error decoding format byte %.02x for decoding", formatByte);
        return ret;
    }

    if (flv->d_format_byte)
    {
        g_debug("Format byte changed. Old: %#.2x   New: %#.2x",
            flv->d_format_byte, formatByte);
    }

    flv->d_format_byte = formatByte;
    flv->d_flags_size = flags_size;

    /* Finish initting codec context */
    // Speex seems to always use 16000, but report 11025
    flv->d_codec_ctx->sample_rate = sampleRate;
    flv->d_codec_ctx->bits_per_coded_sample = sampleSize;
    local_flv_set_audio_codec(flv->d_codec_ctx, codecid);
    flv->d_codec_ctx->channels = channels;

    /* Load the codec */
    AVCodec *codec;

    /* TODO: if our format byte has changed, we have an old codec (and
     * possible resample context) lying around. Free everything that we
     * reallocate */
    g_debug("Loading codec id %d", flv->d_codec_ctx->codec_id);
    codec = avcodec_find_decoder(flv->d_codec_ctx->codec_id);
    if (!codec)
    {
        g_warning("Failed to load codec id %d for decoding",
            flv->d_codec_ctx->codec_id);
        return -1;
    }
    if (avcodec_open(flv->d_codec_ctx, codec) < 0)
    {
        g_warning("Failed to open codec id %d for decoding",
            flv->d_codec_ctx->codec_id);
        return -1;
    }

    /* Determine if we need to resample. Base it off the codec context's
     * sample rate, since the format byte often lies */
    if (flv->d_codec_ctx->sample_rate != SAMPLE_RATE)
    {
        g_debug("Creating decode resample context: %d -> %d",
            flv->d_codec_ctx->sample_rate, SAMPLE_RATE);
        flv->d_resample_ctx = av_audio_resample_init(1, channels,
            SAMPLE_RATE, flv->d_codec_ctx->sample_rate,
            SAMPLE_FMT_S16, SAMPLE_FMT_S16,
            16, //TODO: How many taps do we need?
            10, 0, .8); /* TODO: fix these */
    }
    else
    {
        /* TODO: free old one, if it existed */
        flv->d_resample_ctx = NULL;
    }
    return 0;
}

static int setup_encode_context(FLVStream *flv)
{
    /* This should match the decode context as closely as possible - use the
     * format byte that we cached */

    /* TODO: what do we report for the speex sample rate? The correct one, or the
     * lie that FLV tells us */

    /* TODO: We probably need to prepend the format byte to all audio data */

    int flv_codecid, sampleRate, channels, sampleSize, flags_size;
    int ret;
    ret = decode_format_byte(flv->d_format_byte, &flv_codecid, &sampleRate,
        &channels, &sampleSize, &flags_size);
    if (ret)
    {
        g_warning("Error decoding format byte %.02x for encoding",
                flv->d_format_byte);
        return ret;
    }

    flv->e_codec_ctx->sample_rate = sampleRate;
    flv->e_codec_ctx->codec_id = flv_codecid;
    local_flv_set_audio_codec(flv->e_codec_ctx, flv_codecid);
    flv->e_codec_ctx->channels = channels;

    if (flv_codecid == FLV_CODECID_SPEEX)
    {
        g_debug("Setting QSCALE flag");
        flv->e_codec_ctx->flags |= CODEC_FLAG_QSCALE;
        /* TODO: This is defaulted to 6/10, CBR in actionscript. This should
         * probably be roughly equivalent, which might mean not using VBR at
         * all */
        flv->e_codec_ctx->global_quality = 120; /* TODO: find the right value */
    }
    else
    {
        /* TODO: set e_codec_ctx->bit_rate - try to match incoming */
        g_debug("Not setting QSCALE flag: flv_codecid = %d", flv_codecid);
    }


    /* Load the codec */
    AVCodec *codec;

    /* TODO: if our format byte has changed, we have an old codec (and
     * possible resample context) lying around. Free everything that we
     * reallocate */
    g_debug("Loading codec id %d", flv->e_codec_ctx->codec_id);
    codec = avcodec_find_encoder(flv->e_codec_ctx->codec_id);
    if (!codec)
    {
        g_warning("Failed to load codec id %d for encoding",
            flv->e_codec_ctx->codec_id);
        return -1;
    }
    if (avcodec_open(flv->e_codec_ctx, codec) < 0)
    {
        g_warning("Failed to open codec id %d for encoding",
            flv->e_codec_ctx->codec_id);
        return -1;
    }

    /* TODO: resampling back to the original sample rate, if we downsampled for
     * echo cancellation */
    if (flv->e_codec_ctx->sample_rate != SAMPLE_RATE)
    {
        g_debug("Creating encode resample context: %d -> %d",
                SAMPLE_RATE, flv->d_codec_ctx->sample_rate);
        flv->e_resample_ctx = av_audio_resample_init(1, channels,
            flv->e_codec_ctx->sample_rate, SAMPLE_RATE,
            SAMPLE_FMT_S16, SAMPLE_FMT_S16,
            16,  // TODO: again, how many taps do we need?
            10, 0, .8);
    }
    else
    {
        flv->e_resample_ctx = NULL;
    }

    return 0;
}

/* Caller must free flv_packet */
int flv_create_tag(unsigned char **flv_packet, int *packet_len,
    char *stream_name, SAMPLE_BLOCK *sb)
{
    /* This should always only create FLV tags of type 'A' */

    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    if (!flv)
    {
        g_warning("FLVStream not found for stream %s for encoding - aborting",
            stream_name);
        return -1;
    }

    /* First we (maybe) need to resample from SAMPLE_RATE to the sample rate the
     * client was originally transmitting */
    SAMPLE resampled[AVCODEC_MAX_AUDIO_FRAME_SIZE];
    SAMPLE *sample_buf;
    int numSamples = sb->count;

    if (flv->e_resample_ctx)
    {
        g_debug("Resampling from %d to %d Hz",
                SAMPLE_RATE, flv->d_codec_ctx->sample_rate);

        int newrate_num_samples = audio_resample(flv->e_resample_ctx,
                resampled, sb->s, sb->count);

        g_debug("Resampled from %d to %d samples", (int)sb->count,
            newrate_num_samples);

        sample_buf = resampled;
        numSamples = newrate_num_samples;
    }
    else
    {
        sample_buf = sb->s;
    }

    uint8_t encoded_audio[AVCODEC_MAX_AUDIO_FRAME_SIZE];
    int bytesEncoded = avcodec_encode_audio(flv->e_codec_ctx, encoded_audio,
                AVCODEC_MAX_AUDIO_FRAME_SIZE, sample_buf);

    g_debug("Bytes encoded: %d", bytesEncoded);

    /* We have our audio data (Body part of an FLV tag). Time to create the
     * tag. TODO: extra +1 for format byte - is this correct? */
    *packet_len = 1 + 3 + 3 + 1 + 3 + 1 + bytesEncoded + 4;
    *flv_packet = malloc(*packet_len);

    int offset = 0;

    *(*flv_packet + offset++) = 0x08; /* Audio packet */
    

    return 0;
}
