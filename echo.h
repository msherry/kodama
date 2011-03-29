#ifndef _ECHO_H_
#define _ECHO_H_

#include "kodama.h"

typedef struct hp_fir {
    float *z;
} hp_fir;

/** dB Values */
#define M0dB (1.0f)
#define M3dB (0.71f)
#define M6dB (0.50f)
#define M9dB (0.35f)
#define M12dB (0.25f)
#define M18dB (0.125f)
#define M24dB (0.063f)

/** dB values for 16bit PCM */
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

/// Number of taps per millisecond of speech
#define TAPS_PER_MS (SAMPLE_RATE / 1000)

/** convergence speed. Range: >0 to <1 (0.2 to 0.7). Larger values give
 * more AEC in lower frequencies, but less AEC in higher frequencies. */
#define STEPSIZE (0.7f)

/** Number of milliseconds of echo path to handle */
#define ECHO_PATH_MS (200)

/** NLMS length in taps (ms * TAPS_PER_MS) */
#define NLMS_LEN (ECHO_PATH_MS * TAPS_PER_MS)

/** Extension for NLMS buffer to minimize memmoves */
#define NLMS_EXT (100)


// Double-talk detection constants

/** DTD Speaker/mic threshold. 0dB for single-talk, 12dB for double-talk */
#define GeigelThreshold (M6dB)
/** Holdover for DTD, in taps (ms * TAPS_PER_MS) */
#define DTD_HOLDOVER (30 * TAPS_PER_MS)
/** Optimize Geigel DTD calculation  */
#define DTD_LEN (80)
#if (NLMS_LEN % DTD_LEN)
#error DTD_LEN must divide evenly into NLMS_LEN
#endif



/// Context for echo-canceling one side of a conversation.
typedef struct echo {
    struct CBuffer *rx_buf;

    /* TODO: is this the same as rx_buf? */
    float *x;                   ///< tap-delayed speaker signal
    float *xf;                  ///< pre-whitened tap-delayed speaker signal
    float *w;                   ///< tap weights

    int j;                      ///< offset into x and xf

    /* Geigel DTD values */
    float *max_x;
    float max_max_x;
    int holdover;               ///< DTD hangover
    int dtd_index;
    int dtd_count;

    /* MECC DTD values */
    float Rem;                  ///< Cross-correlation of err and mic (tx)
    float sig_sqr;              ///< Variance of the microphone signal

    /* DTD fn pointer */
    int (*dtd_fn)(struct echo *, float, float, float);

    hp_fir *hp;                 ///< 300Hz high-pass filter

    /* IIR filters */
    struct IIR_DC *iir_dc;      ///< For rx samples, not tx
    struct IIR *Fx, *Fe;

    double dotp_xf_xf;          ///< rolling dot product of xf

    struct hybrid *h;
} echo;

struct SAMPLE_BLOCK;

echo *echo_create(struct hybrid *h);
void echo_destroy(echo *e);
/**
 * This function is expected to update the samples in sb to remove echo - once
 * it completes, they are ready to go out the tx side of the hybrid.
 *
 * @param e The echo-cancellation context.
 * @param sb The samples to echo-cancel.
 */
void echo_update_tx(echo *e, struct SAMPLE_BLOCK *sb);

/**
 * Just copies samples into the rx part of the echo-cancellation context - no
 * processing is done.
 *
 * @param e Echo-cancellation context.
 * @param sb Samples to copy.
 */
void echo_update_rx(echo *e, struct SAMPLE_BLOCK *sb);

/**
 * Calculate the dot product of two vectors of length NLMS_LEN. Exported for
 * calibration/verification purposes
 *
 * @param a first vector
 * @param b second vector
 * @param len length of vectors
 *
 * @return dot product of a and b
 */
float dotp(const float * restrict a, const float * restrict b, const int len);

#endif
