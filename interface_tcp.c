#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);

void setup_tcp_connection(char *host, int port)
{
    UNUSED(host);
    UNUSED(port);
}


/* Handle stream data input */
static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    /* TODO: check the condition */
    UNUSED(cond);
    UNUSED(data);

    gchar *buf;
    gsize num_bytes;

    /* TODO: For now we're assuming that all data is present right away. We need
     * to deal with partial messages */
    g_io_channel_read_to_end(source, &buf, &num_bytes, NULL);

    SAMPLE_BLOCK *sb = message_to_samples(buf, num_bytes);

    g_free(buf);
    sample_block_destroy(sb);

    return TRUE;
}
