#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cbuffer.h"
#include "hybrid.h"
#include "interface_udp.h"
#include "kodama.h"
#include "protocol.h"

/*********** Static functions ***********/
static GUdpSocket *udp_listen(int port);
static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static void xmit_data(hybrid *h, hybrid_side side);

typedef struct recv_context {
    GUdpSocket *sock;
    hybrid *h;
    hybrid_side side;
    protocol proto;             /* TODO: probably unnecessary */
} recv_context;


typedef struct xmit_context {
    GUdpSocket *sock;
    GInetAddr *dest;
    hybrid_side side;
    protocol proto;
} xmit_context;

void setup_udp_network_xmit(hybrid *h, gchar *host, int port, hybrid_side side)
{
    GUdpSocket *sock = gnet_udp_socket_new();
    GInetAddr *addr = gnet_inetaddr_new(host, port);

    xmit_context *xc = malloc(sizeof(xmit_context));
    xc->sock = sock;
    xc->dest = addr;
    xc->side = side;
    xc->proto = raw;            /* for now, udp connections are always raw */

    /* Set up callbacks in the hybrid depending on which side will be doing this
     * xmit */
    if (side == tx_side)
    {
        /* The hybrid will need this context when it has data */
        h->tx_cb_data = xc;
        /* When we have data to xmit, send it along */
        h->tx_cb_fn = xmit_data;
    }
    else
    {
        h->rx_cb_data = xc;
        h->rx_cb_fn = xmit_data;
    }
}

void setup_udp_network_recv(hybrid *h, int port, hybrid_side side)
{
    GUdpSocket *sock;
    if ((sock = udp_listen(port)) == NULL)
    {
        g_warning("(%s:%d) Unable to set up network recv\n",
            __FILE__, __LINE__);
        exit(-1);
    }

    /* This is NOT a normal GIOChannel - it can't be read or written to
     * directly. */
    GIOChannel *ch = gnet_udp_socket_get_io_channel(sock);

    /* Set NULL encoding to that NULL bytes are handled properly */
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, FALSE);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);

    recv_context *rc = malloc(sizeof(recv_context));
    rc->sock = sock;
    rc->h = h;
    rc->side = side;
    rc->proto = raw;

    /* TODO: this returns an int. Use it to call g_source_remove when we're
     * done */
    if (!g_io_add_watch(ch, (G_IO_IN | G_IO_HUP | G_IO_ERR),
            handle_input,
            rc))
    {
        g_warning("(%s:%d) Can't add watch on GIOChannel", __FILE__, __LINE__);
        g_io_channel_shutdown(ch, FALSE, NULL);
        g_io_channel_unref(ch);
    }
}


static GUdpSocket *udp_listen(int port)
{
    GUdpSocket *sock = gnet_udp_socket_new_with_port(port);
    if (!sock)
    {
        g_warning("(%s:%d) Unable to create udp socket on port %d",
            __FILE__, __LINE__, port);
        g_warning("%s", strerror(errno));
        return NULL;
    }

    /* "Before deleting the UDP socket, make sure to remove any watches you have
     * added with g_io_add_watch() again with g_source_remove() using the
     * integer id returned by g_io_add_watch(). You may find your program stuck
     * in a busy loop at 100% CPU utilisation if you forget to do this." */

    return sock;
}

static gboolean
handle_input(GIOChannel *source, GIOCondition cond, gpointer data)
{
    recv_context *rc = (recv_context *)data;

    GUdpSocket *sock = rc->sock;
    hybrid *h        = rc->h;
    hybrid_side side = rc->side;

    UNUSED(source);
    UNUSED(cond);

    if(!gnet_udp_socket_has_packet(sock))
    {
        /* Why are we here? */
        g_warning("(%s:%d) no data to receive", __FILE__, __LINE__);
        return TRUE;
    }

    /* Ok, we've received some data over the network. If it's valid audio data,
     * let's put it into our rx_buf */
    gchar buf[65535];           /* Probably a bad idea, but UDP packets can't be
                                 * larger than this */

    /* UDP sockets are special and can't use the normal GIOChannel functions to
     * read data */
    gint num_bytes = gnet_udp_socket_receive(sock, buf, 65535, NULL);
    /* DEBUG_LOG("(%s:%d) Read %d bytes\n", __FILE__, __LINE__, num_bytes); */

    SAMPLE_BLOCK *sb = message_to_samples(buf, num_bytes);

    if (side == tx_side)
    {
        hybrid_put_rx_samples(h, sb);
    }
    else
    {
        hybrid_put_tx_samples(h, sb);
    }
    sample_block_destroy(sb);

    return TRUE;
}

static void xmit_data(hybrid *h, hybrid_side side)
{
    xmit_context *xc;
    if (side == tx_side)
    {
        xc = (xmit_context *)(h->tx_cb_data);
    }
    else
    {
        xc = (xmit_context *)(h->rx_cb_data);
    }

    GUdpSocket *sock = xc->sock;
    GInetAddr *dest = xc->dest;

    /* TODO: don't xmit as soon as we have samples (we get 64 at a time) */

    SAMPLE_BLOCK *sb;
    if (side == tx_side)
    {
        sb = hybrid_get_tx_samples(h, 0);
    }
    else
    {
        sb = hybrid_get_rx_samples(h, 0);
    }

    if (!sb->count)
    {
        /* Why are we here? */
        g_warning("xmit_data was called with no data to xmit\n");
        return;
    }

    gint num_bytes;
    gchar *buf = samples_to_message(sb, &num_bytes, xc->proto);

    gnet_udp_socket_send(sock, buf, num_bytes, dest);

    g_free(buf);
    sample_block_destroy(sb);
}
