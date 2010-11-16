#include <string.h>

#include "cbuffer.h"
#include "protocol.h"

/* Convert a buffer of bytes (gchars) from the network into a
 * SAMPLE_BLOCK. Caller must free SAMPLE_BLOCK */
SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes)
{
    /* TODO: real error checking, some sort of protocol, endianness */

    /* TODO: this obviously assumes we have an integral number of samples */
    size_t count = num_bytes / sizeof(SAMPLE);
    SAMPLE_BLOCK *sb = sample_block_create(count);
    memcpy(sb->s, buf, num_bytes);

    /* DEBUG_LOG("(%s:%d) Received %ld samples\n", __FILE__, __LINE__, count); */

    return sb;
}

/* Convert a SAMPLE_BLOCK into a buffer of bytes (gchars) for transmission on
 * the network. Caller must free buffer */
gchar *samples_to_message(SAMPLE_BLOCK *sb, gint *num_bytes)
{
    /* We have data to xmit - let's throw it into a buffer and send it out */
    *num_bytes = sb->count * sizeof(SAMPLE);
    gchar *buf = g_malloc(*num_bytes);

    /* TODO: this is just a straight copy - a real protocol would be better */
    memcpy(buf, sb->s, *num_bytes);

    return buf;
}

