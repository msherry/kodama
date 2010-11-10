#ifndef _ECHO_H_
#define _ECHO_H_

#include "cbuffer.h"
#include "kodama.h"

/* Ridiculous forward declaration */
typedef struct hybrid *hybrid_ptr;

typedef struct hp_fir {
    SAMPLE *z;
} hp_fir;

/* dB Values */
#define M0dB (1.0f)
#define M3dB (0.71f)
#define M6dB (0.50f)
#define M9dB (0.35f)
#define M12dB (0.25f)
#define M18dB (0.125f)
#define M24dB (0.063f)


/* Number of taps for high-pass (300Hz+) filter */
#define HP_FIR_SIZE (13)

/* Number of taps for DTD calculation */
#define DTD_LEN (16)

/* Holdover for DTD, in taps */
#define DTD_HOLDOVER (30)

/* DTD Speaker/mic threshold. 0dB for single-talk, 12dB for double-talk */
#define GeigelThreshold (M6dB)



typedef struct echo {
    CBuffer *rx_buf;

    /* DTD */
    int holdover;

    /* HP filter */
    hp_fir *hp;

    hybrid_ptr h;
} echo;

echo *echo_create(hybrid_ptr h);
void echo_destroy(echo *e);
void echo_update_tx(echo *e, SAMPLE_BLOCK *sb);
void echo_update_rx(echo *e, SAMPLE_BLOCK *sb);

#endif
