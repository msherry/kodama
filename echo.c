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
static float nlms_pw(echo *e, float tx, float rx, int update);
static int dtd(echo *e, float tx);
static void hp_fir_destroy(hp_fir *hp);
static float update_fir(hp_fir *hp, float in);


echo *echo_create(hybrid *h)
{
    echo *e = malloc(sizeof(echo));

    e->rx_buf = cbuffer_init(NLMS_LEN);
    e->x      = malloc(NLMS_LEN * sizeof(float));
    e->xf     = malloc(NLMS_LEN * sizeof(float));
    e->w      = malloc(NLMS_LEN * sizeof(float));

    /* HACK: we increment w rather than setting it directly, so it needs to have
     * a valid IEEE-754 value */
    int i;
    for (i = 0; i < NLMS_LEN; i+=2)
    {
        e->x[i] = 0;
        e->x[i+1] = 0;

        e->xf[i] = 1.0/NLMS_LEN;
        e->xf[i+1] = 1.0/NLMS_LEN;

        e->w[i] = 1.0/NLMS_LEN;
        e->w[i+1] = 1.0/NLMS_LEN;
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
        /* tx = update_fir(e->hp, tx); */

        /* Geigel double-talk detector */
        int update = !dtd(e, tx);

        if (!update)
        {
            /* DEBUG_LOG("%s\n", "doubletalk"); */
            any_doubletalk = 1;
        }
        /* else */
        /* { */
        /*     DEBUG_LOG("%s", " "); */
        /* } */

        /* nlms-pw */
        tx = nlms_pw(e, tx, rx, update);

        /* If we're not talking, let's attenuate our signal */
        if (!update)
        {
            tx *= M12dB;
        }

        /* clipping */
        if (tx > MAXPCM)
        {
            tx = MAXPCM;
        }
        else if (tx < -MAXPCM)
        {
            tx = -MAXPCM;
        }
        else
        {
            tx = roundf(tx);
        }

        sb->s[i] = (int)tx;
    }
    if (any_doubletalk)
    {
        DEBUG_LOG("%s\n", "doubletalk");
    }
    else
    {
        DEBUG_LOG("%s\n", "");
    }
}

void echo_update_rx(echo *e, SAMPLE_BLOCK *sb)
{
    cbuffer_push_bulk(e->rx_buf, sb);
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
    /* Shift samples down to make room for new ones. Almost certainly will have
     * to be sped up */
    memmove(e->x+1, e->x, (NLMS_LEN-1)*sizeof(float));
    /* Save the last value of xf[] */
    float last_xf = e->xf[NLMS_LEN-1];
    memmove(e->xf+1, e->xf, (NLMS_LEN-1)*sizeof(float));

    e->x[0] = rx;
    e->xf[0] = iir_highpass(e->Fx, rx); /* pre-whitening of x */

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

    /* DEBUG_LOG("x[0]: %f\txf[0]: %f\n", e->x[0], e->xf[0]); */

    /* TODO: we can update this iteratively for great justice */
    /* DEBUG_LOG("dotp e->xf, e->xf\n") */
    /* e->dotp_xf_xf = dotp(e->xf, e->xf); */
    e->dotp_xf_xf += ((e->xf[0] * e->xf[0]) - last_xf);

    if (e->dotp_xf_xf == 0.0)
    {
        DEBUG_LOG("%s\n", "dotp_xf_xf went to zero");
        int i;
        for (i = 0; i < NLMS_LEN; i++)
        {
            DEBUG_LOG("%.02f ", e->xf[i]);
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
            e->w[i] += u_ef*e->xf[i];
            /* DEBUG_LOG("new e->w[%d]: %f\n", i, e->w[i]) */
            e->w[i+1] += u_ef*e->xf[i+1];
        }
    }

    return err;
}

/*********** DTD functions ***********/

/* Compare against the last NLMS_LEN samples */
static int dtd(echo *e, float tx)
{
    float max = 0.0;
    size_t i;

    /* Get the last DTD_LEN rx samples and find the max*/
    /* TODO: can we just use e->x here? */
    SAMPLE_BLOCK *sb = cbuffer_peek_samples(e->rx_buf, NLMS_LEN);

    for (i=0; i<NLMS_LEN; i++)
    {
        /* TODO: this only works for integral SAMPLE types */
        SAMPLE a = abs(sb->s[i]);
        /* DEBUG_LOG("%d, ", a); */
        if (a > max)
        {
            max = a;
        }
    }
    /* DEBUG_LOG("\n"); */

    SAMPLE a_tx = fabsf(tx);

    if (a_tx > (GeigelThreshold * max))
    {
        e->holdover = DTD_HOLDOVER;
    }

    if (e->holdover)
    {
        e->holdover--;
    }

    sample_block_destroy(sb);

    /* DEBUG_LOG("tx: %5f\ta_tx: %5d\tmax:%5d\tdtd: %d\n", */
    /*     tx, a_tx, (int)max, (e->holdover > 0)) */

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
