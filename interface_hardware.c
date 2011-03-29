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

extern globals_t globals;

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

    err = Pa_OpenStream(&in_stream, &inputParameters, NULL, globals.sample_rate,
            gFramesPerBuffer,
            paClipOff,
            handle_read,
            h);

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
    /* TODO: when the hybrid is destroyed, we need to remove this stream
     * attached to it */

    PaError err = paNoError;

    init_portaudio();

    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = NUM_CHANNELS;
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = \
        Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(&out_stream, NULL, &outputParameters,
            globals.sample_rate,
            gFramesPerBuffer,
            paClipOff,
            handle_write,
            h);

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
    const SAMPLE *rptr = (const SAMPLE *)inputBuffer;

    UNUSED(outputBuffer);
    UNUSED(timeInfo);
    UNUSED(statusFlags);

    /* DEBUG_LOG("handle_read (hardware): %li frames requested\n", framesPerBuffer); */

    size_t num_samples_needed = framesPerBuffer * NUM_CHANNELS;
    SAMPLE_BLOCK *sb = sample_block_create(num_samples_needed);

    SAMPLE *s = sb->s;
    if( inputBuffer == NULL )
    {
        /* Push some silence */
        while(num_samples_needed--)
        {
            *s++ = SAMPLE_SILENCE;
        }
    }
    else
    {
        /* This may discard old data from the hybrid's tx_buf if it hasn't been
         * gotten to yet. This is intentional */
        while(num_samples_needed--)
        {
            *s++ = *rptr++;
        }
    }

    hybrid_put_tx_samples(h, sb);
    sample_block_destroy(sb);

    return 0;
}


static int handle_write( const void *inputBuffer, void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData )
{
    hybrid *h = (hybrid *)userData;
    SAMPLE *wptr = (SAMPLE*)outputBuffer;

    /* DEBUG_LOG("handle_write (hardware): %li frames requested\n", framesPerBuffer); */

    UNUSED(inputBuffer);
    UNUSED(timeInfo);
    UNUSED(statusFlags);

    /* Currently, we're relying on the fact that if the cbuffer doesn't have
     * enough frames to satisfy our request, it will return silence. */
    size_t num_samples_needed = framesPerBuffer * NUM_CHANNELS;
    SAMPLE_BLOCK *sb = hybrid_get_rx_samples(h, num_samples_needed);
    SAMPLE *s = sb->s;
    size_t copied = 0;
    while (copied++ < num_samples_needed)
    {
        *wptr++ = *s++;
    }

    sample_block_destroy(sb);

    /* This is the end of the line for audio data - it's been dumped to the
     * hardware (outputBuffer). No further callbacks possible */

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
