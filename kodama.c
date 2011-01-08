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
#include "hybrid.h"
#include "interface_hardware.h"
#include "interface_tcp.h"
#include "interface_udp.h"
#include "kodama.h"

GMainLoop *loop;
globals_t globals;

/* From interface_tcp */
extern int attempt_reconnect;

static void usage(char *arg0);
static void set_fullname(void);
static void parse_command_line(int argc, char **argv);
static void signal_handler(int signum);
static void init_sig_handlers(void);
static void init_log_handlers(void);
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

    globals.basename = NULL;
    globals.fullname = NULL;
    globals.shardnum = -1;
    globals.server_host = NULL;
    globals.server_port = -1;

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
           break;
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case 'd':
            list_hw_input_devices();
            exit(0);
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
    strcat(filename, globals.fullname);
    strcat(filename, ".log");
    open(filename, O_CREAT|O_WRONLY|O_APPEND, 0644);
    dup2(1, 2);
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

    /* Return FALSE if this function should be removed */
    return TRUE;
}

int main(int argc, char *argv[])
{
    parse_command_line(argc, argv);

    init_hybrids();
    init_log_handlers();
    init_sig_handlers();
    init_av();

    /* If no shardnum is given, we're running in standalone mode */
    if (globals.shardnum == -1)
    {
        hybrid *h = get_hybrid("default");
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

    /* Needed for thread/mutex support */
    g_thread_init(NULL);

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
