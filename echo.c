#include <stdlib.h>
#include <string.h>

#include "echo.h"
#include "hybrid.h"
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
static int dtd(echo *e, SAMPLE tx, SAMPLE rx);
static void hp_fir_destroy(hp_fir *hp);
static SAMPLE update_fir(hp_fir *hp, SAMPLE s);


echo *echo_create(hybrid *h)
{
    echo *e = malloc(sizeof(echo));
    e->rx_buf = cbuffer_init(2000); /* TODO: */
    e->hp = hp_fir_create();

    e->h = h;
    return e;
}

void echo_destroy(echo *e)
{
    if (!e)
    {
        return;
    }

    cbuffer_destroy(e->rx_buf);
    hp_fir_destroy(e->hp);

    free(e);
}

void echo_update_tx(echo *e, hybrid *h, SAMPLE_BLOCK *sb)
{
    UNUSED(h);

    /* TODO: during non-doubletalk, this would be a good place to attenuate the
     * tx signal */

    size_t i;
    for (i=0; i<sb->count; i++)
    {
        SAMPLE rx, tx;

        rx = cbuffer_pop(e->rx_buf);

        /* High-pass filter - filter out sub-300Hz signals */
        tx = update_fir(e->hp, sb->s[i]);

        /* Geigel double-talk detector */
        int update = !dtd(e, tx, rx);

        /* TODO: nlms-pw */

        /* If we're not talking, let's attenuate our signal */
        if (update)
        {
            tx *= M12dB;
        }

        sb->s[i] = tx;
    }

}

/* This function is expected to update the samples in sb to remove echo - once
 * it complete, they are ready to go out the rx side of the hybrid */
void echo_update_rx(echo *e, hybrid *h, SAMPLE_BLOCK *sb)
{
    UNUSED(h);

    cbuffer_push_bulk(e->rx_buf, sb);
}

/*********** DTD functions ***********/
static int dtd(echo *e, SAMPLE tx, SAMPLE rx)
{
    UNUSED(rx);

    float max = 0;
    size_t i;

    /* Get the last DTD_LEN rx samples and find the max*/
    SAMPLE_BLOCK *sb = cbuffer_peek_samples(e->rx_buf, DTD_LEN);

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

    /* DEBUG_LOG("tx: %d\trx: %d\ta_tx: %d\tmax:\t%d\n", tx, rx, a_tx, (int)max) */

    if (a_tx > (GeigelThreshold * max))
    {
        e->holdover = DTD_HOLDOVER;
    }

    sample_block_destroy(sb);

    if (e->holdover)
    {
        e->holdover--;
    }

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
