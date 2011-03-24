#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "calibrate.h"
#include "conversation.h"
#include "echo.h"
#include "kodama.h"
#include "util.h"

extern globals_t globals;
extern stats_t stats;
G_LOCK_EXTERN(stats);

char stream_name_0[] = "zfLQXH8Ts8DnfFt7:0";
char stream_name_1[] = "zfLQXH8Ts8DnfFt7:1";
unsigned char flv_packet_0[] = "\x08\x00\x00\x35\x00\x1A\x27\x00\x00\x00\x00\xB6\x2B\x42\x48\xD4\x16\x8C\xE2\x47\x04\x49\x9C\x01\x18\xC5\xDD\xA7\x16\x95\x38\xB6\xFD\xA2\x57\x8F\xEC\x75\xDA\xA1\x53\x11\xBC\xE9\x7E\x84\xA9\xC9\x20\x2A\x9C\x60\x17\xB3\x80\x3D\xAD\x62\x38\xA0\xC0\x03\x60\xB7\x00\x00\x00\x40";
unsigned char flv_packet_1[] = "\x08\x00\x00\x35\x00\x15\x6B\x00\x00\x00\x00\xB6\x2F\x5D\x8D\x15\x91\x89\x5F\x13\xB0\x9B\x73\xAB\x7A\xFC\xBB\x9A\x0D\xE5\xFC\xAC\x8F\x22\x27\xFB\x5C\x3F\x72\x25\x24\xD1\xB6\xF6\xF0\x2E\xCA\xC3\x9F\x6A\xF8\xD4\x62\xEB\x03\x25\x22\xB3\xB1\x63\x90\x41\xAC\x47\x00\x00\x00\x40";

/// Size of our calibration packets
#define FLV_PACKET_LEN (68)
/// Number of microseconds to calibrate for
#define CALIBRATE_TIME_US (2 * 1000000)

void calibrate(void)
{
    struct timeval start, end;
    uint64_t before_cycles, end_cycles;
    unsigned long d_us;

    /* Save global logging prefs, but disable as much as we can while
     * calibrating */
    int verbose = globals.verbose;
    globals.verbose = 0;
    int flv_debug = globals.flv_debug;
    globals.flv_debug = 0;

    int num_cpus = num_processors();
    g_debug("Num cpus: %i", num_cpus);
    g_debug("Sample rate: %d", SAMPLE_RATE);

    /* Verify that we get the values we expect */
    g_debug("Validating...");

    /* Validate dot product fn */
    float vec_a[NLMS_LEN], vec_b[NLMS_LEN];
    for (int i = 0; i< NLMS_LEN; i++)
    {
        float vals[] = {0.1, 0.2, 0.3};
        int len = sizeof(vals)/sizeof(vals[0]);
        vec_a[i] = vals[i%len];
        vec_b[i] = vals[(i+1)%len];
    }
    float correct_result;
    int temp;
    if (NLMS_LEN == 1600)       /* 8000 Hz */
    {
        temp = DOTP_1600;
    }
    else if (NLMS_LEN == 3200)  /* 16000 Hz */
    {
        temp = DOTP_3200;
    }
    else
    {
        g_error("Unable to determine correct dotp value for NLMS_LEN = %d", NLMS_LEN);
    }
    memcpy(&correct_result, &temp, sizeof(float));

    float dotp_result = dotp(vec_a, vec_b);

    /* Gcc warns about comparing float values, but trust me - it's ok here */
    if (correct_result != dotp_result)
    {
        g_error("dotp returned wrong value for NLMS of length %d: "
                "expected %.05f, got %.05f", NLMS_LEN, correct_result,
                dotp_result);
    }
    else
    {
        g_debug("dotp returned correct result for NLMS of length %d: "
                "   %.05f", NLMS_LEN, correct_result);
    }

    /* Find how many threads to run */
    g_debug("Calibrating...");
    conversation_start(stream_name_0);

    gettimeofday(&start, NULL);
    before_cycles = cycles();

    struct timeval t1, t2;
    do {
        unsigned char *flv_return_packet = NULL;
        int flv_return_len;

        r(stream_name_0, flv_packet_0, FLV_PACKET_LEN,
            &flv_return_packet, &flv_return_len);
        free(flv_return_packet);
        gettimeofday(&t1, NULL);
        r(stream_name_1, flv_packet_1, FLV_PACKET_LEN,
            &flv_return_packet, &flv_return_len);
        gettimeofday(&t2, NULL);
        free(flv_return_packet);

        gettimeofday(&end, NULL);
        d_us = delta(&start, &end);
    } while(d_us < CALIBRATE_TIME_US);
    end_cycles = cycles();


    float cpu_mips = (end_cycles - before_cycles) / (d_us);
    float secs_of_speech = (float)(stats.total_samples_processed) / SAMPLE_RATE;
    float mips_per_ec = cpu_mips / ((secs_of_speech*1E6)/d_us);
    float instances_per_core = cpu_mips/mips_per_ec;

    /* g_debug("Samples processed: %d", stats.total_samples_processed); */
    /* g_debug("Cycles taken: %lu", (end_cycles - before_cycles)); */
    /* g_debug("us taken: %u", d_us); */
    g_debug("CPU runs at %.02f MIPS", cpu_mips);
    g_debug("%.02f ms for %.02f ms of speech (%.02f MIPS/ec)",
        (d_us/1000.), secs_of_speech*1000, mips_per_ec);
    g_debug("%5.2f instances possible / core", instances_per_core);

    float max_instances = instances_per_core * num_cpus;
    g_debug("%5.2f total instances possible", max_instances);
    int num_threads =  max_instances * .8; /* Be conservative */
    num_threads = MAX(num_threads, 1);   /* Be pedantic */

    d_us = delta(&t1, &t2);
    g_debug("Last 20 ms of audio took %.03f ms", d_us/1000.);

    conversation_end(stream_name_0);

    /* Our caller will be responsible for resetting the resettable fields of
     * stats */
    G_LOCK(stats);
    stats.cpu_mips = cpu_mips;
    stats.num_cpus = num_cpus;
    stats.ec_per_core = instances_per_core;
    stats.num_threads = num_threads;
    G_UNLOCK(stats);

    globals.verbose = verbose;
    globals.flv_debug = flv_debug;
}
