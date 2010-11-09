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
static void hp_fir_destroy(hp_fir *hp);
static SAMPLE update_fir(hp_fir *hp, SAMPLE s);

echo *echo_create(hybrid *h)
{
    echo *e = malloc(sizeof(echo));
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
    hp_fir_destroy(e->hp);
    free(e);
}

void echo_update(echo *e, hybrid *h)
{
    /* Currently, we're always on the rx side */
    SAMPLE_BLOCK *sb = hybrid_get_rx_samples(h, 0);


    /* High-pass filter - filter out sub-300Hz signals */
    size_t i;
    for (i=0; i<sb->count; i++)
    {
        sb->s[i] = update_fir(e->hp, sb->s[i]);
    }


    /* Put our data back into the rx buffer */
    hybrid_put_rx_samples_direct(h, sb);

    sample_block_destroy(sb);
}

/*********** High-pass FIR functions ***********/
hp_fir *hp_fir_create(void)
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
