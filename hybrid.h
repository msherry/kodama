#ifndef _HYBRID_H_
#define _HYBRID_H_

#include "cbuffer.h"

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

#endif
