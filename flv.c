// http://cekirdek.uludag.org.tr/~ismail/ffmpeg-docs/libavcodec_2utils_8c.html

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <glib.h>

#include "av.h"
#include "cbuffer.h"
#include "flv.h"
#include "kodama.h"
#include "util.h"

/* TODO: shortcircuit tx samples to rx, send to hardware for testing */

extern globals_t globals;       /* Needed for FLV_LOG */

static FLVStream *create_flv_stream(void);
static void destroy_flv_stream(FLVStream *flv);

static GHashTable *id_to_flvstream;

void flv_init(void)
{
    id_to_flvstream = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, (GDestroyNotify)destroy_flv_stream);
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
    flv->e_codec_ctx->sample_fmt = SAMPLE_FMT_S16;
    flv->e_resample_ctx = NULL;

    return flv;
}

static void destroy_flv_stream(FLVStream *flv)
{
    g_return_if_fail(flv != NULL);

    av_free(flv->d_codec_ctx);
    av_free(flv->d_resample_ctx); /* Ok if this is NULL */

    av_free(flv->e_codec_ctx);
    av_free(flv->e_resample_ctx);

    free(flv);
}

void flv_parse_header(void)
{
    /* TODO: */
}

void flv_start_stream(const char *stream_name)
{
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    if (flv)
    {
        g_warning("FLVStream already exists for stream %s - "
            "this is suspicious", stream_name);
        return;
    }
    FLV_LOG("Creating FLVStream for %s\n", stream_name);
    flv = create_flv_stream();
    g_hash_table_insert(id_to_flvstream, g_strdup(stream_name), flv);
}

void flv_end_stream(const char *stream_name)
{
    FLV_LOG("Destroying FLVStream for %s\n", stream_name);
    g_hash_table_remove(id_to_flvstream, stream_name);
}

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
        /* We'll require that FLVStreams are only created in response to 'S'
         * messages for now */
        g_warning("(%s:%d) No FLVStream found for stream %s - aborting",
            __FILE__, __LINE__, stream_name);
        return -1;
    }

    unsigned char type_code, type;
    int offset = 0;
    int ret=-1;

    FLV_LOG("Packet length: %d\n", packet_len);

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
        FLV_LOG("Unknown packet type: %c\n", type_code);
        return -1;
        break;
    }
    FLV_LOG("Type: %c\n", type);

    /* 3 bytes, big-endian */
    unsigned int bodyLength;
    bodyLength = read_uint24_be(packet_data+offset);
    offset += 3;
    FLV_LOG("BodyLength: %d\n", bodyLength);

    /* Timestamp - 4 bytes, crazy order */
    unsigned int timestamp;
    timestamp = read_uint24_be(packet_data+offset);
    offset += 3;
    timestamp |= (packet_data[offset++] << 24);
    FLV_LOG("Timestamp: %u  (%#.8x)\n", timestamp, timestamp);

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
            FLV_LOG("Setting up decode context\n");
            ret = setup_decode_context(flv, formatByte);
            if (ret)
            {
                FLV_LOG("Error setting up decode context: %d\n", ret);
                return ret;
            }

            FLV_LOG("Setting up encode context\n");
            ret = setup_encode_context(flv);
            if (ret != 0)
            {
                FLV_LOG("Error setting up encode context: %d\n", ret);
                return ret;
            }
        }

        /* TODO: all this crap is probably unnecessary */
        uint8_t *aligned;
        posix_memalign((void **)&aligned, 16,
            bodyLength+FF_INPUT_BUFFER_PADDING_SIZE);
        memset(aligned, 0, bodyLength+FF_INPUT_BUFFER_PADDING_SIZE);
        memcpy(aligned, packet_data+offset+flv->d_flags_size,
            bodyLength-flv->d_flags_size);

        /* The codec context should be set at this point. Convert data */
        AVPacket avpkt;
        av_init_packet(&avpkt);
        avpkt.data = aligned; //packet_data+offset+flv->d_flags_size;
        avpkt.size = bodyLength-flv->d_flags_size;

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

        FLV_LOG("Bytes decoded: %d\n", bytesDecoded);

        free(aligned);

        if (bytesDecoded > 0)
        {
            int numSamples;
            /* TODO: this doesn't need to be this large */
            SAMPLE resampled[AVCODEC_MAX_AUDIO_FRAME_SIZE];
            SAMPLE *sample_buf = sample_array;

            numSamples = frame_size / sizeof(SAMPLE);
            FLV_LOG("Samples decoded: %d\n", numSamples);

            if (flv->d_resample_ctx)
            {
                /* Need to resample */
                FLV_LOG("Resampling from %d to %d Hz\n",
                    flv->d_codec_ctx->sample_rate, SAMPLE_RATE);

                int newrate_num_samples;

                newrate_num_samples = audio_resample(flv->d_resample_ctx,
                    resampled, sample_array, numSamples);

                numSamples = newrate_num_samples;
                sample_buf = resampled;
            }

            char *hex;

            hex = hexify(packet_data, packet_len);
            FLV_LOG("Incoming FLV packet: %s\n", hex);
            free(hex);

            /* hex = samples_to_text(sample_buf, numSamples); */
            /* FLV_LOG("Decoded incoming samples: %s\n", hex); */
            /* free(hex); */

            *sb = sample_block_create(numSamples);
            memcpy((*sb)->s, sample_buf, numSamples*sizeof(SAMPLE));
            (*sb)->pts = timestamp; /* We're just going to pass this back to the
                                     * encoder */

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
    FLV_LOG("PrevTagSize: %u  (%#.8x)\n", prev_tag_size, prev_tag_size);

    return ret;
}

int flv_create_tag(unsigned char **flv_packet, int *packet_len,
    const char *stream_name, SAMPLE_BLOCK *sb)
{
    /* This should always only create FLV tags of type 'A' */

    char *hex;

    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    if (!flv)
    {
        g_warning("FLVStream not found for stream %s for encoding - aborting",
            stream_name);
        return -1;
    }

    SAMPLE *sample_buf = sb->s;
    int numSamples = sb->count;

    /* hex = samples_to_text(sample_buf, numSamples); */
    /* FLV_LOG("Decoded outgoing samples: %s\n", hex); */
    /* free(hex); */

    /* First we (maybe) need to resample from SAMPLE_RATE to the sample rate the
     * client was originally transmitting */
    SAMPLE resampled[AVCODEC_MAX_AUDIO_FRAME_SIZE];
    if (flv->e_resample_ctx)
    {
        FLV_LOG("Resampling from %d to %d Hz\n",
                SAMPLE_RATE, flv->d_codec_ctx->sample_rate);

        int newrate_num_samples = audio_resample(flv->e_resample_ctx,
                resampled, sb->s, sb->count);

        FLV_LOG("Resampled from %d to %d samples\n", (int)sb->count,
            newrate_num_samples);

        sample_buf = resampled;
        numSamples = newrate_num_samples;
    }

    uint8_t encoded_audio[FF_MIN_BUFFER_SIZE];
    int bytesEncoded = avcodec_encode_audio(flv->e_codec_ctx, encoded_audio,
                FF_MIN_BUFFER_SIZE, sample_buf);

    FLV_LOG("Bytes encoded: %d\n", bytesEncoded);

    int bodyLength = bytesEncoded + 1; /* +1 for format byte */

    /* We have our audio data (Body part of an FLV tag). Time to create the
     * tag. */
    *packet_len = 1 + 3 + 3 + 1 + 3 + bodyLength + 4;
    *flv_packet = malloc(*packet_len);

    FLV_LOG("Return flv packet len: %d\n", *packet_len);

    int offset = 0;

    *(*flv_packet + offset++) = 0x08; /* Audio packet */

    write_uint24_be((*flv_packet + offset), bodyLength);
    offset += 3;

    int timestamp = sb->pts;
    FLV_LOG("Outgoing timestamp: %d\n", timestamp);
    write_uint24_be((*flv_packet + offset), timestamp);
    offset += 3;
    *(*flv_packet + offset++) = ((timestamp >> 24) & 0xff);

    /* Stream id - don't care*/
    offset += 3;

    /* Format byte */
    *(*flv_packet + offset++) = flv->d_format_byte;

    /* Body */
    memcpy((*flv_packet + offset), encoded_audio, bytesEncoded);
    offset += bytesEncoded;

    /* Previous tag size */
    write_uint32_be((*flv_packet + offset), *packet_len - 4);
    offset += 4;

    hex = hexify(*flv_packet, *packet_len);
    FLV_LOG("Outgoing FLV packet: %s\n", hex);
    free(hex);

    return 0;
}
