#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"
#include "read_write.h"

static char *g_host;
static int g_port;

int attempt_reconnect;
int wowza_fd = -1;      /* TODO: we probably want something more flexible */
GIOChannel *wowza_channel = NULL;

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static gboolean
    handle_output(GIOChannel *source, GIOCondition cond, gpointer data);

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

    g_free(host_addr);

    if (sock == NULL)
    {
        g_warning("There was an error connecting to wowza on %s:%d - this service will be fairly useless", g_host, g_port);
        attempt_reconnect = 1;
        return;
    }

    g_message("Successfully connected to wowza on %s:%d", g_host, g_port);

    /* Connected to Wowza - set up a watch on the channel */
    GIOChannel *chan = gnet_tcp_socket_get_io_channel(sock);
    /* Has to be set to buffered when setting the encoding, even with NULL
     * encoding, because glib is stupid */
    g_io_channel_set_buffered(chan, TRUE);
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

        wowza_fd = -1;
        wowza_channel = NULL;
        exit(-1);
    }
    wowza_fd = fd;
    wowza_channel = chan;
}

/* Handle stream data input */
static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    int fd, n;

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
        else
        {
            /* The other side closed the connection */
            g_warning("Remote end closed connection");
        }
        /* TODO: clean up any user data */

        g_io_channel_shutdown(source, FALSE, NULL);
        g_io_channel_unref(source);
        unregister_fd(fd);
        wowza_fd = -1;
        wowza_channel = NULL;

        /* Try to reconnect every so often */
        attempt_reconnect = 1;

        /* Remove this GIOFunc */
        return FALSE;
    }

    while (n > 0)
    {
        /* Handler must handle freeing this message (or keeping it around, in
         * case we're reflecting it back to wowza untouched) */
        unsigned char *msg;
        int msg_length;
        n = get_next_message(fd, &msg, &msg_length);

#if 0
        queue_imo_message_for_worker(msg, msg_length);
#else
        handle_imo_message(msg, msg_length);
#endif
    }

    /* Return TRUE to keep this handler intact (don't unregister it) */
    return TRUE;
}

void send_imo_message(const unsigned char *msg, int msg_len)
{
    if (!msg || msg_len == 0)
    {
        g_warning("(%s:%d) msg is NULL or has zero length", __FILE__, __LINE__);
        return;
    }

    /* TODO: Who do we send data to? We probably only have one wowza connection,
     * but it would be good to make this more general. We should map stream
     * names to the wowza fd they came in on */
    if (wowza_fd == -1)
    {
        /* I guess wowza is down. */
        /* TODO: If wowza is smart enough to remember streams across restarts,
         * we should queue data for wowza here */
        g_warning("(%s:%d) wowza fd is -1", __FILE__, __LINE__);

        /* TODO: we should free msg here */
        return;
    }

    queue_message(wowza_fd, msg, msg_len);

    /* Add a watch on the channel so we write data once the channel is
     * writable */
    if (!g_io_add_watch(wowza_channel, G_IO_OUT, handle_output, NULL))
    {
        g_warning("(%s:%d) Cannot add watch on GIOChannel for write",
                __FILE__, __LINE__);
    }
}

static gboolean
handle_output(GIOChannel *source, GIOCondition cond, gpointer data)
{
    UNUSED(cond);
    UNUSED(data);

    /* Presumably we'll have at least one message queued at this point */
    int fd, n;

    fd = g_io_channel_unix_get_fd(source);

    do
    {
        n = write_data(fd);
    } while (n > 0);

    /* We're written everything we had - remove this watch */
    return FALSE;
}
