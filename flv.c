#include <glib.h>

#include "flv.h"
#include "util.h"

static void parse_flv_body(const unsigned char *buf, int len);
static int get_sample_rate(const unsigned char formatbyte);

void flv_parse_header(void)
{
    /* TODO: */
}

/* Parses an FLV tag (not the stream header) */
void flv_parse_tag(const unsigned char *packet_data, const int packet_len)
{
    /* For details of this format, see:
       http://osflash.org/flv
    */

    unsigned char type_code, type;
    int offset = 0;

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
        parse_flv_body(packet_data+offset, packet_len - 4 - offset);
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
}

static void parse_flv_body(const unsigned char *buf, int len)
{
    /* First byte should be the format byte */
    unsigned char formatByte = buf[0];

    g_debug("Format byte: %#.2x", formatByte);

    /* Find the codec */
    int codecid = formatByte & FLV_AUDIO_CODECID_MASK;
    char *codec_name;
    switch(codecid)
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
        break;
    }
    g_debug("Format byte & FLV_AUDIO_CODECID_MASK (codec id): %#.2x", codecid);
    g_debug("Codec: %s", codec_name);

    /* Audio sample rate */
    int sampleRate = get_sample_rate(formatByte);
    g_debug("Sample rate: %d", sampleRate);

    /* Number of audio channels */
    int channels = (formatByte & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
    g_debug("Number of channels: %d", channels);

    /* Bits per coded sample */
    int sampleSize = (formatByte & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
    g_debug("Bits per sample: %d", sampleSize);
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
