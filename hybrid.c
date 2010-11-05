#include <stdlib.h>

#include "cbuffer.h"
#include "hybrid.h"

hybrid *hybrid_new(void)
{
    hybrid *h = calloc(1, sizeof(hybrid));

    h->tx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);
    h->rx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);

    h->tx_count = 0;
    h->rx_count = 0;

    h->tx_cb_fn = NULL;
    h->rx_cb_fn = NULL;

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
}

void hybrid_put_rx_samples(hybrid *h, SAMPLE_BLOCK *sb)
{
    /* TODO: increase counts */

    cbuffer_push_bulk(h->rx_buf, sb);
}

SAMPLE_BLOCK *hybrid_get_tx_samples(hybrid *h)
{
    SAMPLE_BLOCK *sb = cbuffer_get_all(h->tx_buf);
    return sb;
}

SAMPLE_BLOCK *hybrid_get_rx_samples(hybrid *h)
{
    SAMPLE_BLOCK *sb = cbuffer_get_all(h->rx_buf);
    return sb;
}
