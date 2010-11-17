#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "kodama.h"
#include "protocol.h"

static GTcpSocket *create_listening_socket(int port);
static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static void new_connection_func(GTcpSocket *server, GTcpSocket *client,
    gpointer data);

void setup_tcp_connect(void)
{

}

/* This will only be used in standalone mode, and another instance will connect
 * to us.  */
void setup_tcp_listen(int port)
{
    GTcpSocket *sock;
    if ((sock = create_listening_socket(port)) == NULL)
    {
        g_warning("(%s:%d) Unable to set up network listen\n",
            __FILE__, __LINE__);
        exit(-1);
    }

    /* Call a function whenever someone connects to this port */
    gnet_tcp_socket_server_accept_async(sock, new_connection_func, NULL);
}

static GTcpSocket *create_listening_socket(int port)
{
    GTcpSocket *sock = gnet_tcp_socket_server_new_with_port(port);
    if (!sock)
    {
        g_warning("(%s:%d) Unable to create tcp socket on port %d",
            __FILE__, __LINE__, port);
        g_warning("%s", strerror(errno));
        return NULL;
    }

    return sock;
}

static void
new_connection_func(GTcpSocket *server, GTcpSocket *client, gpointer data)
{
    UNUSED(server);
    UNUSED(data);

    /* Ok, someone has connected to us. Let's monitor the connection for data
     * now */
    GIOChannel *ch = gnet_tcp_socket_get_io_channel(client);

    /* TODO: some of this can probably be unified with the udp networking
     * code */
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, FALSE);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);

    if (!g_io_add_watch(ch, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input, NULL))
    {
        g_warning("(%s:%d) Can't add watch on GIOChannel", __FILE__, __LINE__);
        g_io_channel_shutdown(ch, FALSE, NULL);
        g_io_channel_unref(ch);
    }
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
