#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "conversation.h"
#include "imo_message.h"
#include "interface_tcp.h"
#include "protocol.h"
#include "util.h"

typedef struct msg_block
{
    unsigned char *msg;
    int len;
} msg_block;

extern stats_t stats;

/// Incoming imo messages get queued here for the next available thread
GAsyncQueue *work_queue = NULL;

/// Messages to send back to wowza get queued here for the main thread
GAsyncQueue *return_queue = NULL;

static gpointer worker_thread_loop(gpointer data);
static gpointer wowza_thread_loop(gpointer data);

static void queue_imo_message_for_wowza(unsigned char *msg, int msg_length);

void init_protocol(void)
{
#if !THREADED
    g_debug("THREADING DISABLED");
    return;
#endif

    if (work_queue)
    {
        return;
    }
    work_queue = g_async_queue_new();
    return_queue = g_async_queue_new();

    /* We assume the global stats object has been populated at this point -
     * start the number of threads that we've determined is appropriate */
    if (stats.num_threads == 1)
    {
        g_debug("Starting 1 thread");
    }
    else
    {
        g_debug("Starting %d threads (%.01f conversations)",
                stats.num_threads, stats.num_threads * 0.5);
    }

    for (int i = 0; i < stats.num_threads; i++)
    {
        /* TODO: should we save thread objects? */
        /* TODO: monitor when threads crash so we can start them up again */
        g_thread_create(worker_thread_loop, NULL, FALSE, NULL);
    }

    g_thread_create(wowza_thread_loop, NULL, FALSE, NULL);
}

/* PROTOCOL 1 - UDP */

/* Convert a buffer of bytes (gchars) from the network into a SAMPLE_BLOCK,
 * according to the proto in the message. Caller must free SAMPLE_BLOCK */
SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes)
{
    protocol proto = ((proto_header *)buf)->proto;
    size_t data_bytes = num_bytes - sizeof(proto_header);
    gchar *data_start = buf + sizeof(proto_header);
    size_t count = 0;

    SAMPLE_BLOCK *sb = NULL;
    switch (proto)
    {
    case raw:
        count = data_bytes / sizeof(SAMPLE);
        sb = sample_block_create(count);
        memcpy(sb->s, data_start, data_bytes);
        break;
    default:
        DEBUG_LOG("(%s:%d) unknown proto %d\n", __FILE__, __LINE__, proto);
        break;
    }

    /* DEBUG_LOG("(%s:%d) Received %ld samples\n", __FILE__, __LINE__, count); */

    return sb;
}

/* Convert a SAMPLE_BLOCK into a buffer of bytes (gchars) for transmission on
 * the network, according to the given proto. Caller must free buffer */
gchar *samples_to_message(SAMPLE_BLOCK *sb, gint *num_bytes, protocol proto)
{
    /* We have data to xmit - let's throw it into a buffer and send it out */
    gchar *buf;
    size_t data_bytes;

    switch (proto)
    {
    case raw:
        /* TODO: still doesn't consider endianness */
        data_bytes = sb->count * sizeof(SAMPLE);
        *num_bytes = data_bytes + sizeof(proto_header);
        buf = g_malloc(*num_bytes);
        ((proto_header *)buf)->proto = proto;
        memcpy(buf+sizeof(proto_header), sb->s, data_bytes);
        break;
    default:
        DEBUG_LOG("(%s:%d) unknown proto %d\n", __FILE__, __LINE__, proto);
        return NULL;
    }

    return buf;
}


/* PROTOCOL 2 - IMO MESSAGES */

void handle_imo_message(unsigned char *msg, int msg_length)
{
    /* g_debug("Got an imo packet"); */

    char *stream_name;
    char type;

    /* We may or may not get these */
    unsigned char *flv_data = NULL;
    int flv_len = 0;

    int reflect = 1;            /* Reflect this message back unchanged? */

    char *hex;

    decode_imo_message(msg, msg_length, &type, &stream_name, &flv_data,
            &flv_len);

    /* TODO: test reflecting message back with different delays -- see what
     * wowza's deadline is */

    /* int ms = 10; */
    /* usleep(ms * 1000); */

    switch(type)
    {
    case 'S':
        g_debug("Got an S message");
        conversation_start(stream_name);
        break;
    case 'E':
        g_debug("Got an E message");
        /* Any messages from the other side will just be reflected */
        conversation_end(stream_name);
        break;
    case 'D':
        /* g_debug("Got a D message"); */
        if ((!flv_data) || (flv_len == 0))
        {
            g_warning("D message received with no FLV packet");
        }
        else
        {
            unsigned char *return_flv_packet = NULL;
            int return_flv_len;

            int ret;
            int lock_failure_count = 0;
            do {
                ret = r(stream_name, flv_data, flv_len, &return_flv_packet,
                        &return_flv_len);
                if (ret == LOCK_FAILURE)
                {
                    lock_failure_count++;
                    if ((lock_failure_count % 10) == 0)
                    {
                        g_warning("Failed %d times to get process stream %s",
                                lock_failure_count, stream_name);
                    }
                    usleep(100);
                }
            } while (ret == LOCK_FAILURE);

            /* Don't reflect if everything is OK */
            reflect = ((ret != 0) || (return_flv_packet == NULL) ||
                       (return_flv_len == 0));

            if (!reflect)
            {
                unsigned char *return_msg;
                int return_msg_length;
                create_imo_message(&return_msg, &return_msg_length, 'D',
                    stream_name, return_flv_packet, return_flv_len);

#if THREADED
                queue_imo_message_for_wowza(return_msg, return_msg_length);
#else
                send_imo_message(return_msg, return_msg_length);
#endif
            }
            /* Ok to do this even if it's NULL */
            free(return_flv_packet);
        }
        break;
    default:
        g_debug("Unknown message type %c", type);
        hex = hexify(msg, msg_length);
        g_debug("%s", hex);
        free(hex);
    }

    if (reflect)
    {
#if THREADED
        queue_imo_message_for_wowza(msg, msg_length);
#else
        send_imo_message(msg, msg_length);
#endif
    }
    else
    {
        /* Done with this message */
        free(msg);
    }

    free(stream_name);
    free(flv_data);             /* Should be ok to free even if it's NULL */
}

void queue_imo_message_for_worker(unsigned char *msg, int msg_length)
{
    /* g_debug("Queueing a message block for worker threads"); */
    msg_block *mb = malloc(sizeof(msg_block));
    mb->msg = msg;
    mb->len = msg_length;
    g_async_queue_push(work_queue, mb);
}

static void queue_imo_message_for_wowza(unsigned char *msg, int msg_length)
{
    msg_block *mb = malloc(sizeof(msg_block));
    mb->msg = msg;
    mb->len = msg_length;
    g_async_queue_push(return_queue, mb);
}

static gpointer worker_thread_loop(gpointer data)
{
    UNUSED(data);

    g_async_queue_ref(work_queue);
    g_async_queue_ref(return_queue);

    while(TRUE)
    {
        /* g_debug("Waiting for a message block"); */
        msg_block *mb;
        unsigned char *msg;
        int msg_len;

        mb = g_async_queue_pop(work_queue);
        msg = mb->msg;
        msg_len = mb->len;

        /* g_debug("Got a message block"); */
        handle_imo_message(msg, msg_len);
        /* g_debug("Handled a message block"); */

        free(mb);
    }
    g_async_queue_unref(return_queue);
    g_async_queue_unref(work_queue);

    return NULL;
}

static gpointer wowza_thread_loop(gpointer data)
{
    UNUSED(data);

    g_async_queue_ref(return_queue);

    while(TRUE)
    {
        msg_block *mb;
        unsigned char *msg;
        int msg_len;

        mb = g_async_queue_pop(return_queue);
        msg = mb->msg;
        msg_len = mb->len;

        send_imo_message(msg, msg_len);

        free(mb);
    }

    g_async_queue_unref(return_queue);

    return NULL;
}
