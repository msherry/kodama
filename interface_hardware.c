#include <stdio.h>
#include <stdlib.h>

#include "cbuffer.h"
#include "hybrid.h"
#include "interface_hardware.h"
#include "kodama.h"
#include "portaudio.h"

/*********** Globals ***********/
/* Has portaudio been initialized? */
static int pa_initted = 0;
/* Only one set of speaker/mic per machine, so these are global */
static PaStream *in_stream, *out_stream;
/* Let portaudio choose how many frames it wants per buffer */
int gFramesPerBuffer = paFramesPerBufferUnspecified;


/*********** Static prototypes ***********/
static void init_portaudio(void);
static int handle_read( const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData );
static int handle_write( const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData );


/*********** Static functions ***********/
static void init_portaudio(void)
{
    PaError err = paNoError;

    if (pa_initted)
    {
        /* Already initted */
        return;
    }

    pa_initted = 1;
    err = Pa_Initialize();
    if (err != paNoError)
    {
        fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err));
        pa_initted = 0;
    }
}


void setup_hw_in(hybrid *h)
{
    PaError err = paNoError;

    init_portaudio();

    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();
    inputParameters.channelCount = NUM_CHANNELS;
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = \
        Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(&in_stream, &inputParameters, NULL, SAMPLE_RATE,
            gFramesPerBuffer,
            paClipOff,
            handle_read,
            h);

    /* err = Pa_OpenDefaultStream(&in_stream, NUM_CHANNELS, NUM_CHANNELS, */
    /*                            PA_SAMPLE_TYPE, SAMPLE_RATE, */
    /*                            /\* FRAMES_PER_BUFFER, *\/ */
    /*                            paFramesPerBufferUnspecified, */
    /*                            handle_read, hc); */
    if (err != paNoError) goto done;

    Pa_StartStream(in_stream);

done:
    if (err != paNoError)
    {
        fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err));
    }
}


/* Set up a stream to accept data from elsewhere and write it to the speaker. I
 * guess it can have a callback function too, but I'm not sure what it will be
 * used for */
void setup_hw_out(hybrid *h)
{
    PaError err = paNoError;

    init_portaudio();

    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = NUM_CHANNELS;
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = \
        Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(&out_stream, NULL, &outputParameters, SAMPLE_RATE,
            gFramesPerBuffer,
            paClipOff,
            handle_write,
            h);
    /* err = Pa_OpenDefaultStream(&out_stream, NUM_CHANNELS, NUM_CHANNELS, */
    /*                            PA_SAMPLE_TYPE, SAMPLE_RATE, */
    /*                            /\* FRAMES_PER_BUFFER, *\/ */
    /*                            paFramesPerBufferUnspecified, */
    /*                            handle_write, hc); */

    if (err != paNoError) goto done;
    Pa_StartStream(out_stream);

done:
    if (err != paNoError)
    {
        fprintf(stderr, "portaudio error: %s\n", Pa_GetErrorText(err));
    }
}

static int handle_read( const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData )
{
    hybrid *h = (hybrid *)userData;
    CBuffer *tx_buf = h->tx_buf;
    SAMPLE *rptr = (SAMPLE *)inputBuffer;

    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;

    /* DEBUG_LOG("handle_read (hardware): %li frames requested\n", framesPerBuffer); */

    unsigned int i;
    if( inputBuffer == NULL )
    {
        for( i=0; i<framesPerBuffer; i++ )
        {
            cbuffer_push(tx_buf, SAMPLE_SILENCE);   /* left */
            if( NUM_CHANNELS == 2 )
            {
                cbuffer_push(tx_buf, SAMPLE_SILENCE); /* right */
            }
        }
    }
    else
    {
        /* This may discard old data from buf if it hasn't been gotten to
         * yet. This is intentional */
        for( i=0; i<framesPerBuffer; i++)
        {
            SAMPLE s = *rptr++;
            cbuffer_push(tx_buf, s);  /* left */
            if( NUM_CHANNELS == 2 )
            {
                s = *rptr++;
                cbuffer_push(tx_buf, s);  /* right */
            }
        }
    }

    /* The buffer has new data in it - let's inform whoever cares about it */
    if (h->tx_cb_fn)
        (*h->tx_cb_fn)(h);

    return 0;
}


static int handle_write( const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData )
{
    hybrid *h = (hybrid *)userData;
    CBuffer *rx_buf = h->rx_buf;
    SAMPLE *wptr = (SAMPLE*)outputBuffer;
    unsigned int i;

    /* DEBUG_LOG("handle_write (hardware): %li frames requested\n", framesPerBuffer); */

    (void)inputBuffer;
    (void)timeInfo;
    (void)statusFlags;

    /* Currently, we're relying on the fact that if the cbuffer doesn't have
     * enough frames to satisfy our request, it will return zeroes. */
    for( i=0; i<framesPerBuffer; i++ )
    {
        SAMPLE s = cbuffer_pop(rx_buf); /* left */
        /* if (h->ec) */
        /*     s = echo_can_update(h->ec, cbuffer_pop(h->ec->cb), s); */
        *wptr++ = s;
        if( NUM_CHANNELS == 2 )
        {
            s = cbuffer_pop(rx_buf);   /* right */
            /* if (h->ec) */
            /*     s = echo_can_update(h->ec, cbuffer_pop(h->ec->cb), s); */
            *wptr++ = s;
        }
    }

    /* This is the end of the line for audio data - it's been dumped to the
     * hardware. No further callbacks possible */

    return 0;
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
