#include <execinfo.h>
#include <fcntl.h>
#include <getopt.h>
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "av.h"
#include "calibrate.h"
#include "conversation.h"
#include "hybrid.h"
#include "interface_hardware.h"
#include "interface_tcp.h"
#include "interface_udp.h"
#include "kodama.h"
#include "protocol.h"

GMainLoop *loop;
globals_t globals;
stats_t stats;

G_LOCK_DEFINE(stats);

/* From interface_tcp */
extern int attempt_reconnect;

static void usage(char *arg0);
static void set_fullname(void);
static void calc_echo_globals(void);
static void parse_command_line(int argc, char **argv);
static void signal_handler(int signum);
static void init_sig_handlers(void);
static void init_log_handlers(void);
static void init_stats(void);
static void report_stats(void);
static gboolean trigger(gpointer data);

static void usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [options]...\n", arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "-d: list hardware input devices\n");
    fprintf(stderr, "-h: this help\n");
    fprintf(stderr, "--------------\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Standalone options:\n");
    fprintf(stderr, "-t: host   tx side: xmit data to host\n");
    fprintf(stderr, "-p: port   tx side: portnum to xmit to\n");
    fprintf(stderr, "-l: port   tx side: portnum to listen on\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-r: host   rx side: xmit data to host\n");
    fprintf(stderr, "-q: port   rx side: portnum to xmit to\n");
    fprintf(stderr, "-a: port   rx side: portnum to listen on\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-m: ms     tx-side number of milliseconds of delay to simulate\n");
    fprintf(stderr, "-n: ms     rx-side number of milliseconds of delay to simulate\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "--sample/-s: rate    Sampling rate for echo cancellation\n");
    fprintf(stderr, "--echopath path len: Echo path length in milliseconds\n");
    fprintf(stderr, "--dtd {geigel|mecc}: Which double-talk detector to use\n");
    fprintf(stderr, "--dummy:             Reflect all messages back to wowza unchanged\n");
    fprintf(stderr, "--nothread:          Run in single-threaded mode\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "IMO options:\n");
    fprintf(stderr, "--shard: shardnum of this shard (enables imo mode)\n");
    fprintf(stderr, "--server: <ip:port> Wowza server and port to connect to\n");
    fprintf(stderr, "--basename: Name of the service\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-e: set up rx-side echo cancellation\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-v:        verbose output\n");
    fprintf(stderr, "--flv:     FLV debugging output \n");

    exit(0);
}

static void set_fullname(void)
{
    if (globals.fullname)
    {
        /* Already set */
        return;
    }

    if (globals.shardnum == -1 || !globals.basename)
    {
        /* Don't have required params yet */
        return;
    }

    globals.fullname = g_strdup_printf("%s.%d", globals.basename, globals.shardnum);
    g_debug("Set fullname to %s", globals.fullname);
}

static void calc_echo_globals(void)
{
    /* Computed */
    globals.nlms_len = globals.echo_path * TAPS_PER_MS;
    globals.dtd_hangover = 30 * TAPS_PER_MS; /* TODO: make user-settable */
}

static void parse_command_line(int argc, char *argv[])
{
    globals.txhost = NULL;
    globals.tx_xmit_port = PORTNUM;
    globals.tx_recv_port = PORTNUM;

    globals.rxhost = NULL;
    globals.rx_xmit_port = 0;
    globals.rx_recv_port = 0;

    globals.tx_delay_ms = 0;
    globals.rx_delay_ms = 0;

    globals.echo_cancel = 0;

    globals.dtd = geigel;

    globals.echo_path = 200;    /* TODO: constants */
    globals.sample_rate = 16000;

    calc_echo_globals();

    globals.dummy = 0;
    globals.nothread = 0;

    /* TODO: verify that sample_rate is a multiple of 8000, dtd_len divides into
     * nlms_len, sample_rate divides by 1000 */

    globals.basename = NULL;
    globals.fullname = NULL;
    globals.shardnum = -1;
    globals.server_host = NULL;
    globals.server_port = -1;

    globals.verbose = 0;
    globals.flv_debug = 0;

    int c;

    opterr = 0;
    while (1)
    {
        int option_index = 0;
        static struct option long_options[] = {
            /* {name, has_arg, flag, val},  */
            {"shard", 1, 0, 0}, /* 0 */
            {"server", 1, 0, 0},
            {"basename", 1, 0, 0},
            {"dtd", 1, 0, 0},
            {"sample", 1, 0, 's'},
            {"echopath", 1, 0, 0},
            {"dummy", 0, 0, 0},
            {"nothread", 0, 0, 0},
            {"flv", 0, 0, 0},
            {"help", 0, 0, 'h'},
            {0, 0, 0, 0}
        };
        c = getopt_long(argc, argv, "ehdt:r:p:l:q:a:m:n:v", long_options,
            &option_index);

        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case 'd':
            list_hw_input_devices();
            exit(0);
            break;
        case 0:
            if (!strcmp("shard", long_options[option_index].name))
            {
                int shardnum = atoi(optarg);
                globals.shardnum = shardnum;

                set_fullname();
            }
            else if (!strcmp("server", long_options[option_index].name))
            {
                gchar **host_and_port = g_strsplit(optarg, ":", 2);

                /* TODO: error checking would be great here, but let's just not
                 * call this program with bad options, no? */
                globals.server_host = g_strdup(host_and_port[0]);
                globals.server_port = atoi(host_and_port[1]);

                g_strfreev(host_and_port);
            }
            else if (!strcmp("basename", long_options[option_index].name))
            {
                globals.basename = g_strdup_printf("%s", optarg);

                set_fullname();
            }
            else if (!strcmp("flv", long_options[option_index].name))
            {
                globals.flv_debug = 1;
            }
            else if (!strcmp("dtd", long_options[option_index].name))
            {
                if (!strcmp("geigel", optarg))
                {
                    globals.dtd = geigel;
                }
                else if (!strcmp("mecc", optarg))
                {
                    globals.dtd = mecc;
                }
                else
                {
                    fprintf(stderr, "Unknown DTD algorithm %s\n", optarg);
                    usage(argv[0]);
                    exit(0);
                }
            }
            else if (!strcmp("dummy", long_options[option_index].name))
            {
                globals.dummy = 1;
            }
            else if (!strcmp("nothread", long_options[option_index].name))
            {
                globals.nothread = 1;
            }
            else if (!strcmp("echopath", long_options[option_index].name))
            {
                globals.echo_path = atoi(optarg);
                calc_echo_globals();
            }
           break;
        case 'e':
            globals.echo_cancel = 1;
            break;
        case 'v':
            globals.verbose = 1;
            break;
        case 't':
            globals.txhost = g_strdup_printf("%s", optarg);
            break;
        case 'r':
            globals.rxhost = g_strdup_printf("%s", optarg);
            break;
        case 'p':
            globals.tx_xmit_port = atoi(optarg);
            break;
        case 'l':
            globals.tx_recv_port = atoi(optarg);
            break;
        case 'q':
            globals.rx_xmit_port = atoi(optarg);
            break;
        case 'a':
            globals.rx_recv_port = atoi(optarg);
            break;
        case 'm':
            globals.tx_delay_ms = atoi(optarg);
            break;
        case 'n':
            globals.rx_delay_ms = atoi(optarg);
            break;
        case 's':
            globals.sample_rate = atoi(optarg);
            calc_echo_globals();
            break;
        case '?':
            fprintf(stderr, "Unknown option '%c'\n", optopt);
            exit(1);
        default:
            DEBUG_LOG("?? getopt_long returned character code 0%o\n", c);
        }
    }
}

static void init_sig_handlers(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
}

static void signal_handler(int signum)
{
    switch(signum)
    {
    case SIGHUP:
        g_message("Got SIGHUP - rotating logs");
        init_log_handlers();
        signal(SIGHUP, signal_handler);
        break;
    case SIGTERM:
        g_message("Got SIGTERM - shutting down");
        /* TODO: make this more graceful */
        /* TODO: stop all threads */
        exit_all_threads();
        g_main_loop_quit(loop);
        break;
    case SIGINT:
        g_message("Got SIGINT - shutting down");
        g_main_loop_quit(loop);
        break;
    }
}

/* Redirect stdout and stderr to log file */
static void init_log_handlers(void)
{
    char filename[256];
    char *dir;

    close(1);
    memset(filename, 0, 256);
    dir = getenv("IMO_LOG_DIR");
    if (!dir)
    {
        dir = "/tmp";
    }
    strcat(filename, dir);
    strcat(filename, "/");
    /* Hack - if we don't have a fullname at this point, make one up */
    if (!globals.fullname)
    {
        globals.fullname = g_strdup_printf("%s.%d", "Kodama", 0);
    }
    strcat(filename, globals.fullname);
    strcat(filename, ".log");
    open(filename, O_CREAT|O_WRONLY|O_APPEND, 0644);
    dup2(1, 2);
}

static void init_stats(void)
{
    G_LOCK(stats);
    stats.samples_processed = 0;
    stats.total_samples_processed = 0;
    stats.total_us = 0;
    G_UNLOCK(stats);
}

static void report_stats(void)
{
    G_LOCK(stats);

    if (stats.samples_processed)
    {
        g_debug("*** Stats dump ***");
        g_debug("Samples processed: %llu (%.02f s)",
            stats.samples_processed,
            (float)stats.samples_processed/globals.sample_rate);
    }

    stats.samples_processed = 0;
    G_UNLOCK(stats);
}

static gboolean trigger(gpointer data)
{
    static unsigned int count = 0;

    UNUSED(data);

    count++;

    if (attempt_reconnect && ((count % 3) == 0))
    {
        g_debug("Attempting to reconnect");
        tcp_connect();
    }

    if ((count % 60) == 0)
    {
        report_stats();
    }

    /* Return FALSE if this function should be removed */
    return TRUE;
}

int main(int argc, char *argv[])
{
    /* Needed for thread/mutex support */
    g_thread_init(NULL);

    parse_command_line(argc, argv);

    init_stats();
    init_hybrids();
    init_log_handlers();
    init_sig_handlers();
    init_av();
    init_conversations();

    calibrate();                /* Determine how many threads we can run */
    init_protocol();            /* Create the work queue and threads */
    init_stats();               /* Clear out the calibration values */

    /* If no shardnum is given, we're running in standalone mode */
    if (globals.shardnum == -1)
    {
        hybrid *h = get_hybrid("default");
        h->tx_cb_fn = shortcircuit_tx_to_rx;
        hybrid_simulate_tx_delay(h, globals.tx_delay_ms);
        hybrid_simulate_rx_delay(h, globals.rx_delay_ms);

        if (globals.echo_cancel)
        {
            hybrid_setup_echo_cancel(h);
        }

        if (globals.txhost)
        {
            setup_udp_network_xmit(h, globals.txhost, globals.tx_xmit_port,
                    tx_side);
            // Yes, we receive on the tx side. Trust me
            setup_udp_network_recv(h, globals.tx_recv_port, tx_side);
        }

        if (globals.rxhost)
        {
            setup_udp_network_recv(h, globals.rx_recv_port, rx_side);
            setup_udp_network_xmit(h, globals.rxhost, globals.rx_xmit_port,
                    rx_side);
        }
        else
        {
            /* By default, the rx side is hooked up to the microphone and
             * speaker */
            setup_hw_in(h);
            setup_hw_out(h);
        }
    }
    else
    {
        /* We're in imo mode - connect to a wowza and echo cancel data from
         * it */
        setup_tcp_connection(globals.server_host, globals.server_port);
    }

    /* Set up a trigger function to run approximately every second */
    g_timeout_add_seconds(1, trigger, NULL);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    return 0;
}


void stack_trace(int die)
{
    void *array[10];
    size_t size, i;
    char **strings;

    size = backtrace(array, 10);
    strings = backtrace_symbols(array, size);

    for(i = 1; i<size; i++)     /* skip the trace for our own function */
    {
        fprintf(stderr, "%s\n", strings[i]);
    }

    free(strings);

    if (die)
    {
        exit(-1);
    }
}
