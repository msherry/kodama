#ifndef _IIR_H_
#define _IIR_H_

typedef struct IIR {
    float x,y;
} IIR;

IIR *iir_create(void);
void iir_destroy(IIR *);

float iir_highpass(IIR *ir, float in);

#endif
