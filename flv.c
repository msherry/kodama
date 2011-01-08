// http://cekirdek.uludag.org.tr/~ismail/ffmpeg-docs/libavcodec_2utils_8c.html

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <glib.h>

#include "cbuffer.h"
#include "flv.h"
#include "kodama.h"
#include "util.h"

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size);
static int get_sample_rate(const unsigned char formatbyte);
static FLVStream *create_flv_stream(void);
static void local_flv_set_audio_codec(AVCodecContext *acodec, int flv_codecid);

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

    /* libavcodec context */
    flv->d_codec_ctx = avcodec_alloc_context();
    flv->d_codec_ctx->codec_type = CODEC_TYPE_AUDIO;
    flv->d_codec_ctx->codec_id = CODEC_ID_NONE;

    flv->d_resample_ctx = NULL;

    return flv;
}

void flv_parse_header(void)
{
    /* TODO: */
}

/* Parses an FLV tag (not the stream header) */
/* Caller must free samples */
/* TODO: create a SAMPLE_BLOCK, not an array of SAMPLEs. We can save a memcpy */
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
        g_debug("FLVStream not found for stream %s", stream_name);
        flv = create_flv_stream();
        g_hash_table_insert(id_to_flvstream, g_strdup(stream_name), flv);
    }

    unsigned char type_code, type;
    int offset = 0;
    int ret;

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
            int codecid, sampleRate, channels, sampleSize, flags_size;
            ret = decode_format_byte(formatByte, &codecid, &sampleRate,
                    &channels, &sampleSize, &flags_size);
            if (ret)
            {
                /* Something went wrong - we shouldn't attempt to process this
                 * tag. (Someone could could be maliciously sending us a bad
                 * codec id, for example). Send the data back to wowza untouched
                 * and let someone else deal with it */
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
                g_warning("Failed to load codec id %d",
                        flv->d_codec_ctx->codec_id);
                return -1;
            }
            if (avcodec_open(flv->d_codec_ctx, codec) < 0)
            {
                g_warning("Failed to open codec id %d",
                        flv->d_codec_ctx->codec_id);
                return -1;
            }

            /* Determine if we need to resample */
            if (sampleRate != SAMPLE_RATE)
            {
                flv->d_resample_ctx = av_audio_resample_init(1, channels,
                        SAMPLE_RATE, sampleRate, SAMPLE_FMT_S16, SAMPLE_FMT_S16,
                    16, //TODO: How many taps do we need?
                    10, 0, .8); /* TODO: fix these */
            }
            else
            {
                /* TODO: free old one, if it existed */
                flv->d_resample_ctx = NULL;
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
            numSamples = frame_size / sizeof(SAMPLE);
            g_debug("Samples decoded: %d", numSamples);

            if (flv->d_resample_ctx)
            {
                /* Need to resample */
                g_debug("Resampling from %d to %d Hz",
                    flv->d_codec_ctx->sample_rate, SAMPLE_RATE);

                /* TODO: this doesn't need to be this large */
                SAMPLE resampled[AVCODEC_MAX_AUDIO_FRAME_SIZE];
                int newrate_num_samples;

                newrate_num_samples = audio_resample(flv->d_resample_ctx,
                    resampled, sample_array, numSamples);

                numSamples = newrate_num_samples;
                *sb = sample_block_create(numSamples);
                memcpy((*sb)->s, resampled, numSamples*sizeof(SAMPLE));

            }
            else
            {
                *sb = sample_block_create(numSamples);
                memcpy((*sb)->s, sample_array, numSamples*sizeof(SAMPLE));
            }
            ret = 0;
        }
    }
    else if (type == 'V')
    {
        g_warning("Got video frame - I don't know how to handle those yet");
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

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size)
{
    g_debug("Format byte: %#.2x", formatByte);

    /* Find the codec */
    *codecid = formatByte & FLV_AUDIO_CODECID_MASK;
    char *codec_name;
    switch(*codecid)
    {
    case FLV_CODECID_PCM:
        codec_name = "FLV_CODECID_PCM";
        break;
    case FLV_CODECID_ADPCM:
        codec_name = "FLV_CODECID_ADPCM";
        break;
    case FLV_CODECID_MP3:
        codec_name = "FLV_CODECID_MP3";
        break;
    case FLV_CODECID_PCM_LE:
        codec_name = "FLV_CODECID_PCM_LE";
        break;
    case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
        codec_name = "FLV_CODECID_NELLYMOSER_8KHZ_MONO";
        break;
    case FLV_CODECID_NELLYMOSER:
        codec_name = "FLV_CODECID_NELLYMOSER";
        break;
    case FLV_CODECID_AAC:
        codec_name = "FLV_CODECID_AAC";
        break;
    case FLV_CODECID_SPEEX:
        codec_name = "FLV_CODECID_SPEEX";
        break;
    default:
        codec_name = "UNKNOWN";
        *codecid = -1;
        break;
    }
    g_debug("Codec: %s", codec_name);

    if (*codecid == -1)
    {
        return -1;
    }

    /* Audio sample rate */
    *sampleRate = get_sample_rate(formatByte);
    g_debug("Sample rate: %d", *sampleRate);

    if (*sampleRate == -1)
    {
        return -1;
    }

    /* Number of audio channels */
    *channels = (formatByte & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
    g_debug("Number of channels: %d", *channels);

    /* Bits per coded sample */
    *sampleSize = (formatByte & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
    g_debug("Bits per sample: %d", *sampleSize);

    if (*codecid == FLV_CODECID_VP6 || *codecid == FLV_CODECID_VP6A ||
            *codecid == FLV_CODECID_AAC)
    {
        *flags_size = 2;
    }
    else if (*codecid == FLV_CODECID_H264)
    {
        *flags_size = 5;
    }
    else
    {
        *flags_size = 1;
    }

    return 0;
}

static int get_sample_rate(const unsigned char formatByte)
{
    int samplerate_code = formatByte & FLV_AUDIO_SAMPLERATE_MASK;
    int codecid;                /* Some special cases need this */
    int rate;
    switch (samplerate_code)
    {
    case FLV_SAMPLERATE_SPECIAL:
        codecid = formatByte & FLV_AUDIO_CODECID_MASK;
        if (codecid == FLV_CODECID_SPEEX)
        {
            rate = 16000;
        }
        else if (codecid == FLV_CODECID_NELLYMOSER_8KHZ_MONO)
        {
            rate = 8000;
        }
        else
        {
            rate = 5512;
        }
        break;
    case FLV_SAMPLERATE_11025HZ:
        rate = 11025;
        break;
    case FLV_SAMPLERATE_22050HZ:
        rate = 22050;
        break;
    case FLV_SAMPLERATE_44100HZ:
        rate = 44100;
        break;
    default:
        rate = -1;
        break;
    }
    return rate;
}


static void local_flv_set_audio_codec(AVCodecContext *acodec, int flv_codecid)
{
  switch(flv_codecid) {
    //no distinction between S16 and S8 PCM codec flags
    case FLV_CODECID_PCM:
      acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_U8 :
#ifdef HAVE_BIGENDIAN
      CODEC_ID_PCM_S16BE;
#else
      CODEC_ID_PCM_S16LE;
#endif
      g_debug("local_flv_set_audio_codec: FLV_CODECID_PCM");
      break;
    case FLV_CODECID_PCM_LE:
      acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_U8 : CODEC_ID_PCM_S16LE;
      g_debug("local_flv_set_audio_codec: FLV_CODECID_PCM_LE");
      break;
    case FLV_CODECID_AAC:
      acodec->codec_id = CODEC_ID_AAC;
      g_debug("local_flv_set_audio_codec: FLV_CODECID_AAC");
      break;
    case FLV_CODECID_ADPCM:
      acodec->codec_id = CODEC_ID_ADPCM_SWF;
      g_debug("local_flv_set_audio_codec: FLV_CODECID_ADPCM");
      break;
    case FLV_CODECID_MP3:
      acodec->codec_id = CODEC_ID_MP3;
      g_debug("local_flv_set_audio_codec: FLV_CODECID_MP3");
      break;
    case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
      acodec->sample_rate = 8000; //in case metadata does not otherwise declare samplerate
    case FLV_CODECID_NELLYMOSER:
      acodec->codec_id = CODEC_ID_NELLYMOSER;
      g_debug("local_flv_set_audio_codec: FLV_CODECID_NELLYMOSER");
      break;
    case FLV_CODECID_SPEEX:
      acodec->codec_id = CODEC_ID_SPEEX;
      /* Should be safe to do this for now - adobe always seems to use it */
      acodec->sample_rate = 16000; /* flvdec.c */
      g_debug("local_flv_set_audio_codec: FLV_CODECID_SPEEX");
      break;
    default:
      acodec->codec_tag = flv_codecid >> FLV_AUDIO_CODECID_OFFSET;
      g_debug("local_flv_set_audio_codec: default");
  }
}

int flv_create_tag(unsigned char **flv_packet, int *packet_len,
    char *stream_name, SAMPLE_BLOCK *sb)
{

}
