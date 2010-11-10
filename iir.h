#ifndef _IIR_H_
#define _IIR_H_

typedef struct IIR {
    float x,y;
} IIR;

typedef struct IIR_DC {
    float x;
} IIR_DC;

IIR *iir_create(void);
void iir_destroy(IIR *);

float iir_highpass(IIR *ir, float in);

IIR_DC *iirdc_create(void);
void iirdc_destroy(IIR_DC *);

float iirdc_highpass(IIR_DC *ir, float in);

#endif
