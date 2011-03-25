#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imo_message.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"
#include "read_write.h"

static char *g_host;
static int g_port;
static char *g_port_str;

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
    g_port_str = g_strdup_printf("%d", g_port);

    tcp_connect();
}

/* Uses the global g_host and g_port */
void tcp_connect(void)
{
    int sock_fd;
    int error;
    struct addrinfo hints, *result;

    /* Stop trying to reconnect */
    /* TODO: having to set this to 1 on on every failure is lame */
    attempt_reconnect = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* Does a blocking DNS lookup */
    error = getaddrinfo(g_host, g_port_str, &hints, &result);
    if (error)
    {
        g_warning("There was an error looking up the address for %s:%d - %s",
                g_host, g_port, gai_strerror(error));
        attempt_reconnect = 1;
        return;
    }

    /* These values are all in result, but ehh */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        g_warning("There was an error creating a socket: %s",
                strerror(errno));
        attempt_reconnect = 1;
        return;
    }

    int flag = 1;
    error = setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY,
            &flag, sizeof(int));

    error = connect(sock_fd, result->ai_addr, result->ai_addrlen);
    if (error)
    {
        g_warning("There was an error connecting to wowza on %s:%d (%s)"
                " - this service will be fairly useless",
                g_host, g_port, strerror(errno));
        attempt_reconnect = 1;
        return;
    }

    freeaddrinfo(result);

    g_message("Successfully connected to wowza on %s:%d", g_host, g_port);

    /* Connected to Wowza - set up a watch on the channel */
    GIOChannel *chan = g_io_channel_unix_new(sock_fd);
    // Set NULL encoding so that NULL bytes are handled properly
    g_io_channel_set_encoding(chan, NULL, NULL);
    g_io_channel_set_buffered(chan, FALSE);
    g_io_channel_set_flags(chan, G_IO_FLAG_NONBLOCK, NULL);

    register_fd(sock_fd);

    if (!g_io_add_watch(chan, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input, NULL))
    {
        g_warning("(%s:%d) Unable to add watch on channel", __FILE__, __LINE__);
        g_io_channel_shutdown(chan, FALSE, NULL);
        g_io_channel_unref(chan);
        unregister_fd(sock_fd);

        wowza_fd = -1;
        wowza_channel = NULL;
        exit(-1);
    }
    wowza_fd = sock_fd;
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
        imo_message *msg;
        n = get_next_message(fd, &msg);

        if (msg && msg->text && msg->length)
        {
#if THREADED
            queue_imo_message_for_worker(msg);
#else
            handle_imo_message(msg);
#endif
        }
        else
        {
            g_warning("Pulled an empty message from the incoming queue");
        }
    }

    /* Return TRUE to keep this handler intact (don't unregister it) */
    return TRUE;
}

void send_imo_message(imo_message *msg)
{
    if (!msg || !msg->text || !msg->length)
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

    queue_message(wowza_fd, msg);

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
