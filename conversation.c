#include <glib.h>

#include "cbuffer.h"
#include "conversation.h"
#include "hybrid.h"
#include "interface_tcp.h"
#include "kodama.h"
#include "protocol.h"
#include "util.h"

GHashTable *id_to_conv = NULL;
extern stats_t stats;

G_LOCK_EXTERN(stats);

static Conversation *conversation_create(void);
static void conversation_process_samples(Conversation *c, int conv_side,
    SAMPLE_BLOCK *sb);

void init_conversations(void)
{
    id_to_conv = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free, NULL);  /* TODO: conversation_destroy */
}

static Conversation *conversation_create(void)
{
    Conversation *c = malloc(sizeof(Conversation));

    c->h0 = hybrid_new();
    c->h1 = hybrid_new();

    hybrid_setup_echo_cancel(c->h0);
    hybrid_setup_echo_cancel(c->h1);

    c->h0->tx_cb_fn = NULL;
    c->h0->rx_cb_fn = NULL;

    c->h1->tx_cb_fn = NULL;
    c->h1->rx_cb_fn = NULL;

    return c;
}

/**
 * Handles processing incoming imo messages - extracts audio data, echo cancels,
 * creates a return message, and sends it back over the network.
 *
 * @param msg The incoming message
 * @param msg_length The length of the incoming message
 */
void r(const unsigned char *msg, int msg_length)
{
    char *stream_name;
    SAMPLE_BLOCK *sb = imo_message_to_samples(msg, msg_length, &stream_name);

    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    int conv_side = atoi(conv_and_num[1]);

    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);

    if (!c)
    {
        c = conversation_create();
        g_hash_table_insert(id_to_conv, g_strdup(conv_and_num[0]), c);

        /* TODO: both sides */
        /* hybrid_set_name(); */
    }

    /* TODO: check for sb == NULL */
    /* TODO: commenting this out should leave samples alone */
    conversation_process_samples(c, conv_side, sb);

    /* sb has echo-canceled samples. Send them back under the same stream
     * name */

    unsigned char *return_msg;
    int return_msg_length;
    return_msg = samples_to_imo_message(sb, &return_msg_length, stream_name);

    char *hex;
    hex = hexify(msg, msg_length);
    /* g_debug("Original message: %s", hex); */
    free(hex);

    hex = hexify(return_msg, return_msg_length);
    /* g_debug("Return message: %s", hex); */
    free(hex);

    /* TODO: send_imo_message was originally static to interface_tcp. Calling it
     * here as a hack, but it should be designed properly */
    send_imo_message(return_msg, return_msg_length);

    G_LOCK(stats);
    stats.samples_processed += sb->count;
    G_UNLOCK(stats);

    sample_block_destroy(sb);
    g_strfreev(conv_and_num);
}

static void conversation_process_samples(Conversation *c, int conv_side,
        SAMPLE_BLOCK *sb)
{
    hybrid *hl = (conv_side == 0) ? c->h0 : c->h1;
    hybrid *hr = (conv_side == 0) ? c->h1 : c->h0;


    /* Let the left-side hybrid see these samples and echo-cancel them */
    hybrid_put_tx_samples(hl, sb);

    /* Now that the samples have been echo-canceled, let the right-side hybrid
     * see them */
    hybrid_put_rx_samples(hr, sb);
}
