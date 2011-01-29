#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "echo.h"
#include "hybrid.h"
#include "iir.h"
#include "kodama.h"
#include "util.h"

extern globals_t globals;

/* Much of this is from http://www.andreadrian.de/echo_cancel, with credit also
 * to the OSLEC project */

/**
Kaiser Window FIR Filter.\n
source:
http://www.dsptutor.freeuk.com/KaiserFilterDesign/KaiserFilterDesign.html \n

Filter type: High pass\n
Passband: 300.0 - 4000.0 Hz\n
Order: 12\n
Transition band: 100.0 Hz\n
Stopband attenuation: 10.0 dB
*/
const float HP_FIR[] = {-0.043183226, -0.046636667, -0.049576525, -0.051936015,
                  -0.053661242, -0.054712527, 0.82598513, -0.054712527,
                  -0.053661242, -0.051936015, -0.049576525, -0.046636667,
                  -0.043183226};

/** Number of taps for high-pass (300Hz+) filter */
#define HP_FIR_SIZE (13)

/********* Static functions *********/
static hp_fir *hp_fir_create(void);
static inline float clip(float in);
static float nlms_pw(echo *e, float tx, float rx, int update);
static int dtd(echo *e, float tx, float rx);
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

    e->max_x = malloc((NLMS_LEN/DTD_LEN) * sizeof(float));
    memset(e->max_x, 0, (NLMS_LEN/DTD_LEN) * sizeof(float));
    e->max_max_x = 0.0;
    e->dtd_index = 0;
    e->dtd_count = 0;

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

    free(e->max_x);

    cbuffer_destroy(e->rx_buf);

    hp_fir_destroy(e->hp);
    iir_destroy(e->Fx);
    iir_destroy(e->Fe);
    iirdc_destroy(e->iir_dc);

    free(e);
}

void echo_update_tx(echo *e, SAMPLE_BLOCK *sb)
{
    VERBOSE_LOG("%s\n", e->h->name);

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

        /* Speaker high-pass filter - remove DC */
        rx = iirdc_highpass(e->iir_dc, rx);

        /* Geigel double-talk detector */
        int update = !dtd(e, tx, rx);

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

        /* HACK: I'd rather diverge for a bit than have that horrible
         * static. Find out why we get such bad data sometimes */
        if (fabsf(tx)+10 > MAXPCM)
        {
            /* Wipe all the weights. Brutal. */
            memset(e->w, 0, (NLMS_LEN*sizeof(float)));

            g_debug("Orig: %i  clipped: %f", tx_s, tx);
        }

        sb->s[i] = (int)tx;
    }

    VERBOSE_LOG("dotp(xf, xf): %f\n", e->dotp_xf_xf);
    VERBOSE_LOG("%s\n", any_doubletalk ? "doubletalk" : "no doubletalk");
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
        g_debug("Clipping high");
    }
    else if (in < -MAXPCM)
    {
        out = -MAXPCM;
        g_debug("Clipping low");
    }
    else
    {
        out = roundf(in);
    }
    return out;
}

/*********** NLMS functions ***********/

/* -funroll-loops doesn't help, but does look crazy */
/* So far, hand-written asm with a 32-byte stride is fastest */
__attribute__ ((noinline))
static float dotp(const float * restrict a, const float * restrict b)
{
    float sum = 0.0;

#ifdef ASM_DOTP
    int *i = 0;
    __asm__ volatile(
"1:                                       \n\t"
        "movlps (%2,%0), %%xmm1           \n\t"
        "movhps 8(%2,%0), %%xmm1          \n\t"
        "movlps (%3,%0), %%xmm2           \n\t"
        "movhps 8(%3,%0), %%xmm2          \n\t"
        "movlps 16(%2,%0), %%xmm3         \n\t"
        "movhps 24(%2,%0), %%xmm3         \n\t"
        "movlps 16(%3,%0), %%xmm4         \n\t"
        "movhps 24(%3,%0), %%xmm4         \n\t"
        "addq   $32, %0                   \n\t"
        "cmpq   %4, %0                    \n\t"
        "mulps  %%xmm2, %%xmm1            \n\t"
        "addps  %%xmm1, %1                \n\t"
        "mulps  %%xmm4, %%xmm3            \n\t"
        "addps  %%xmm3, %1                \n\t"
        "jne    1b                        \n\t"
        "haddps %1, %1                    \n\t"
        "haddps %1, %1                    \n\t"

        :"+r"(i), "+x"(sum)
        :"r"(a), "r"(b), "n"(NLMS_LEN*sizeof(float))
        :"%xmm1", "%xmm2", "%xmm3", "%xmm4"
    );

#else
    for (int i=0; i<NLMS_LEN; i++)
    {
        sum += a[i] * b[i];
    }
#endif

    return sum;
}

static float nlms_pw(echo *e, float tx, float rx, int update)
{
    char *hex;
    int j = e->j;

    e->x[j] = rx;
    e->xf[j] = iir_highpass(e->Fx, rx); /* pre-whitening of x */

    float dotp_w_x = dotp(e->w, e->x+j);
    float err = tx - dotp_w_x;
    float ef = iir_highpass(e->Fe, err); /* pre-whitening of err */
    if (isnan(ef))
    {
        DEBUG_LOG("%s\n", "ef went NaN");
        DEBUG_LOG("err: %f\n", err);
        DEBUG_LOG("dotp_w_x: %f\n", dotp_w_x);
        hex = floats_to_text(e->w, NLMS_LEN);
        DEBUG_LOG("w: %s\n", hex);
        free(hex);
        hex = floats_to_text(e->x+j, NLMS_LEN);
        DEBUG_LOG("x: %s\n", hex);
        free(hex);
        stack_trace(1);
    }

#ifdef FAST_DOTP
    /* Iterative update */
    e->dotp_xf_xf += (e->xf[j] * e->xf[j] -
        e->xf[j+NLMS_LEN-1] * e->xf[j+NLMS_LEN-1]);
#else
    /* The slow way to do this */
    e->dotp_xf_xf = dotp(e->xf, e->xf);
#endif

    /* TODO: find a reasonable value for this */
    e->dotp_xf_xf = MAX(e->dotp_xf_xf, 0.1);

    if (update)
    {
        float u_ef = STEPSIZE * ef / e->dotp_xf_xf;
        if (isinf(u_ef))
        {
            DEBUG_LOG("%s\n", "u_ef went infinite");
            DEBUG_LOG("ef: %f\tdotp_xf_xf: %f\n", ef, e->dotp_xf_xf);
            DEBUG_LOG("dotp_w_x: %f\terr: %f\n", dotp_w_x, err);
            DEBUG_LOG("STEPSIZE: %f\n", STEPSIZE);
            hex = floats_to_text(e->w, NLMS_LEN);
            DEBUG_LOG("e->w: %s\n", hex);
            free(hex);
            hex = floats_to_text(e->x+j, NLMS_LEN);
            DEBUG_LOG("e->x+j: %s\n", hex);
            free(hex);
            /* stack_trace(1); */

            /* TODO: / HACK: for now, reset the weights to zero */
            memset(e->w, 0, (NLMS_LEN)*sizeof(float));
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

#ifdef FAST_DTD
static int dtd(echo *e, float tx, float rx)
{
    /* Get the last NLMS_LEN rx samples and find the max*/
    size_t i;

    float a_rx = fabsf(rx);

    if (a_rx > e->max_x[e->dtd_index])
    {
        e->max_x[e->dtd_index] = a_rx;
        if (a_rx > e->max_max_x)
        {
            e->max_max_x = a_rx;
        }
    }

    /* Do we have a new chunk of samples to summarize? */
    if (++e->dtd_count >= DTD_LEN)
    {
        e->dtd_count = 0;
        /* Find max of max */
        e->max_max_x = 0;
        for (i = 0; i< NLMS_LEN/DTD_LEN; i++)
        {
            if (e->max_x[i] > e->max_max_x)
            {
                e->max_max_x = e->max_x[i];
            }
        }
        /* Rotate */
        if (++e->dtd_index >= NLMS_LEN/DTD_LEN)
        {
            e->dtd_index = 0;
        }
        e->max_x[e->dtd_index] = 0.0; /* This will be set next time */
    }

    if (fabsf(tx) > (GeigelThreshold * e->max_max_x))
    {
        e->holdover = DTD_HOLDOVER;
    }

    if (e->holdover)
    {
        e->holdover--;
    }

    /* VERBOSE_LOG("tx: %5d\ta_tx: %5d\tmax:%5d\tdtd: %d\n", */
    /*     (int)tx, (int)a_tx, (int)max, (e->holdover > 0)) */

    return e->holdover > 0;
}

#else
static int dtd(echo *e, float tx, float rx_unused)
{
    UNUSED(rx_unused);          /* Just here to make the signatures match */

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

    /* VERBOSE_LOG("tx: %5d\ta_tx: %5d\tmax:%5d\tdtd: %d\n", */
    /*     (int)tx, (int)a_tx, (int)max, (e->holdover > 0)) */

    return e->holdover > 0;
}
#endif

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
