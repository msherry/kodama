#include <stdlib.h>

#include "cbuffer.h"
#include "hybrid.h"

/* Static prototypes */
static void shortcircuit_tx_to_rx(hybrid *h, hybrid_side side);


hybrid *hybrid_new(void)
{
    hybrid *h = calloc(1, sizeof(hybrid));

    h->tx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);
    h->rx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);

    h->tx_count = 0;
    h->rx_count = 0;

    /* Default callback fn - shortcircuit tx to rx */
    h->tx_cb_fn = shortcircuit_tx_to_rx;
    h->rx_cb_fn = NULL;

    h->tx_cb_data = NULL;

    /* Dummy initial data to simulate delay */
    float NUM_SECONDS = 0;
    int i;
    for (i=0; i<NUM_SECONDS * SAMPLE_RATE * NUM_CHANNELS; i++)
    {
        cbuffer_push(h->rx_buf, SAMPLE_SILENCE);
    }

    return h;
}

void hybrid_destroy(hybrid *h)
{
    if (!h)
    {
        return;
    }

    cbuffer_destroy(h->tx_buf);
    cbuffer_destroy(h->rx_buf);

    free(h);
}

void hybrid_put_tx_samples(hybrid *h, SAMPLE_BLOCK *sb)
{
    /* TODO: increase counts */

    cbuffer_push_bulk(h->tx_buf, sb);

    /* We just got some data - inform whoever cares */
    if (h->tx_cb_fn)
        (*h->tx_cb_fn)(h, tx);
}

void hybrid_put_rx_samples(hybrid *h, SAMPLE_BLOCK *sb)
{
    /* TODO: increase counts */

    cbuffer_push_bulk(h->rx_buf, sb);
    /* We just got some data - inform whoever cares */
    if (h->rx_cb_fn)
        (*h->rx_cb_fn)(h, rx);
}

SAMPLE_BLOCK *hybrid_get_tx_samples(hybrid *h, size_t count)
{
    SAMPLE_BLOCK *sb;
    if (count == 0)
    {
        count = cbuffer_get_count(h->tx_buf);
    }
    sb = cbuffer_get_samples(h->tx_buf, count);
    return sb;
}

SAMPLE_BLOCK *hybrid_get_rx_samples(hybrid *h, size_t count)
{
    SAMPLE_BLOCK *sb;
    if (count == 0)
    {
        count = cbuffer_get_count(h->rx_buf);
    }
    sb = cbuffer_get_samples(h->rx_buf, count);
    return sb;
}



/*********** Hybrid transfer functions ***********/

/* Default transfer function - moves data instantly from tx_buf to rx_buf */
static void shortcircuit_tx_to_rx(hybrid *h, hybrid_side side)
{
    UNUSED(side);

    fprintf(stderr, "shortcircuit_tx_to_rx\n");

    SAMPLE_BLOCK *sb = hybrid_get_tx_samples(h, 0);

    hybrid_put_rx_samples(h, sb);

    sample_block_destroy(sb);
}
