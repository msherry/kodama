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

    hybrid_ptr h;
} echo;

echo *echo_create(hybrid_ptr h);
void echo_destroy(echo *e);
void echo_update(echo *e, hybrid_ptr h);

#endif
