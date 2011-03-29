#ifndef _KODAMA_H_
#define _KODAMA_H_

#include <glib.h>
#include <stdio.h>
#include <stdint.h>

#if DEBUG
/* Yay C preprocessor */
#define DEBUG_LOG(...) {fprintf(stderr, __VA_ARGS__); fflush(stderr);}
#else
#define DEBUG_LOG() {}
#endif

#define VERBOSE_LOG(...) {if (globals.verbose) {fprintf(stderr, __VA_ARGS__); fflush(stderr);}}

#define FLV_LOG(...) {if (globals.flv_debug) {fprintf(stderr, __VA_ARGS__); fflush(stderr);}}

#define ERROR_LOG(...) {fprintf(stderr, __VA_ARGS__); fflush(stderr);}

#define NUM_CHANNELS (1)
#define PORTNUM (7650)

/// Doubletalk detection algorithms
typedef enum dtd_algo {
    geigel, mecc
} dtd_algo;

/* Select sample format. TODO: let's not make these constants, yes? */
#define PA_SAMPLE_TYPE  paInt16
typedef int16_t SAMPLE;
#define SAMPLE_SILENCE  (0)

/* Macro stolen from lighty for unused values */
#define UNUSED(x) ( (void)(x) )

void stack_trace(int die);

typedef struct globals_t {
    /** tx side */
    gchar *txhost;
    int tx_xmit_port;
    int tx_recv_port;

    /** rx side */
    gchar *rxhost;
    int rx_xmit_port;
    int rx_recv_port;

    /** Fake delay */
    int tx_delay_ms;
    int rx_delay_ms;

    /** rx-side echo cancellation */
    int echo_cancel;

    dtd_algo dtd;
    /** Dummy mode - reflect all messages back unchanged */
    int dummy;
    /** No threading mode - run in a single thread */
    int nothread;
    /** Number of milliseconds of echo to handle  */
    int echo_path;
    /** Sample rate to use for echo cancellation */
    int sample_rate;
    /** nlms length in taps (ms of echo path * TAPS_PER_MS) */
    int nlms_len;
    /** DTD hangover time, in taps - default is 30 ms. (ms * TAPS_PER_MS) */
    int dtd_hangover;

    /** Logging options */
    int verbose;
    int flv_debug;

    /** Imo flags */
    char *basename;
    int shardnum;
    char *fullname;
    gchar *server_host;         // Server machine and port are both set with the
    int server_port;            // "server" command-line option
} globals_t;

typedef struct stats_t {

    float cpu_mips;              /// MIPS per core - read-only, no lock required
    int num_cpus;
    float ec_per_core;           /// Estimate of how many ec we can run per core
    int num_threads;             /// How many threads we actually run

    uint64_t samples_processed;            /// Processed in the last minute
    uint64_t total_samples_processed;      /// Processed over server lifetime
    uint64_t total_us;                     /// Total time spent processing
} stats_t;


/* Convenience macros */

#endif
