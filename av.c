#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "av.h"

static void prep_codecs_and_formats(void);
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
    prep_codecs_and_formats();
    flv_init();
}

void prep_codecs_and_formats(void)
{
    flv_ifmt = find_input_format("flv");
    flv_ofmt = find_output_format("flv");

    codec_nellymoser_decode = find_codec(CODEC_ID_NELLYMOSER, DECODE_STREAM);
    codec_mp3_encode = find_codec(CODEC_ID_MP3, ENCODE_STREAM);
    codec_flv1_decode = find_codec(CODEC_ID_FLV1, DECODE_STREAM);
    codec_flv1_encode = find_codec(CODEC_ID_FLV1, ENCODE_STREAM);
    codec_pcm_s16le_decode = find_codec(CODEC_ID_PCM_S16LE, DECODE_STREAM);
    codec_pcm_s16le_encode = find_codec(CODEC_ID_PCM_S16LE, ENCODE_STREAM);

    flv_ofmt->audio_codec = CODEC_ID_MP3;
    flv_ofmt->video_codec = CODEC_ID_FLV1;
}

AVInputFormat *find_input_format(char *name)
{
    AVInputFormat *ifmt = NULL, *ret = NULL;
    while ((ifmt = av_iformat_next(ifmt)) != NULL)
    {
        if (!strcmp(ifmt->name, name))
        {
            ret = ifmt;
            break;
        }
    }
    return ret;
}

AVOutputFormat *find_output_format(char *name)
{
    AVOutputFormat *ofmt = NULL, *ret = NULL;
    while ((ofmt = av_oformat_next(ofmt)) != NULL)
    {
        if (!strcmp(ofmt->name, name))
        {
            ret = ofmt;
            break;
        }
    }
    return ret;
}

AVCodec *find_codec(enum CodecID id, char isdecode)
{
    /* Courtesy of Patrick */

    AVCodec *codec = NULL;
    while ((codec = av_codec_next(codec)) != NULL)
    {
        if (codec->id == id)
        {
            if ((codec->decode && isdecode==DECODE_STREAM) ||
                    (codec->encode && isdecode==ENCODE_STREAM))
            {
                return codec;
            }
        }
    }
    g_warning("(%s:%d) Codec %d not found", __FILE__, __LINE__, id);
    abort();
    return NULL;
}
