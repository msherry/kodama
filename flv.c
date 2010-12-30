#include <glib.h>

#include "flv.h"
#include "util.h"

static void parse_flv_body(const unsigned char *buf, int len);

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
    parse_flv_body(packet_data+offset, packet_len - 4 - offset);

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
}
