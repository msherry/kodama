#ifndef _HYBRID_H_
#define _HYBRID_H_

#include "cbuffer.h"
#include "kodama.h"

/*

                            +---------------------+
            in from hw      |                     |  out to
                 ----------->      tx_buf         ----------->
                            |                     |
                            |                     |
                            |                     |
                            |                     |
                            |                     |
                            |                     |
                 <----------+      rx_buf         <-----------
                            |                     |
                            |                     |
                            +---------------------+
*/

/* Circular typedefs are awesome */
struct hybrid;

typedef void (*hybrid_callback_fn) (struct hybrid *);

typedef struct hybrid {
    CBuffer *tx_buf;
    CBuffer *rx_buf;

    unsigned long tx_count;
    unsigned long rx_count;

    hybrid_callback_fn tx_cb_fn;
    hybrid_callback_fn rx_cb_fn;
} hybrid;


/* Hybrid methods */
hybrid *hybrid_new(void);
void hybrid_destroy(hybrid *h);
SAMPLE_BLOCK *hybrid_get_tx_samples(hybrid *h, size_t count);
SAMPLE_BLOCK *hybrid_get_rx_samples(hybrid *h, size_t count);
void hybrid_put_tx_samples(hybrid *h, SAMPLE_BLOCK *sb);
void hybrid_put_rx_samples(hybrid *h, SAMPLE_BLOCK *sb);

#endif
