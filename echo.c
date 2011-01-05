#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "echo.h"
#include "hybrid.h"
#include "iir.h"
#include "kodama.h"

/*
source:
http://www.dsptutor.freeuk.com/KaiserFilterDesign/KaiserFilterDesign.html
Kaiser Window FIR Filter

Filter type: High pass
Passband: 300.0 - 4000.0 Hz
Order: 12
Transition band: 100.0 Hz
Stopband attenuation: 10.0 dB
*/

extern globals_t globals;

const float HP_FIR[] = {-0.043183226, -0.046636667, -0.049576525, -0.051936015,
                  -0.053661242, -0.054712527, 0.82598513, -0.054712527,
                  -0.053661242, -0.051936015, -0.049576525, -0.046636667,
                  -0.043183226};

/* Number of taps for high-pass (300Hz+) filter */
#define HP_FIR_SIZE (13)

/********* Static functions *********/
static hp_fir *hp_fir_create(void);
static inline float clip(float in);
static float nlms_pw(echo *e, float tx, float rx, int update);
static int dtd(echo *e, float tx);
static void hp_fir_destroy(hp_fir *hp);
static float update_fir(hp_fir *hp, float in);


echo *echo_create(hybrid *h)
{
    echo * restrict e = malloc(sizeof(echo));

    e->rx_buf = cbuffer_init((size_t)NLMS_LEN);
    e->x  = malloc((NLMS_LEN+NLMS_EXT) * sizeof(float));
    e->xf = malloc((NLMS_LEN+NLMS_EXT) * sizeof(float));
    e->w  = malloc(NLMS_LEN * sizeof(float));

    e->j  = NLMS_EXT;

    /* HACK: we increment w rather than setting it directly, so it needs to have
     * a valid IEEE-754 value */
    int i;
    int j = e->j;
    for (i = 0; i < NLMS_LEN; i++)
    {
        e->x[j+i] = 0;
        e->xf[j+i] = 1.0/NLMS_LEN;
        e->w[i] = 1.0/NLMS_LEN;
    }

    e->hp = hp_fir_create();

    e->Fx = iir_create();
    e->Fe = iir_create();
    e->iir_dc = iirdc_create();

    e->dotp_xf_xf = M80dB_PCM;

    e->h = h;
    return e;
}

void echo_destroy(echo *e)
{
    if (!e)
    {
        return;
    }

    free(e->w);
    free(e->xf);
    free(e->x);

    cbuffer_destroy(e->rx_buf);

    hp_fir_destroy(e->hp);
    iir_destroy(e->Fx);
    iir_destroy(e->Fe);
    iirdc_destroy(e->iir_dc);

    free(e);
}

/* This function is expected to update the samples in sb to remove echo - once
 * it completes, they are ready to go out the tx side of the hybrid */
void echo_update_tx(echo *e, SAMPLE_BLOCK *sb)
{
    size_t i;
    int any_doubletalk = 0;
    for (i=0; i<sb->count; i++)
    {
        SAMPLE rx_s, tx_s;
        float tx, rx;

        rx_s = cbuffer_pop(e->rx_buf);
        tx_s = sb->s[i];

        tx = (float)tx_s;
        rx = (float)rx_s;

        /* High-pass filter - filter out sub-300Hz signals */
        tx = update_fir(e->hp, tx);

        /* Geigel double-talk detector */
        int update = !dtd(e, tx);

        /* nlms-pw */
        tx = nlms_pw(e, tx, rx, update);

        /* If we're not talking, let's attenuate our signal */
        if (update)
        {
            tx *= M12dB;
        }
        else
        {
            any_doubletalk = 1;
        }

        /* clipping */
        tx = clip(tx);

        sb->s[i] = (int)tx;
    }
    VERBOSE_LOG("%s\n", any_doubletalk ? "doubletalk" : " ");
}

void echo_update_rx(echo *e, SAMPLE_BLOCK *sb)
{
    cbuffer_push_bulk(e->rx_buf, sb);
}

static inline float clip(float in)
{
    float out;
    if (in > MAXPCM)
    {
        out = MAXPCM;
    }
    else if (in < -MAXPCM)
    {
        out = -MAXPCM;
    }
    else
    {
        out = roundf(in);
    }
    return out;
}

/*********** NLMS functions ***********/
static float dotp(float * restrict a, float * restrict b)
{
    float sum = 0.0;

    int i;
    for (i=0; i<NLMS_LEN; i++)
    {
        sum += a[i] * b[i];
    }
    return sum;
}

static float nlms_pw(echo *e, float tx, float rx, int update)
{
    int j = e->j;

    e->x[j] = rx;
    e->xf[j] = iir_highpass(e->Fx, rx); /* pre-whitening of x */

    float dotp_w_x = dotp(e->w, e->x+j);
    float err = tx - dotp_w_x;
    float ef = iir_highpass(e->Fe, err); /* pre-whitening of err */
    if (isnan(ef))
    {
        DEBUG_LOG("%s\n", "ef went NaN");
        stack_trace(1);
    }

    /* Iterative update */
    e->dotp_xf_xf += (e->xf[j] * e->xf[j] -
        e->xf[j+NLMS_LEN-1] * e->xf[j+NLMS_LEN-1]);

    if (e->dotp_xf_xf == 0.0)
    {
        DEBUG_LOG("%s\n", "dotp_xf_xf went to zero");
        int i;
        for (i = 0; i < NLMS_LEN; i++)
        {
            DEBUG_LOG("%.02f ", e->xf[j+i]);
        }
        DEBUG_LOG("%s\n\n", "");
        stack_trace(1);
    }

    if (update)
    {
        float u_ef = STEPSIZE * ef / e->dotp_xf_xf;
        if (isinf(u_ef))
        {
            DEBUG_LOG("%s\n", "u_ef went infinite");
            DEBUG_LOG("ef: %f\tdotp_xf_xf: %f\n", ef, e->dotp_xf_xf);
            stack_trace(1);
        }

        /* Update tap weights */
        int i;

        float * restrict weights = e->w;
        float * restrict xf      = e->xf;

        for (i = 0; i < NLMS_LEN; i++)
        {
            weights[i] += u_ef*xf[j+i];
        }
    }

    /* Keep us within our sample buffers */
    if (--e->j < 0)
    {
        e->j = NLMS_EXT;
        memmove(e->x+e->j+1, e->x, (NLMS_LEN-1)*sizeof(float));
        memmove(e->xf+e->j+1, e->xf, (NLMS_LEN-1)*sizeof(float));
    }

    return err;
}

/*********** DTD functions ***********/

/* Compare against the last NLMS_LEN samples */
/* TODO: apparently Geigel works well on line echo, but rather more poorly on
 * acoustic echo. Look into something more sophisticated. */
static int dtd(echo *e, float tx)
{
    /* Get the last NLMS_LEN rx samples and find the max*/
    float max = 0.0;
    size_t i;
    int j = e->j;

    for (i=0; i<NLMS_LEN-1; i++)
    {
        float rx = fabsf(e->x[j+i+1]); /* e->x[j] hasn't been set yet */
        if (rx > max)
        {
            max = rx;
        }
    }

    float a_tx = fabsf(tx);

    if (a_tx > (GeigelThreshold * max))
    {
        e->holdover = DTD_HOLDOVER;
    }

    if (e->holdover)
    {
        e->holdover--;
    }

    VERBOSE_LOG("tx: %5d\ta_tx: %5d\tmax:%5d\tdtd: %d\n",
        (int)tx, (int)a_tx, (int)max, (e->holdover > 0))

    return e->holdover > 0;
}

/*********** High-pass FIR functions ***********/
static hp_fir *hp_fir_create(void)
{
    hp_fir *h = malloc(sizeof(hp_fir));
    /* 13-tap filter */
    h->z = calloc(HP_FIR_SIZE+1, sizeof(float));

    return h;
}

void hp_fir_destroy(hp_fir *hp)
{
    if (!hp)
    {
        return;
    }
    free(hp->z);
    free(hp);
}

/* TODO: is this working correctly? */
float update_fir(hp_fir * restrict hp, float in)
{
    /* Shift the samples down to make room for the new one */
    memmove(hp->z+1, hp->z, (HP_FIR_SIZE-1)*sizeof(float));

    hp->z[0] = in;

    float sum = 0.0;
    int i;
    for (i=0; i<HP_FIR_SIZE; i++)
    {
        sum += HP_FIR[i] * hp->z[i];
    }
    return sum;
}
