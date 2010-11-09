#ifndef _ECHO_H_
#define _ECHO_H_

#include "kodama.h"

/* Ridiculous forward declaration */
typedef struct hybrid *hybrid_ptr;

typedef struct hp_fir {
    SAMPLE *z;
} hp_fir;

#define HP_FIR_SIZE (13)

typedef struct echo {
    hp_fir *hp;
} echo;

echo *echo_create(void);
void echo_destroy(echo *e);
void echo_update(hybrid_ptr h);

#endif
