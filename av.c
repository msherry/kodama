#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "av.h"
#include "flv.h"

AVCodec *find_codec(enum CodecID id, char isdecode);
AVInputFormat *find_input_format(char *name);
AVOutputFormat *find_output_format(char *name);

AVInputFormat *flv_ifmt;
AVOutputFormat *flv_ofmt;

struct AVCodec *codec_nellymoser_decode;
struct AVCodec *codec_mp3_encode;
struct AVCodec *codec_flv1_decode;
struct AVCodec *codec_flv1_encode;
struct AVCodec *codec_pcm_s16le_decode;
struct AVCodec *codec_pcm_s16le_encode;

void init_av(void)
{
    av_register_all();
    flv_init();
}

int decode_format_byte(const unsigned char formatByte, int *codecid,
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

int get_sample_rate(const unsigned char formatByte)
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

void local_flv_set_audio_codec(AVCodecContext *acodec, int flv_codecid)
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


