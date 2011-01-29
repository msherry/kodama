#include <glib.h>
#include <sys/time.h>

#include "cbuffer.h"
#include "conversation.h"
#include "flv.h"
#include "hybrid.h"
#include "imo_message.h"
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
static Conversation *find_conv_for_stream(const char *stream_name,
        int *conv_side);

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

static void conversation_destroy(Conversation *c)
{
    g_return_if_fail(c != NULL);

    hybrid_destroy(c->h0);
    hybrid_destroy(c->h1);

    free(c);
}

void conversation_start(const char *stream_name)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);

    /* This will be called once per participant in a conversation -- only create
     * one the first time */
    if (!c)
    {
        c = conversation_create();
        g_hash_table_insert(id_to_conv, g_strdup(conv_and_num[0]), c);

        gchar *tmpname = g_strdup_printf("%s:%d", conv_and_num[0], 0);
        hybrid_set_name(c->h0, tmpname);
        g_free(tmpname);

        tmpname = g_strdup_printf("%s:%d", conv_and_num[0], 1);
        hybrid_set_name(c->h1, tmpname);
        g_free(tmpname);

        /* TODO: temporary debugging */
        /* c->h0->tx_cb_fn = shortcircuit_tx_to_rx; */
        /* setup_hw_out(c->h0); */
    }

    g_strfreev(conv_and_num);
}

void conversation_end(const char *stream_name)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);

    int conv_side;
    Conversation *c = find_conv_for_stream(stream_name, &conv_side);

    if (!c)
    {
        return;
    }

    conversation_destroy(c);
    g_hash_table_remove(id_to_conv, (conv_and_num[0]));

    g_strfreev(conv_and_num);
}

int r(const char *stream_name, const unsigned char *flv_data, int flv_len)
{
    struct timeval start, end;
    uint64_t before_cycles, end_cycles;

    SAMPLE_BLOCK *sb = NULL;

    gettimeofday(&start, NULL);
    before_cycles = cycles();

    int conv_side;
    Conversation *c = find_conv_for_stream(stream_name, &conv_side);

    if (!c)
    {
        /* This should have been done on receiving an 'S' message */
        g_warning("Conversation not found for stream %s", stream_name);

        /* TODO: handle this better. We get here at the end of conversations
         * because we've closed one side but not the other yet */
        return -1;
    }

    int ret = flv_parse_tag(flv_data, flv_len, stream_name, &sb);
    if (ret)
    {
        goto exit;
    }

    conversation_process_samples(c, conv_side, sb);

    /* sb has echo-canceled samples. Send them back under the same stream
     * name */

    gettimeofday(&end, NULL);
    long d_us = delta(&start, &end);

    /* TODO: is this kosher? */
    /* sb->pts += d_us/1000; */

    unsigned char *return_flv_packet = NULL;
    int return_flv_len = 0;
    ret = flv_create_tag(&return_flv_packet, &return_flv_len, stream_name, sb);
    if (ret)
    {
        goto free_sample_block;
    }

    /* TODO: we may want to move this part elsewhere */
    unsigned char *return_msg;
    int return_msg_length;
    create_imo_message(&return_msg, &return_msg_length, 'D',
            stream_name, return_flv_packet, return_flv_len);

    /* TODO: send_imo_message was originally static to interface_tcp. Calling it
     * here as a hack, but it should be designed properly */
    send_imo_message(return_msg, return_msg_length);

    end_cycles = cycles();
    /* Reuse end */
    gettimeofday(&end, NULL);
    d_us = delta(&start, &end);

    float mips_cpu = (end_cycles - before_cycles) / (d_us);
    float secs_of_speech = (float)(sb->count)/SAMPLE_RATE;
    float mips_per_ec = mips_cpu / ((secs_of_speech*1E6)/d_us);

    /* g_debug("CPU executes %5.2f MIPS", mips_cpu); */

    g_debug("%.02f ms for %.02f ms of speech (%.02f MIPS / second)",
        (d_us/1000.), secs_of_speech*1000, mips_per_ec);
    g_debug("%5.2f instances possible / core", (mips_cpu/mips_per_ec));

    G_LOCK(stats);
    stats.samples_processed += sb->count;
    stats.total_samples_processed += sb->count;
    stats.total_us += d_us;

    float total_secs_of_speech = (float)(stats.total_samples_processed)/SAMPLE_RATE;
    float total_us = stats.total_us;
    G_UNLOCK(stats);

    float avg_mips_per_ec = mips_cpu / ((total_secs_of_speech*1E6)/total_us);
    g_debug("%5.2f avg instances possible / core", (mips_cpu/avg_mips_per_ec));


    free(return_flv_packet);

free_sample_block:
    sample_block_destroy(sb);

exit:
    return ret;
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

static Conversation *find_conv_for_stream(const char *stream_name,
        int *conv_side)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);
    *conv_side = atoi(conv_and_num[1]);

    g_strfreev(conv_and_num);

    return c;
}
