#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <gnet.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "imo_message.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"
#include "read_write.h"
#include "util.h"

static char *g_host;
static int g_port;

int attempt_reconnect;
int wowza_fd = -1;      /* TODO: we probably want something more flexible */
GIOChannel *wowza_channel = NULL;

static gboolean
    handle_input(GIOChannel *source, GIOCondition cond, gpointer data);
static gboolean
    handle_output(GIOChannel *source, GIOCondition cond, gpointer data);
static void handle_imo_message(const unsigned char *msg, int msg_len);
static void send_imo_message(const unsigned char *msg, int msg_len);
static void flv_parse_tag(const unsigned char *packet_data, const int packet_len);

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

    g_debug("Successfully connected to wowza on %s:%d", g_host, g_port);

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
            g_debug("Remote end closed connection");
        }
        /* TODO: clean up any user data */
        /* TODO: attempt to reconnect periodically */

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
        unsigned char *msg;
        int msg_length;
        n = get_next_message(fd, &msg, &msg_length);
        handle_imo_message(msg, msg_length);
        free(msg);
    }

    /* Return TRUE to keep this handler intact (don't unregister it) */
    return TRUE;
}

static void handle_imo_message(const unsigned char *msg, int msg_length)
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

    g_debug("Got an imo packet");

    char type;
    unsigned char *stream_name, *packet_data;
    int data_len;

    decode_imo_message(msg, msg_length, &type, &stream_name, &packet_data,
            &data_len);

    g_debug("Size: %d", msg_length);
    g_debug("Type: %c", type);
    g_debug("Stream name: %s", stream_name);


    if (data_len > 0)
    {
        char *hex = hexify(packet_data, data_len);
        g_debug("FLV packet data: %s", hex);
        flv_parse_tag(packet_data, data_len);
        free(hex);
    }

    g_debug("\n\n");


    /* TODO: TEMPORARY. We're just going to send the packets right back to where
     * they came from, for now. */
    send_imo_message(msg, msg_length);

    free(stream_name);
    free(packet_data);
}

static void send_imo_message(const unsigned char *msg, int msg_len)
{
    if (!msg || msg_len == 0)
    {
        return;
    }

    /* TODO: Who do we send data to? We probably only have one wowza
     * connection, but it would be good to make this more general */
    if (wowza_fd == -1)
    {
        /* I guess wowza is down. */
        /* TODO: If wowza is smart enough to remember streams across restarts,
         * we should queue data for wowza here */
        return;
    }

    queue_message(wowza_fd, msg, msg_len);

    /* Add a watch on the channel so we write data once the channel is
     * writable*/
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

/* TODO: this is most likely a temporary function */
/* Parses an FLV tag (not the stream header) */
static void flv_parse_tag(const unsigned char *packet_data, const int packet_len)
{
    /* For details of this format, see:
       http://osflash.org/flv
    */

    unsigned char type_code, type;
    int offset = 0;

    g_debug("Packet length: %d", packet_len);

    type_code = packet_data[offset++];
    switch(type_code)
    {
    case 0x08:
        /* Audio */
        type = 'A';
        break;
    case 0x09:
        /* Video */
        type = 'V';
        break;
    case 0x12:
        /* Meta */
        type = 'M';
        break;
    default:
        /* Unknown */
        type = 'U';
        g_debug("Unknown packet type: %c", type_code);
        break;
    }
    g_debug("Type: %c", type);

    /* 3 bytes, big-endian */
    unsigned int bodyLength = 0;
    bodyLength |= (packet_data[offset++] << 16);
    bodyLength |= (packet_data[offset++] << 8);
    bodyLength |= (packet_data[offset++] << 0);
    g_debug("BodyLength: %d", bodyLength);

    /* Timestamp - 4 bytes, crazy order */
    unsigned int timestamp = 0;
    timestamp |= (packet_data[offset++] << 16);
    timestamp |= (packet_data[offset++] << 8);
    timestamp |= (packet_data[offset++] << 0);
    timestamp |= (packet_data[offset++] << 24);
    g_debug("Timestamp: %u  (%#.8x)", timestamp, timestamp);

    /* stream id is 3 bytes, and always zero - skip it */
    offset += 3;

    /* The rest is packet data, except the last 4 bytes, which should contain
     * the size of this packet */
    offset = (packet_len - 4);
    /* A full 4-byte integer, big-endian. Read it the hard way since ints are
     * probably 8 bytes for us */
    unsigned int prev_tag_size = 0;
    prev_tag_size |= (packet_data[offset++] << 24);
    prev_tag_size |= (packet_data[offset++] << 16);
    prev_tag_size |= (packet_data[offset++] << 8);
    prev_tag_size |= (packet_data[offset++] << 0);
    g_debug("PrevTagSize: %u  (%#.8x)", prev_tag_size, prev_tag_size);
}
