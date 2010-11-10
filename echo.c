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


/********* Static functions *********/
static hp_fir *hp_fir_create(void);
static SAMPLE nlms_pw(echo *e, SAMPLE tx, SAMPLE rx, int update);
static int dtd(echo *e, SAMPLE tx, SAMPLE rx);
static void hp_fir_destroy(hp_fir *hp);
static SAMPLE update_fir(hp_fir *hp, SAMPLE s);


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

        e->xf[i] = 1;
        e->xf[i+1] = 1;

        e->w[i] = 1.0/NLMS_LEN;
        e->w[i+1] = 1.0/NLMS_LEN;
    }

    e->hp = hp_fir_create();

    e->Fx = iir_create();
    e->Fe = iir_create();

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

    free(e);
}

/* This function is expected to update the samples in sb to remove echo - once
 * it complete, they are ready to go out the tx side of the hybrid */
void echo_update_tx(echo *e, SAMPLE_BLOCK *sb)
{
    size_t i;
    for (i=0; i<sb->count; i++)
    {
        SAMPLE rx, tx;

        rx = cbuffer_pop(e->rx_buf);

        /* High-pass filter - filter out sub-300Hz signals */
        tx = update_fir(e->hp, sb->s[i]);

        /* Geigel double-talk detector */
        int update = !dtd(e, tx, rx);

        /* if (!update) */
        /* { */
        /*     DEBUG_LOG("doubletalk\n") */
        /* } */
        /* else */
        /* { */
        /*     DEBUG_LOG(" ") */
        /* } */

        /* nlms-pw */
        tx = nlms_pw(e, tx, rx, update);

        /* If we're not talking, let's attenuate our signal */
        if (!update)
        {
            tx *= M12dB;
        }

        sb->s[i] = tx;
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
        /* DEBUG_LOG("a[i]: %f\ta[i+1]: %f\tb[i]: %f\tb[i+1]:\t%f\n", */
        /*     a[i], a[i+1], b[i], b[i+1]) */
        /* DEBUG_LOG("sum0: %f\tsum1: %f\n", sum0, sum1) */
    }
    return sum0+sum1;
}

static SAMPLE nlms_pw(echo *e, SAMPLE tx, SAMPLE rx, int update)
{
    float d = (float)tx;

    /* Shift samples down to make room for new ones. Almost certainly will have
     * to be sped up */
    memmove(e->x+1, e->x, (NLMS_LEN-1)*sizeof(float));
    memmove(e->xf+1, e->xf, (NLMS_LEN-1)*sizeof(float));

    e->x[0] = (float)rx;
    e->xf[0] = iir_highpass(e->Fx, d); /* pre-whitening of x */

    /* DEBUG_LOG("dotp e->w, e->x\n") */
    float err = d - dotp(e->w, e->x);
    float ef = iir_highpass(e->Fe, err); /* pre-whitening of err */

    /* DEBUG_LOG("x[0]: %f\txf[0]: %f\n", e->x[0], e->xf[0]) */

    /* TODO: we can update this iteratively for great justice */
    /* DEBUG_LOG("dotp e->xf, e->xf\n") */
    e->dotp_xf_xf = dotp(e->xf, e->xf);
    if (e->dotp_xf_xf == 0.0)
    {
        e->dotp_xf_xf = NLMS_LEN * MIN_XF * MIN_XF;
    }

    /* DEBUG_LOG("dotp_xf_xf: %f\n", e->dotp_xf_xf) */
    if (update)
    {
        float u_ef = STEPSIZE * ef / e->dotp_xf_xf;
        /* DEBUG_LOG("err: %f\tef: %f\tu_ef: %f\n", err, ef, u_ef) */

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

    /* TODO: this whole chain should probably be converted to use floats, so
     * move this up to the top level */
    if (err > MAXPCM)
    {
        return (int)MAXPCM;
    }
    else if (err < -MAXPCM)
    {
        return (int)-MAXPCM;
    }

    return (int)roundf(err);
}

/*********** DTD functions ***********/
static int dtd(echo *e, SAMPLE tx, SAMPLE rx)
{
    UNUSED(rx);

    float max = 0;
    size_t i;

    /* Get the last DTD_LEN rx samples and find the max*/
    /* TODO: can we just use e->x here? */
    SAMPLE_BLOCK *sb = cbuffer_peek_samples(e->rx_buf, NLMS_LEN);

    for (i=0; i<DTD_LEN; i++)
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

    SAMPLE a_tx = abs(tx);

    if (a_tx > (GeigelThreshold * max))
    {
        e->holdover = DTD_HOLDOVER;
    }

    if (e->holdover)
    {
        e->holdover--;
    }

    sample_block_destroy(sb);

    DEBUG_LOG("tx: %5d\trx: %5d\ta_tx: %5d\tmax:%5d\tdtd: %d\n",
        tx, rx, a_tx, (int)max, (e->holdover > 0))

    return e->holdover > 0;
}

/*********** High-pass FIR functions ***********/
static hp_fir *hp_fir_create(void)
{
    hp_fir *h = malloc(sizeof(hp_fir));
    /* 13-tap filter */
    h->z = calloc(HP_FIR_SIZE+1, sizeof(SAMPLE));

    return h;
}

void hp_fir_destroy(hp_fir *hp)
{
    free(hp->z);
    free(hp);
}

SAMPLE update_fir(hp_fir *hp, SAMPLE s)
{
    /* Shift the samples down to make room for the new one */
    memmove(hp->z+1, hp->z, HP_FIR_SIZE*sizeof(SAMPLE));

    hp->z[0] = s;

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
