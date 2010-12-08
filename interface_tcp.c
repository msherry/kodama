#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"

static GInetAddr *host_addr;

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);

/* NOTES: when our connection to wowza dies, we should just forget all
 * information we have, and attempt to reconnect */

void setup_tcp_connection(char *host, int port)
{
    GTcpSocket *sock;

    UNUSED(host);
    UNUSED(port);

    host_addr = gnet_inetaddr_new(host, port);

    /* This blocks until we connect */
    sock = gnet_tcp_socket_new(host_addr);

    if (sock == NULL)
    {
        g_error("There was an error connecting to %s:%d - aborting\n",
                host, port);
        exit(-1);
    }

    /* Connected to Wowza - set up a watch on the channel */
    GIOChannel *chan = gnet_tcp_socket_get_io_channel(sock);
    // Set NULL encoding so that NULL bytes are handled properly
    g_io_channel_set_encoding(chan, NULL, NULL);
    g_io_channel_set_buffered(chan, FALSE);
    g_io_channel_set_flags(chan, G_IO_FLAG_NONBLOCK, NULL);

    if (!g_io_add_watch(chan, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input, NULL))
    {
        g_warning("(%s:%d) Unable to add watch on channel", __FILE__, __LINE__);
        exit(-1);
    }
}


/* Handle stream data input */
static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    /* TODO: check the condition */
    UNUSED(cond);
    UNUSED(data);

    if (cond & G_IO_HUP || cond & G_IO_ERR) /* TODO: or we can't read any bytes */
    {
        if (cond & G_IO_HUP)
        {
            g_warning("(%s:%d) Socket hupped", __FILE__, __LINE__);
        }
        else if (cond & G_IO_ERR)
        {
            g_warning("(%s:%d) Error on socket", __FILE__, __LINE__);
        }
        /* TODO: else the other side closed the connection */
        /* TODO: clean up any user data */

        /* Remove this GIOFunc */
        return FALSE;
    }

    /* Return TRUE to keep this handler intact (don't unregister it) */
    return TRUE;
}
