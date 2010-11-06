#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "hybrid.h"
#include "interface_network.h"
#include "kodama.h"

/*********** Static functions ***********/
static gboolean udp_listen(int port, hybrid *h);
static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data);

typedef struct socket_and_hybrid {
    GUdpSocket *sock;
    hybrid *h;
} socket_and_hybrid;

void setup_network_xmit(hybrid *h, gchar *host)
{
    /* TODO: change the tx_cb_fn */
}

void setup_network_recv(hybrid *h)
{
    if (!udp_listen(PORTNUM, h))
    {
        g_warning("(%s:%d) Unable to set up network recv\n",
            __FILE__, __LINE__);
        exit(-1);
    }
}


static gboolean udp_listen(int port, hybrid *h)
{
    GUdpSocket *sock = gnet_udp_socket_new_with_port(port);
    if (!sock)
    {
        g_warning("(%s:%d) Unable to create udp socket on port %d",
            __FILE__, __LINE__, port);
        g_warning("%s", strerror(errno));
        return FALSE;
    }

    /* This is NOT a normal GIOChannel - it can't be read or written to
     * directly. */
    GIOChannel *ch = gnet_udp_socket_get_io_channel(sock);

    /* Set NULL encoding to that NULL bytes are handled properly */
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, FALSE);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);

    socket_and_hybrid *sh = malloc(sizeof(socket_and_hybrid));
    sh->sock = sock;
    sh->h = h;

    /* TODO: this returns an int. Use it to call g_source_remove when we're
     * done */
    if (!g_io_add_watch(ch, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input,
            sh))
    {
        g_warning("(%s:%d) Can't add watch on GIOChannel", __FILE__, __LINE__);
        g_io_channel_shutdown(ch, FALSE, NULL);
        g_io_channel_unref(ch);
        return FALSE;
    }

    /* "Before deleting the UDP socket, make sure to remove any watches you have
     * added with g_io_add_watch() again with g_source_remove() using the
     * integer id returned by g_io_add_watch(). You may find your program stuck
     * in a busy loop at 100% CPU utilisation if you forget to do this." */

    return TRUE;
}

static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    socket_and_hybrid *sh = (socket_and_hybrid *)data;
    GUdpSocket *sock = sh->sock;
    hybrid *h = sh->h;

    UNUSED(source);
    UNUSED(cond);

    if(!gnet_udp_socket_has_packet(sock))
    {
        /* Why are we here? */
        g_warning("(%s:%d) no data to receive", __FILE__, __LINE__);
        return TRUE;
    }

    return TRUE;
}
