#include <glib.h>
#include <sys/time.h>

#include "cbuffer.h"
#include "conversation.h"
#include "flv.h"
#include "hybrid.h"
#include "kodama.h"
#include "util.h"

GHashTable *id_to_conv = NULL;
/// Messages arriving for these aren't necessarily an error
GHashTable *closed_conversations = NULL;

extern globals_t globals;
extern stats_t stats;

GStaticRWLock id_to_conv_rwlock = G_STATIC_RW_LOCK_INIT;

G_LOCK_DEFINE(closed_conversations); /* TODO: make this a rwlock */
G_LOCK_EXTERN(stats);

static Conversation *conversation_create(void);
static void conversation_destroy(Conversation *c);
static void conversation_process_samples(Conversation *c, int conv_side,
    SAMPLE_BLOCK *sb);
static Conversation *find_conv_for_stream(const char *stream_name,
        int *conv_side);
static Conversation *find_conv_for_stream_nolock(const char *stream_name,
        int *conv_side);
static gboolean conv_is_closed(const char *stream_name);

void init_conversations(void)
{
    g_static_rw_lock_writer_lock(&id_to_conv_rwlock);
    id_to_conv = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free,
            /* We don't put the destroy function here - explained below */
            NULL);
    g_static_rw_lock_writer_unlock(&id_to_conv_rwlock);

    G_LOCK(closed_conversations);
    closed_conversations = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free, NULL);
    G_UNLOCK(closed_conversations);
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

    c->c0_mutex = g_mutex_new();
    c->c1_mutex = g_mutex_new();
    c->echo_mutex = g_mutex_new();

    return c;
}

static void conversation_destroy(Conversation *c)
{
    g_return_if_fail(c != NULL);

    hybrid_destroy(c->h0);
    hybrid_destroy(c->h1);

    g_mutex_free(c->c0_mutex);
    g_mutex_free(c->c1_mutex);
    g_mutex_free(c->echo_mutex);

    free(c);
}

void conversation_start(const char *stream_name)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);

    g_static_rw_lock_writer_lock(&id_to_conv_rwlock);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);

    /* This will be called once per participant in a conversation -- only create
     * one the first time */
    if (!c)
    {
        c = conversation_create();

        gchar *stream_name_0, *stream_name_1;

        stream_name_0 = g_strdup_printf("%s:%d", conv_and_num[0], 0);
        stream_name_1 = g_strdup_printf("%s:%d", conv_and_num[0], 1);

        hybrid_set_name(c->h0, stream_name_0);
        hybrid_set_name(c->h1, stream_name_1);

        /* Debugging only - shortcircuit audio directly to hardware */
        /* c->h0->tx_cb_fn = shortcircuit_tx_to_rx; */
        /* setup_hw_out(c->h0); */

        flv_start_stream(stream_name_0);
        flv_start_stream(stream_name_1);

        g_free(stream_name_0);
        g_free(stream_name_1);

        g_hash_table_insert(id_to_conv, g_strdup(conv_and_num[0]), c);
    }
    g_static_rw_lock_writer_unlock(&id_to_conv_rwlock);

    g_strfreev(conv_and_num);
}

void conversation_end(const char *stream_name)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);

    /* TODO: fix these comments to deal with separate mutexes */

    /* If one side of the conversation is processing samples, and we try to
     * destroy the conversation via other side, we free the mutex while another
     * thread holds it. This is bad.
     *
     * So instead, we remove the conversation from the hash table so no other
     * thread can find it, including another 'E' message. We then acquire and
     * release its lock. Once we do, we can immediately free it without worrying
     * that another thread will grab it again */

    g_static_rw_lock_writer_lock(&id_to_conv_rwlock);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);
    g_hash_table_remove(id_to_conv, conv_and_num[0]);
    g_static_rw_lock_writer_unlock(&id_to_conv_rwlock);

    if(c)
    {
        gchar *stream_name_0, *stream_name_1;

        stream_name_0 = g_strdup_printf("%s:%d", conv_and_num[0], 0);
        stream_name_1 = g_strdup_printf("%s:%d", conv_and_num[0], 1);

        g_mutex_lock(c->c0_mutex);
        g_mutex_lock(c->c1_mutex);

        /* TODO: do we need to acquire the echo mutex? Probably not, since it
         * can't be held without holding c0 or c1 */

        g_mutex_unlock(c->c1_mutex);
        g_mutex_unlock(c->c0_mutex);

        flv_end_stream(stream_name_0);
        flv_end_stream(stream_name_1);

        g_free(stream_name_0);
        g_free(stream_name_1);

        conversation_destroy(c);

        /* If c still existed, this must be the first 'E' message for the
         * conversation. Mark the conversation as closed so further messages
         * about this conversation don't cause error messages */
        G_LOCK(closed_conversations);
        g_hash_table_insert(closed_conversations, g_strdup(conv_and_num[0]),
                NULL);
        G_UNLOCK(closed_conversations);
    }
    else
    {
        /* No conversation existed - this is most likely the second 'E' message
         * for this conversation. Remove it from closed_conversations */
        G_LOCK(closed_conversations);
        gboolean found = g_hash_table_remove(closed_conversations,
                conv_and_num[0]);
        G_UNLOCK(closed_conversations);
        if (!found)
        {
            g_warning("Received a second 'E' message for %s, but conv not found in closed_conversations", conv_and_num[0]);
        }
    }
    g_strfreev(conv_and_num);
}

int r(const char *stream_name, const unsigned char *flv_data, int flv_len,
    unsigned char **return_flv_packet, int *return_flv_len)
{
    struct timeval start, end;
    struct timeval t1, t2;
    uint64_t before_cycles, end_cycles;
    long d_us;

    SAMPLE_BLOCK *sb = NULL;

    gettimeofday(&start, NULL);
    before_cycles = cycles();

    int conv_side;

    gettimeofday(&t1, NULL);
    g_static_rw_lock_reader_lock(&id_to_conv_rwlock);
    gettimeofday(&t2, NULL);

    d_us = delta(&t1, &t2);

    Conversation *c = find_conv_for_stream_nolock(stream_name, &conv_side);

    /* This log message comes after find_conv_for_stream_nolock just so
     * conv_side is set properly - that call isn't included in the timing
     * data */
    /* VERBOSE_LOG("C: Time to acquire id_to_conv rwlock (reader) for side %d: %li\n", */
    /*         conv_side, d_us); */
    if (!c)
    {
        g_static_rw_lock_reader_unlock(&id_to_conv_rwlock);

        /* Maybe the conversation was recently closed */
        if (!conv_is_closed(stream_name))
        {
            g_warning("Conversation not found for stream %s", stream_name);
        }
        return -1;
    }

    /* Try for a mutex for just our side of the conversation for now */
    GMutex *conv_side_mutex = (conv_side == 0) ? c->c0_mutex : c->c1_mutex;

    gboolean got_lock = g_mutex_trylock(conv_side_mutex);
    g_static_rw_lock_reader_unlock(&id_to_conv_rwlock);

    if (!got_lock)
    {
        /* TODO: fix these comments to deal with the new 2-mutex system */

        /* c exists, but we couldn't immediately get its mutex. Someone else is
         * using c, possibly deleting it. If they're deleting it, we don't want
         * to wait around for its mutex to become free, so we tell our caller to
         * try again -- next time they try, c might be free or not exist */

        /* TODO: if the thread holding this lock crashes for some reason, c will
         * never become available, and all threads might eventually end up
         * waiting on it. If no thread can make progress towards acquiring
         * c->mutex for some amount of time, we might want to forcibly remove
         * c */
        /* VERBOSE_LOG("C: Failed to get conv lock for side %d\n", conv_side); */
        return LOCK_FAILURE;
    }

    /* c still exists, and we got its mutex successfully. */

    gettimeofday(&t1, NULL);
    d_us = delta(&start, &t1);

    /* VERBOSE_LOG("C: Time to acquire conversation lock: %li\n", d_us); */

    int ret = flv_parse_tag(flv_data, flv_len, stream_name, &sb);
    if (ret)
    {
        /* TODO: We often get errors from libspeex after parsing exactly 84 bits
         * of the stream - mode (m) is set to 11, which isn't a valid
         * mode. Figure out why. */

        /* Valid modes:
           http://www.speex.org/docs/manual/speex-manual/node10.html
        */
        char *hex = hexify(flv_data, flv_len);
        g_debug("Error parsing tag: %s", hex);
        free(hex);
        goto exit;
    }

    gettimeofday(&t2, NULL);
    d_us = delta(&t1, &t2);

    /* VERBOSE_LOG("C: Time to parse flv tag: %li\n", d_us); */

    conversation_process_samples(c, conv_side, sb);

    gettimeofday(&t1, NULL);
    d_us = delta(&t2, &t1);
    /* VERBOSE_LOG("C: Time to echo-cancel samples: %li\n", d_us); */

    /* sb now contains echo-canceled samples */

    gettimeofday(&end, NULL);
    d_us = delta(&start, &end);

    /* TODO: is this kosher? */
    /* sb->pts += d_us/1000; */

    *return_flv_packet = NULL;
    *return_flv_len = 0;
    ret = flv_create_tag(return_flv_packet, return_flv_len, stream_name, sb);
    if (ret)
    {
        goto free_sample_block;
    }

    gettimeofday(&t2, NULL);
    d_us = delta(&t1, &t2);
    /* VERBOSE_LOG("C: Time to create return FLV tag: %li\n", d_us); */


    end_cycles = cycles();
    /* Reuse end */
    gettimeofday(&end, NULL);
    d_us = delta(&start, &end);

    float mips_cpu = (end_cycles - before_cycles) / (d_us);
    float secs_of_speech = (float)(sb->count)/SAMPLE_RATE;
    float mips_per_ec = mips_cpu / ((secs_of_speech*1E6)/d_us);

    /* VERBOSE_LOG("CPU executes %5.2f MIPS\n", mips_cpu); */

    /* VERBOSE_LOG("C: %.02f ms for %.02f ms of speech (%.02f MIPS / ec)" */
    /*         "- side %d\n", (d_us/1000.), secs_of_speech*1000, mips_per_ec, */
    /*         conv_side); */
    /* VERBOSE_LOG("%5.2f instances possible / core\n", (mips_cpu/mips_per_ec)); */

    G_LOCK(stats);
    stats.samples_processed += sb->count;
    stats.total_samples_processed += sb->count;
    stats.total_us += d_us;

    float total_secs_of_speech = (float)(stats.total_samples_processed)/SAMPLE_RATE;
    float total_us = stats.total_us;
    G_UNLOCK(stats);

    float avg_mips_per_ec = mips_cpu / ((total_secs_of_speech*1E6)/total_us);
    /* VERBOSE_LOG("%5.2f avg instances possible / core\n", */
    /*         (mips_cpu/avg_mips_per_ec)); */


free_sample_block:
    sample_block_destroy(sb);

exit:
    g_mutex_unlock(conv_side_mutex);
    return ret;
}

/* This should be called with a lock held on c */
static void conversation_process_samples(Conversation *c, int conv_side,
        SAMPLE_BLOCK *sb)
{
    hybrid *hl = (conv_side == 0) ? c->h0 : c->h1;
    hybrid *hr = (conv_side == 0) ? c->h1 : c->h0;

    /* Only one thread can update samples at a time, since it affects both
     * sides */
    g_mutex_lock(c->echo_mutex);

    /* TODO: use sb->pts to determine a) if these samples are too old for us to
     * care about, and b) if we need to insert them other than at the head of
     * the queue */

    /* Let the left-side hybrid see these samples and echo-cancel them */
    hybrid_put_tx_samples(hl, sb);

    /* Now that the samples have been echo-canceled, let the right-side hybrid
     * see them */
    hybrid_put_rx_samples(hr, sb);

    g_mutex_unlock(c->echo_mutex);
}

/* Called when not holding the lock on id_to_conv */
static Conversation *find_conv_for_stream(const char *stream_name,
        int *conv_side)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    g_static_rw_lock_reader_lock(&id_to_conv_rwlock);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);
    g_static_rw_lock_reader_unlock(&id_to_conv_rwlock);
    *conv_side = atoi(conv_and_num[1]);

    g_strfreev(conv_and_num);

    return c;
}

/* Assumes the lock is held externally */
static Conversation *find_conv_for_stream_nolock(const char *stream_name,
        int *conv_side)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);
    Conversation *c = g_hash_table_lookup(id_to_conv, conv_and_num[0]);
    *conv_side = atoi(conv_and_num[1]);

    g_strfreev(conv_and_num);

    return c;
}

static gboolean conv_is_closed(const char *stream_name)
{
    gchar **conv_and_num = g_strsplit(stream_name, ":", 2);

    G_LOCK(closed_conversations);
    gboolean found = g_hash_table_lookup_extended(closed_conversations,
            conv_and_num[0], NULL, NULL);
    G_UNLOCK(closed_conversations);

    g_strfreev(conv_and_num);

    return found;
}
