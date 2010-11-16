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

float HP_FIR[] = {-0.043183226, -0.046636667, -0.049576525, -0.051936015,
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
    echo *e = malloc(sizeof(echo));

    e->rx_buf = cbuffer_init(NLMS_LEN);
    e->x  = malloc((NLMS_LEN+NLMS_EXT) * sizeof(float));
    e->xf = malloc((NLMS_LEN+NLMS_EXT) * sizeof(float));
    e->w  = malloc(NLMS_LEN * sizeof(float));

    e->j  = NLMS_EXT;

    /* HACK: we increment w rather than setting it directly, so it needs to have
     * a valid IEEE-754 value */
    int i;
    int j = e->j;
    for (i = 0; i < NLMS_LEN; i+=2)
    {
        e->x[j+i] = 0;
        e->x[j+i+1] = 0;

        e->xf[j+i] = 1.0/NLMS_LEN;
        e->xf[j+i+1] = 1.0/NLMS_LEN;

        e->w[j+i] = 1.0/NLMS_LEN;
        e->w[j+i+1] = 1.0/NLMS_LEN;
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
static float dotp(float *a, float *b)
{
    float sum0 = 0.0, sum1 = 0.0;

    int i;
    for (i=0; i<NLMS_LEN; i+=2)
    {
        sum0 += a[i] * b[i];
        sum1 += a[i+1] * b[i+1];
    }
    return sum0+sum1;
}

static float nlms_pw(echo *e, float tx, float rx, int update)
{
    int j = e->j;

    e->x[j] = rx;
    e->xf[j] = iir_highpass(e->Fx, rx); /* pre-whitening of x */

    float dotp_w_x = dotp(e->w, e->x);
    /* DEBUG_LOG("tx: %f\trx: %f\n", tx, rx) */
    /* DEBUG_LOG("dotp(e->w, e->x): %f\n", dotp_w_x) */
    float err = tx - dotp_w_x;
    float ef = iir_highpass(e->Fe, err); /* pre-whitening of err */
    if (isnan(ef))
    {
        DEBUG_LOG("%s\n", "ef went NaN");
        stack_trace(1);
    }

    /* DEBUG_LOG("tx: %f\terr: %f\n", tx, err); */

    /* DEBUG_LOG("x[j]: %f\txf[j]: %f\n", e->x[j], e->xf[j]); */

    /* DEBUG_LOG("dotp e->xf, e->xf\n") */
    /* e->dotp_xf_xf = dotp(e->xf, e->xf); */

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

    /* DEBUG_LOG("dotp_xf_xf: %f\n", e->dotp_xf_xf) */
    if (update)
    {
        float u_ef = STEPSIZE * ef / e->dotp_xf_xf;
        /* DEBUG_LOG("err: %f\tef: %f\tu_ef: %f\n", err, ef, u_ef); */
        if (isinf(u_ef))
        {
            DEBUG_LOG("%s\n", "u_ef went infinite");
            stack_trace(1);
        }

        /* Update tap weights */
        int i;
        for (i = 0; i < NLMS_LEN; i += 2)
        {
            /* DEBUG_LOG("old e->w[%d]: %f\t", i, e->w[i]) */
            e->w[i] += u_ef*e->xf[j+i];
            /* DEBUG_LOG("new e->w[%d]: %f\n", i, e->w[i]) */
            e->w[i+1] += u_ef*e->xf[j+i+1];
        }
    }

    /* Keep us within our sample buffers */
    if (--e->j < 0)
    {
        e->j = NLMS_EXT;
        memmove(e->x+j+1, e->x, (NLMS_LEN-1)*sizeof(float));
        memmove(e->xf+j+1, e->xf, (NLMS_LEN-1)*sizeof(float));
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
        float a = fabsf(e->x[j+i+1]); /* e->x[j] hasn't been set yet */
        /* DEBUG_LOG("%d, ", a); */
        if (a > max)
        {
            max = a;
        }
    }
    /* DEBUG_LOG("\n"); */

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
    free(hp->z);
    free(hp);
}

/* TODO: is this working correctly? */
float update_fir(hp_fir *hp, float in)
{
    /* Shift the samples down to make room for the new one */
    memmove(hp->z+1, hp->z, HP_FIR_SIZE*sizeof(float));

    hp->z[0] = in;

    /* Partially unrolled */
    SAMPLE sum0=0.0, sum1=0.0;
    int i;
    for (i=0; i<=HP_FIR_SIZE; i+=2)
    {
        sum0 += HP_FIR[i] * hp->z[i];
        sum1 += HP_FIR[i+1] * hp->z[i+1];
    }
    return sum0 + sum1;
}
