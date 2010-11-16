#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "protocol.h"

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

