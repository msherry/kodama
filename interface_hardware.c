#include <stdio.h>
#include <stdlib.h>
#include "kodama.h"

#include "portaudio.h"
#include "interface_hardware.h"

static int pa_initted = 0;

/*********** Static functions ***********/
static void init_portaudio()
{
    PaError err = paNoError;

    if (pa_initted)
    {
        /* Already initted */
        return;
    }

    pa_initted = 1;

    err = Pa_Initialize();
    if (err != paNoError) goto done;

done:
    if (err != paNoError)
    {
        fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err));
        pa_initted = 0;
    }
}


/************** Public functions **************/
void list_hw_input_devices(void)
{
  int i;
  int defaultInput, defaultOutput;
  int numDevices;
  const PaDeviceInfo *deviceInfo;

  init_portaudio();

  defaultInput = Pa_GetDefaultInputDevice();
  defaultOutput = Pa_GetDefaultOutputDevice();

  numDevices = Pa_GetDeviceCount();

  for(i=0; i<numDevices; i++)
  {
    deviceInfo = Pa_GetDeviceInfo(i);

    fprintf(stderr, "%d\t%s ", i, deviceInfo->name);
    if (i == defaultInput)
      fprintf(stderr, "*");
    if (i == defaultOutput)
      fprintf(stderr, "%%");
    fprintf(stderr, "\n");
  }
}



/* /\* #define FRAMES_PER_BUFFER (1024) *\/ */

/* int gFramesPerBuffer = paFramesPerBufferUnspecified; */


/* /\* Prototypes *\/ */
/* static int handle_read( const void *inputBuffer, void *outputBuffer, */
/*                         unsigned long framesPerBuffer, */
/*                         const PaStreamCallbackTimeInfo* timeInfo, */
/*                         PaStreamCallbackFlags statusFlags, */
/*                         void *userData ); */
/* static int handle_write( const void *inputBuffer, void *outputBuffer, */
/*                            unsigned long framesPerBuffer, */
/*                            const PaStreamCallbackTimeInfo* timeInfo, */
/*                            PaStreamCallbackFlags statusFlags, */
/*                            void *userData ); */

/* /\* Globals *\/ */
/* static int pa_initted = 0; */
/* PaStream *in_stream, *out_stream; */

/* /\* Set up a stream to read from the microphone and fill a buffer (contained in */
/*  * the stream_context), then call whatever function is contained in sc (passing */
/*  * sc as an arg). *\/ */
/* void setup_hw_in(hybrid_context *hc) */
/* { */
/*   PaError err = paNoError; */

/*   init_portaudio(); */

/*   PaStreamParameters inputParameters; */
/*   inputParameters.device = Pa_GetDefaultInputDevice(); */
/*   inputParameters.channelCount = NUM_CHANNELS; */
/*   inputParameters.sampleFormat = PA_SAMPLE_TYPE; */
/*   inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency; */
/*   inputParameters.hostApiSpecificStreamInfo = NULL; */

/*   err = Pa_OpenStream(&in_stream, &inputParameters, NULL, SAMPLE_RATE, */
/*           gFramesPerBuffer, */
/*           paClipOff, */
/*           handle_read, */
/*           hc); */

/*   /\* err = Pa_OpenDefaultStream(&in_stream, NUM_CHANNELS, NUM_CHANNELS, *\/ */
/*   /\*                            PA_SAMPLE_TYPE, SAMPLE_RATE, *\/ */
/*   /\*                            /\\* FRAMES_PER_BUFFER, *\\/ *\/ */
/*   /\*                            paFramesPerBufferUnspecified, *\/ */
/*   /\*                            handle_read, hc); *\/ */
/*   if (err != paNoError) goto done; */

/*   Pa_StartStream(in_stream); */

/* done: */
/*   if (err != paNoError) */
/*   { */
/*     fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err)); */
/*   } */
/* } */

/* /\* Set up a stream to accept data from elsewhere and write it to the speaker. I */
/*  * guess it can have a callback function too, but I'm not sure what it will be */
/*  * used for *\/ */
/* void setup_hw_out(hybrid_context *hc) */
/* { */
/*   PaError err = paNoError; */

/*   init_portaudio(); */

/*   PaStreamParameters outputParameters; */
/*   outputParameters.device = Pa_GetDefaultOutputDevice(); */
/*   outputParameters.channelCount = NUM_CHANNELS; */
/*   outputParameters.sampleFormat = PA_SAMPLE_TYPE; */
/*   outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency; */
/*   outputParameters.hostApiSpecificStreamInfo = NULL; */

/*   err = Pa_OpenStream(&out_stream, NULL, &outputParameters, SAMPLE_RATE, */
/*           gFramesPerBuffer, */
/*           paClipOff, */
/*           handle_write, */
/*           hc); */
/*   /\* err = Pa_OpenDefaultStream(&out_stream, NUM_CHANNELS, NUM_CHANNELS, *\/ */
/*   /\*                            PA_SAMPLE_TYPE, SAMPLE_RATE, *\/ */
/*   /\*                            /\\* FRAMES_PER_BUFFER, *\\/ *\/ */
/*   /\*                            paFramesPerBufferUnspecified, *\/ */
/*   /\*                            handle_write, hc); *\/ */

/*   if (err != paNoError) goto done; */
/*   Pa_StartStream(out_stream); */

/* done: */
/*   if (err != paNoError) */
/*   { */
/*     fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err)); */
/*   } */
/* } */


/* static int handle_read( const void *inputBuffer, void *outputBuffer, */
/*                         unsigned long framesPerBuffer, */
/*                         const PaStreamCallbackTimeInfo* timeInfo, */
/*                         PaStreamCallbackFlags statusFlags, */
/*                         void *userData ) */
/* { */
/*   hybrid_context *hc = (hybrid_context *)userData; */
/*   CBuffer *tx_cb = hc->tx_cb; */
/*   SAMPLE *rptr = (SAMPLE *)inputBuffer; */

/*   (void) outputBuffer; /\* Prevent unused variable warnings. *\/ */
/*   (void) timeInfo; */
/*   (void) statusFlags; */

/*   DEBUG_LOG("handle_read (hardware): %li frames requested\n", framesPerBuffer); */

/*   unsigned int i; */
/*   if( inputBuffer == NULL ) */
/*   { */
/*     for( i=0; i<framesPerBuffer; i++ ) */
/*     { */
/*       cbuffer_push(tx_cb, SAMPLE_SILENCE);   /\* left *\/ */
/*       if( NUM_CHANNELS == 2 ) */
/*       { */
/*         cbuffer_push(tx_cb, SAMPLE_SILENCE); /\* right *\/ */
/*       } */
/*     } */
/*   } */
/*   else */
/*   { */
/*     /\* This may discard old data from cb if it hasn't been gotten to yet. This */
/*      * is intentional *\/ */
/*     for( i=0; i<framesPerBuffer; i++) */
/*     { */
/*       SAMPLE s = *rptr++; */
/*       cbuffer_push(tx_cb, s);  /\* left *\/ */
/*       if( NUM_CHANNELS == 2 ) */
/*       { */
/*         s = *rptr++; */
/*         cbuffer_push(tx_cb, s);  /\* right *\/ */
/*       } */
/*     } */
/*   } */

/*   /\* The buffer has new data in it - let's inform whoever cares about it *\/ */
/*   if (hc->tx_cb_fn) */
/*     (*hc->tx_cb_fn)(hc); */

/*   return 0; */
/* } */


/* static int handle_write( const void *inputBuffer, void *outputBuffer, */
/*                          unsigned long framesPerBuffer, */
/*                          const PaStreamCallbackTimeInfo* timeInfo, */
/*                          PaStreamCallbackFlags statusFlags, */
/*                          void *userData ) */
/* { */
/*   hybrid_context *hc = (hybrid_context *)userData; */
/*   CBuffer *rx_cb = hc->rx_cb; */
/*   SAMPLE *wptr = (SAMPLE*)outputBuffer; */
/*   unsigned int i; */

/*   DEBUG_LOG("handle_write (hardware): %li frames requested\n", framesPerBuffer); */

/*   (void)inputBuffer; */
/*   (void)timeInfo; */
/*   (void)statusFlags; */

/*   /\* Currently, we're relying on the fact that if the cbuffer doesn't have */
/*    * enough frames to satisfy our request, it will return zeroes. *\/ */
/*   for( i=0; i<framesPerBuffer; i++ ) */
/*   { */
/*     SAMPLE s = cbuffer_pop(rx_cb); /\* left *\/ */
/*     if (hc->ec) */
/*       s = echo_can_update(hc->ec, cbuffer_pop(hc->ec->cb), s); */
/*     *wptr++ = s; */
/*     if( NUM_CHANNELS == 2 ) */
/*     { */
/*       s = cbuffer_pop(rx_cb);   /\* right *\/ */
/*       if (hc->ec) */
/*         s = echo_can_update(hc->ec, cbuffer_pop(hc->ec->cb), s); */
/*       *wptr++ = s; */
/*     } */
/*   } */

/*   /\* This is the end of the line for audio data - it's been dumped to the */
/*    * hardware. No further callbacks possible *\/ */

/*   return 0; */
/* } */
