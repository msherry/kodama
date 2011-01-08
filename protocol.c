#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "flv.h"
#include "imo_message.h"
#include "protocol.h"
#include "util.h"

/* PROTOCOL 1 - UDP */

/* Convert a buffer of bytes (gchars) from the network into a SAMPLE_BLOCK,
 * according to the proto in the message. Caller must free SAMPLE_BLOCK */
SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes)
{
    protocol proto = ((proto_header *)buf)->proto;
    size_t data_bytes = num_bytes - sizeof(proto_header);
    gchar *data_start = buf + sizeof(proto_header);
    size_t count = 0;

    SAMPLE_BLOCK *sb;
    switch (proto)
    {
    case raw:
        count = data_bytes / sizeof(SAMPLE);
        sb = sample_block_create(count);
        memcpy(sb->s, data_start, data_bytes);
        break;
    default:
        DEBUG_LOG("(%s:%d) unknown proto %d\n", __FILE__, __LINE__, proto);
        break;
    }

    /* DEBUG_LOG("(%s:%d) Received %ld samples\n", __FILE__, __LINE__, count); */

    return sb;
}

/* Convert a SAMPLE_BLOCK into a buffer of bytes (gchars) for transmission on
 * the network, according to the given proto. Caller must free buffer */
gchar *samples_to_message(SAMPLE_BLOCK *sb, gint *num_bytes, protocol proto)
{
    /* We have data to xmit - let's throw it into a buffer and send it out */
    gchar *buf;
    size_t data_bytes;

    switch (proto)
    {
    case raw:
        /* TODO: still doesn't consider endianness */
        data_bytes = sb->count * sizeof(SAMPLE);
        *num_bytes = data_bytes + sizeof(proto_header);
        buf = g_malloc(*num_bytes);
        ((proto_header *)buf)->proto = proto;
        memcpy(buf+sizeof(proto_header), sb->s, data_bytes);
        break;
    default:
        DEBUG_LOG("(%s:%d) unknown proto %d\n", __FILE__, __LINE__, proto);
        return NULL;
    }

    return buf;
}


/* PROTOCOL 2 - TCP (WOWZA) */
/* Caller must free stream_name as well as the returned SAMPLE_BLOCK */
SAMPLE_BLOCK *imo_message_to_samples(const unsigned char *msg, int msg_length,
        char **stream_name)
{
    char type;
    unsigned char *packet_data;
    int data_len;

    char *hex;

    /* TODO: if there are any problems decoding/encoding, just reflect the
     * original message back(?) */

    decode_imo_message(msg, msg_length, &type, stream_name, &packet_data,
            &data_len);

    g_debug("Size: %d", msg_length);
    g_debug("Type: %c", type);
    /* Stream name is convName:[01] */
    g_debug("Stream name: %s", *stream_name);
    /* hex = hexify(msg, msg_length); */
    /* g_debug("Hex: %s", hex); */
    /* free(hex); */

    SAMPLE_BLOCK *sb = NULL;
    if (data_len > 0)
    {
        hex = hexify(packet_data, data_len);
        g_debug("FLV tag data: %s", hex);
        free(hex);
        int ret = flv_parse_tag(packet_data, data_len, *stream_name, &sb);

    }

    g_debug("\n\n");

    free(packet_data);
    return sb;
}

char *samples_to_imo_message(SAMPLE_BLOCK *sb, int *msg_length, char *stream_name)
{
    unsigned char *flv_packet;
    int flv_packet_len;

    int ret = flv_create_tag(&flv_packet, &flv_packet_len, stream_name, sb);
}
