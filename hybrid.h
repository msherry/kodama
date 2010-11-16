#ifndef _HYBRID_H_
#define _HYBRID_H_

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
    struct CBuffer *tx_buf;
    struct CBuffer *rx_buf;

    unsigned long tx_count;
    unsigned long rx_count;

    hybrid_callback_fn tx_cb_fn;
    hybrid_callback_fn rx_cb_fn;

    void *tx_cb_data;
    void *rx_cb_data;

    struct echo *e;
} hybrid;


/* Hybrid methods */
hybrid *hybrid_new(void);
void hybrid_destroy(hybrid *h);
void hybrid_setup_echo_cancel(hybrid *h);

struct SAMPLE_BLOCK *hybrid_get_tx_samples(hybrid *h, size_t count);
struct SAMPLE_BLOCK *hybrid_get_rx_samples(hybrid *h, size_t count);
int hybrid_get_tx_sample_count(hybrid *h);
int hybrid_get_rx_sample_count(hybrid *h);
void hybrid_put_tx_samples(hybrid *h, struct SAMPLE_BLOCK *sb);
void hybrid_put_rx_samples(hybrid *h, struct SAMPLE_BLOCK *sb);
void hybrid_put_rx_samples_direct(hybrid *h, struct SAMPLE_BLOCK *sb);

void hybrid_simulate_tx_delay(hybrid *h, float ms);
void hybrid_simulate_rx_delay(hybrid *h, float ms);

#endif
