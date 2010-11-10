#ifndef _ECHO_H_
#define _ECHO_H_

#include "cbuffer.h"
#include "kodama.h"

typedef struct hp_fir {
    float *z;
} hp_fir;

/* dB Values */
#define M0dB (1.0f)
#define M3dB (0.71f)
#define M6dB (0.50f)
#define M9dB (0.35f)
#define M12dB (0.25f)
#define M18dB (0.125f)
#define M24dB (0.063f)

/* dB values for 16bit PCM */
/* MxdB_PCM = 32767 * 10 ^ (x / 20) */
#define M10dB_PCM (10362.0f)
#define M20dB_PCM (3277.0f)
#define M25dB_PCM (1843.0f)
#define M30dB_PCM (1026.0f)
#define M35dB_PCM (583.0f)
#define M40dB_PCM (328.0f)
#define M45dB_PCM (184.0f)
#define M50dB_PCM (104.0f)
#define M55dB_PCM (58.0f)
#define M60dB_PCM (33.0f)
#define M65dB_PCM (18.0f)
#define M70dB_PCM (10.0f)
#define M75dB_PCM (6.0f)
#define M80dB_PCM (3.0f)
#define M85dB_PCM (2.0f)
#define M90dB_PCM (1.0f)

#define MAXPCM (32767.0f)

#define MIN_XF M85dB_PCM

/* convergence speed. Range: >0 to <1 (0.2 to 0.7). Larger values give
 * more AEC in lower frequencies, but less AEC in higher frequencies. */
#define STEPSIZE (0.7f)

/* Number of taps for high-pass (300Hz+) filter */
#define HP_FIR_SIZE (13)

/* Number of taps for DTD calculation */
#define DTD_LEN (16)

/* Holdover for DTD, in taps */
#define DTD_HOLDOVER (30)

/* DTD Speaker/mic threshold. 0dB for single-talk, 12dB for double-talk */
#define GeigelThreshold (M6dB)

/* NLMS length in taps */
#define NLMS_LEN (128)

/* zero */
#define EPSILON (0.000001f)

typedef struct echo {
    CBuffer *rx_buf;

    /* TODO: is this the same as rx_buf? */
    float *x;                   /* tap delayed speaker signal */
    float *xf;                  /* pre-whitened tap delayed speaker signal */
    float *w;                   /* tap weights */

    /* DTD */
    int holdover;

    /* FIR filter */
    hp_fir *hp;

    /* IIR filters */
    struct IIR_DC *iir_dc;
    struct IIR *Fx, *Fe;

    double dotp_xf_xf;

    struct hybrid *h;
} echo;

echo *echo_create(struct hybrid *h);
void echo_destroy(echo *e);
void echo_update_tx(echo *e, SAMPLE_BLOCK *sb);
void echo_update_rx(echo *e, SAMPLE_BLOCK *sb);

#endif
