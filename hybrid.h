#ifndef _HYBRID_H_
#define _HYBRID_H_

#include "cbuffer.h"
#include "echo.h"
#include "kodama.h"

/*

         client (rx) side                            network (tx) side

                            +---------------------+
        in from hw/wowza    |                     |  out to network
                 ----------->      tx_buf         ----------->
                            |                     |
                            |                     |
                            |                     |
                            |                     |
                            |                     |
          out to hw/wowza   |                     |  in from network
                 <----------+      rx_buf         <-----------
                            |                     |
                            |                     |
                            +---------------------+
*/

/* Circular typedefs are awesome */
struct hybrid;

typedef enum hybrid_side {
    tx, rx
} hybrid_side;


typedef void (*hybrid_callback_fn) (struct hybrid *, hybrid_side side);

typedef struct hybrid {
    CBuffer *tx_buf;
    CBuffer *rx_buf;

    unsigned long tx_count;
    unsigned long rx_count;

    hybrid_callback_fn tx_cb_fn;
    hybrid_callback_fn rx_cb_fn;

    void *tx_cb_data;
    void *rx_cb_data;

    echo *e;
} hybrid;


/* Hybrid methods */
hybrid *hybrid_new(void);
void hybrid_destroy(hybrid *h);
void hybrid_setup_echo_cancel(hybrid *h);

SAMPLE_BLOCK *hybrid_get_tx_samples(hybrid *h, size_t count);
SAMPLE_BLOCK *hybrid_get_rx_samples(hybrid *h, size_t count);
void hybrid_put_tx_samples(hybrid *h, SAMPLE_BLOCK *sb);
void hybrid_put_rx_samples(hybrid *h, SAMPLE_BLOCK *sb);

void hybrid_simulate_tx_delay(hybrid *h, float ms);
void hybrid_simulate_rx_delay(hybrid *h, float ms);

#endif
