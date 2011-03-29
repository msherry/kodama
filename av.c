#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "av.h"
#include "flv.h"
#include "kodama.h"

extern globals_t globals;       /* for FLV_LOG */

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size);
static int get_sample_rate(const unsigned char formatbyte);
static void local_flv_set_audio_codec(struct AVCodecContext *acodec,
    int flv_codecid);
static int local_get_audio_flags(AVCodecContext *enc);

void init_av(void)
{
    av_register_all();
    flv_init();
}

/* TODO: we should be able to handle > 1 speex frame per packet. It looks like
 * for decode this is handled automatically, but we may have to do something for
 * encoding */

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
        g_warning("Format byte changed. Old: %#.2x   New: %#.2x",
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
    FLV_LOG("Loading codec id %d\n", flv->d_codec_ctx->codec_id);
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
    if (flv->d_codec_ctx->sample_rate != globals.sample_rate)
    {
        FLV_LOG("Creating decode resample context: %d -> %d\n",
            flv->d_codec_ctx->sample_rate, globals.sample_rate);
        flv->d_resample_ctx = av_audio_resample_init(1, channels,
            globals.sample_rate, flv->d_codec_ctx->sample_rate,
            SAMPLE_FMT_S16, SAMPLE_FMT_S16,
            16, //TODO: How many taps do we need?
            10, 0, .8); /* TODO: fix these */
    }
    else
    {
        /* TODO: free old one, if it existed */
        flv->d_resample_ctx = NULL;
    }
    if (flv->d_codec_ctx->sample_fmt != SAMPLE_FMT_S16)
    {
        /* TODO: don't abort here, but we have to signal that this codec context
         * isn't valid */
        g_error("WTF? sample_fmt was %d", flv->d_codec_ctx->sample_fmt);
        return -1;
    }
    else
    {
        /* g_debug("Copacetic"); */
    }

    return 0;
}

int setup_encode_context(FLVStream *flv)
{
    /* This should match the decode context as closely as possible - use the
     * format byte that we cached */

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
        FLV_LOG("Setting QSCALE flag\n");
        flv->e_codec_ctx->flags |= CODEC_FLAG_QSCALE;
        /* TODO: This is defaulted to 6/10, CBR in actionscript. This should
         * probably be roughly equivalent, which might mean not using VBR at
         * all */
        flv->e_codec_ctx->global_quality = 120; /* TODO: find the right value */
    }
    else
    {
        FLV_LOG("Not setting QSCALE flag: flv_codecid = %d\n", flv_codecid);
        flv->e_codec_ctx->bit_rate = 20600; /* The default in actionscript */
        flv->e_codec_ctx->compression_level = 4; /* Higher = better quality */
    }

    /* Load the codec */
    AVCodec *codec;

    /* TODO: if our format byte has changed, we have an old codec (and
     * possible resample context) lying around. Free everything that we
     * reallocate */
    FLV_LOG("Loading codec id %d\n", flv->e_codec_ctx->codec_id);
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

    /* Resample back to the original sample rate, if we downsampled for echo
     * cancellation */
    if (flv->e_codec_ctx->sample_rate != globals.sample_rate)
    {
        FLV_LOG("Creating encode resample context: %d -> %d\n",
                globals.sample_rate, flv->d_codec_ctx->sample_rate);
        flv->e_resample_ctx = av_audio_resample_init(1, channels,
            flv->e_codec_ctx->sample_rate, globals.sample_rate,
            SAMPLE_FMT_S16, SAMPLE_FMT_S16,
            16,  // TODO: again, how many taps do we need?
            10, 0, .8);
    }
    else
    {
        flv->e_resample_ctx = NULL;
    }

    /* This should match the incoming format byte */
    int flags = local_get_audio_flags(flv->e_codec_ctx);

    if (flags != flv->d_format_byte)
    {
        g_warning("Calculated encode format byte != decode format byte");
        g_warning("%.2X\t%.2X", flags, flv->d_format_byte);
        return -1;
    }
    else
    {
        /* g_message("Everything is cool with the format byte"); */
    }

    return 0;
}

static int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size)
{
    FLV_LOG("Format byte: %#.2x\n", formatByte);

    /* Find the codec */
    *codecid = formatByte & FLV_AUDIO_CODECID_MASK;
    FLV_LOG("codec id: %d\n", *codecid);
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
    FLV_LOG("Codec: %s\n", codec_name);

    if (*codecid == -1)
    {
        return -1;
    }

    /* Audio sample rate */
    *sampleRate = get_sample_rate(formatByte);
    FLV_LOG("(Reported) sample rate: %d\n", *sampleRate);

    if (*sampleRate == -1)
    {
        return -1;
    }

    /* Number of audio channels */
    *channels = (formatByte & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
    FLV_LOG("Number of channels: %d\n", *channels);
    if (*channels != 1)
    {
        g_warning("********* Channels != 1 ************");
        return -1;
    }

    /* Bits per coded sample */
    *sampleSize = (formatByte & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
    FLV_LOG("Bits per sample: %d\n", *sampleSize);

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
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_PCM\n");
      break;
    case FLV_CODECID_PCM_LE:
      acodec->codec_id = acodec->bits_per_coded_sample == 8 ? CODEC_ID_PCM_U8 : CODEC_ID_PCM_S16LE;
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_PCM_LE\n");
      break;
    case FLV_CODECID_AAC:
      acodec->codec_id = CODEC_ID_AAC;
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_AAC\n");
      break;
    case FLV_CODECID_ADPCM:
      acodec->codec_id = CODEC_ID_ADPCM_SWF;
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_ADPCM\n");
      break;
    case FLV_CODECID_MP3:
      acodec->codec_id = CODEC_ID_MP3;
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_MP3\n");
      break;
    case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
      acodec->sample_rate = 8000; //in case metadata does not otherwise declare samplerate
    case FLV_CODECID_NELLYMOSER:
      acodec->codec_id = CODEC_ID_NELLYMOSER;
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_NELLYMOSER\n");
      break;
    case FLV_CODECID_SPEEX:
      acodec->codec_id = CODEC_ID_SPEEX;
      /* Should be safe to do this for now - adobe always seems to use it */
      acodec->sample_rate = 16000; /* flvdec.c */
      FLV_LOG("local_flv_set_audio_codec: FLV_CODECID_SPEEX\n");
      break;
    default:
      acodec->codec_tag = flv_codecid >> FLV_AUDIO_CODECID_OFFSET;
      FLV_LOG("local_flv_set_audio_codec: default\n");
  }
}

static int local_get_audio_flags(AVCodecContext *enc)
{
    int flags = (enc->bits_per_coded_sample == 16) ? FLV_SAMPLESSIZE_16BIT : FLV_SAMPLESSIZE_8BIT;

    if (enc->codec_id == CODEC_ID_AAC) // specs force these parameters
        return FLV_CODECID_AAC | FLV_SAMPLERATE_44100HZ | FLV_SAMPLESSIZE_16BIT | FLV_STEREO;
    else if (enc->codec_id == CODEC_ID_SPEEX) {
        if (enc->sample_rate != 16000) {
            g_warning("flv only supports wideband (16kHz) Speex audio\n");
            return -1;
        }
        if (enc->channels != 1) {
            g_warning("flv only supports mono Speex audio\n");
            return -1;
        }
        if (enc->frame_size / 320 > 8) {
            g_warning("Warning: Speex stream has more than "
                "8 frames per packet. Adobe Flash "
                "Player cannot handle this!\n");
        }
        return FLV_CODECID_SPEEX | FLV_SAMPLERATE_11025HZ | FLV_SAMPLESSIZE_16BIT;
    } else {
    switch (enc->sample_rate) {
        case    44100:
            flags |= FLV_SAMPLERATE_44100HZ;
            break;
        case    22050:
            flags |= FLV_SAMPLERATE_22050HZ;
            break;
        case    11025:
            flags |= FLV_SAMPLERATE_11025HZ;
            break;
        case     8000: //nellymoser only
        case     5512: //not mp3
            if(enc->codec_id != CODEC_ID_MP3){
                flags |= FLV_SAMPLERATE_SPECIAL;
                break;
            }
        default:
            g_warning("flv does not support that sample rate, choose from (44100, 22050, 11025).\n");
            return -1;
    }
    }

    if (enc->channels > 1) {
        flags |= FLV_STEREO;
    }

    switch(enc->codec_id){
    case CODEC_ID_MP3:
        flags |= FLV_CODECID_MP3    | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_PCM_U8:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_8BIT;
        break;
    case CODEC_ID_PCM_S16BE:
        flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_PCM_S16LE:
        flags |= FLV_CODECID_PCM_LE | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_ADPCM_SWF:
        flags |= FLV_CODECID_ADPCM | FLV_SAMPLESSIZE_16BIT;
        break;
    case CODEC_ID_NELLYMOSER:
        if (enc->sample_rate == 8000) {
            flags |= FLV_CODECID_NELLYMOSER_8KHZ_MONO | FLV_SAMPLESSIZE_16BIT;
        } else {
            flags |= FLV_CODECID_NELLYMOSER | FLV_SAMPLESSIZE_16BIT;
        }
        break;
    case 0:
        flags |= enc->codec_tag<<4;
        break;
    default:
        g_warning("codec not compatible with flv\n");
        return -1;
    }

    return flags;
}
