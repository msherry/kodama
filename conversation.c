#include <glib.h>

#include "cbuffer.h"
#include "conversation.h"
#include "hybrid.h"
#include "protocol.h"

GHashTable *id_to_conv = NULL;

static Conversation *conversation_new(void);
static void conversation_process_samples(Conversation *c, int conv_side,
    SAMPLE_BLOCK *sb);

static Conversation *conversation_new(void)
{

}

void r(const unsigned char *msg, int msg_length)
{
    char *stream_name;
    SAMPLE_BLOCK *sb = imo_message_to_samples(msg, msg_length, &stream_name);

    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    int conv_side = atoi(conv_and_num[1]);

    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);

    if (!c)
    {
        if (!id_to_conv)
        {
            id_to_conv = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, NULL);
        }
        c = conversation_new();
        g_hash_table_insert(id_to_conv, g_strdup(conv_and_num[0]), c);
    }

    conversation_process_samples(c, conv_side, sb);

    /* sb has echo-canceled samples. Send them back under the same stream
     * name */

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

    /* TODO: send the echo-canceled samples back to wowza */
}