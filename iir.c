#include <stdlib.h>

#include "iir.h"

/*
Chebyshev IIR filter

Filter type: HP
Passband: 3700 - 4000 Hz
Passband ripple: 1.5 dB
Order: 1

Coefficients

a[0] = 0.105831884      b[0] = 1.0
a[1] = -0.105831884     b[1] = 0.78833646
*/

const float a0 = 0.105831884f;
const float a1 = -0.105831884f;
const float b1 = 0.78833646;

IIR *iir_create(void)
{
    IIR *ir = malloc(sizeof(IIR));
    ir->x = 0.0;
    ir->y = 0.0;

    return ir;
}

float iir_highpass(IIR *ir, float in)
{
    float out = a0 * in + a1 * ir->x + b1 * ir->y;

    ir->x = in;
    ir->y = out;

    return out;
}

void iir_destroy(IIR *ir)
{
    if (!ir)
    {
        return;
    }

    free(ir);
}
