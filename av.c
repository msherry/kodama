#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "av.h"
#include "flv.h"
#include "kodama.h"

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size);
static int get_sample_rate(const unsigned char formatbyte);
static void local_flv_set_audio_codec(struct AVCodecContext *acodec, int flv_codecid);

void init_av(void)
{
    av_register_all();
    flv_init();
}

int setup_decode_context(FLVStream *flv, unsigned char formatByte)
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

int setup_encode_context(FLVStream *flv)
{
    /* This should match the decode context as closely as possible - use the
     * format byte that we cached */

    /* TODO: what do we report for the speex sample rate? The correct one, or
     * the lie that FLV tells us */

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

    if (flv_codecid == FLV_CODECID_SPEEX && 0) /* TODO: */
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
        flv->e_codec_ctx->bit_rate = 20600;
        /* flv->e_codec_ctx->compression_level = 5; */
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

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size)
{
    g_debug("Format byte: %#.2x", formatByte);

    /* Find the codec */
    *codecid = formatByte & FLV_AUDIO_CODECID_MASK;
    g_debug("codec id: %d", *codecid);
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
    g_debug("(Reported) sample rate: %d", *sampleRate);

    if (*sampleRate == -1)
    {
        return -1;
    }

    /* Number of audio channels */
    *channels = (formatByte & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
    g_debug("Number of channels: %d", *channels);
    if (*channels != 1)
    {
        g_warning("********* Channels != 1 ************");
        return -1;
    }

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


