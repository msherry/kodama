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

static char *g_host;
static int g_port;

int attempt_reconnect;

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static void handle_message(char *msg, int message_length);

/* NOTES: when our connection to wowza dies, we should just forget all
 * information we have, and attempt to reconnect */

void setup_tcp_connection(char *host, int port)
{
    /* Don't try to reconnect before we've connected */
    attempt_reconnect = 0;

    /* Set up tables for read/write handlers */
    init_read_write();

    g_host = g_strdup_printf("%s", host);
    g_port = port;

    tcp_connect();
}

/* Uses the global g_host and g_port */
void tcp_connect(void)
{
    GTcpSocket *sock;
    GInetAddr *host_addr;

    /* Stop trying to reconnect */
    attempt_reconnect = 0;

    host_addr = gnet_inetaddr_new(g_host, g_port);

    /* This blocks until we connect or fail to connect */
    sock = gnet_tcp_socket_new(host_addr);

    if (sock == NULL)
    {
        g_warning("There was an error connecting to %s:%d - this service will be fairly useless", g_host, g_port);
        attempt_reconnect = 1;
        return;
    }

    g_debug("Successfully connected to wowza on %s:%d", g_host, g_port);

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

    g_debug("in handle_input");
    fd = g_io_channel_unix_get_fd(source);

    g_debug("fd = %d, cond = %d", fd, cond);

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
        else
        {
            /* The other side closed the connection */
            g_debug("Remote end closed connection");
        }
        /* TODO: clean up any user data */
        /* TODO: attempt to reconnect periodically */

        g_io_channel_shutdown(source, FALSE, NULL);
        g_io_channel_unref(source);
        unregister_fd(fd);

        /* Try to reconnect every so often */
        attempt_reconnect = 1;

        /* Remove this GIOFunc */
        return FALSE;
    }

    g_debug("finished read_data - n = %d", n);

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

    g_debug("Got a packet");
}
