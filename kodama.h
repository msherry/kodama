#ifndef _KODAMA_H_
#define _KODAMA_H_

#if DEBUG
/* Yay C preprocessor */
#include <stdio.h>              /* Just in case */
#define DEBUG_LOG(x, ...) {fprintf(stderr, x, ##__VA_ARGS__); fflush(stderr);}
#else
#define DEBUG_LOG(x) {}
#endif

#define NUM_CHANNELS (1)
#define SAMPLE_RATE  (8000)

#define PORTNUM (7650)

/* Select sample format. TODO: let's not make these constants, yes? */
#if 0
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

/* Macro stolen from lighty for unused values */
#define UNUSED(x) ( (void)(x) )

void stack_trace(int die);

#endif
