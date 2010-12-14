#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"
#include "read_write.h"

static GInetAddr *host_addr;

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static void handle_message(char *msg, int message_length);

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
        g_warning("There was an error connecting to %s:%d - this service will be fairly useless\n", host, port);
        return;
    }

    DEBUG_LOG("Successfully connected to wowza on %s:%d\n", host, port);

    /* Connected to Wowza - set up a watch on the channel */
    GIOChannel *chan = gnet_tcp_socket_get_io_channel(sock);
    // Set NULL encoding so that NULL bytes are handled properly
    g_io_channel_set_encoding(chan, NULL, NULL);
    g_io_channel_set_buffered(chan, FALSE);
    g_io_channel_set_flags(chan, G_IO_FLAG_NONBLOCK, NULL);

    int fd = g_io_channel_unix_get_fd(chan);

    register_fd(fd);

    if (!g_io_add_watch(chan, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input, NULL))
    {
        g_warning("(%s:%d) Unable to add watch on channel", __FILE__, __LINE__);
        g_io_channel_shutdown(chan, FALSE, NULL);
        g_io_channel_unref(chan);
        unregister_fd(fd);
        exit(-1);
    }
}


/* Handle stream data input */
static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    int fd, n;

    UNUSED(cond);
    UNUSED(data);

    fd = g_io_channel_unix_get_fd(source);

    if (cond & G_IO_HUP || cond & G_IO_ERR || ((n = read_data(fd)) == -9))
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
        /* TODO: attempt to reconnect periodically */

        /* Remove this GIOFunc */
        return FALSE;
    }

    while (n > 0)
    {
        char *msg;
        int msg_length;
        n = get_next_message(fd, &msg, &msg_length);
        handle_message(msg, msg_length);
        free(msg);
    }

    /* Return TRUE to keep this handler intact (don't unregister it) */
    return TRUE;
}

static void handle_message(char *msg, int msg_length)
{
    /* Header format:
       Message length (including header)      - 4 bytes
       Type                                   - 1 byte
       Stream name length                     - 1 byte
       Stream name                            - variable length
    */

    /* TODO: we're going to need a conversation id of sorts, since both streams
     * of a conversation need to go to the same hybrid. The hybrid can then be
     * named with this conv_id */
}
