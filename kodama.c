#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hybrid.h"
#include "interface_hardware.h"
#include "interface_network.h"
#include "kodama.h"

struct globals {
    /* tx side */
    gchar *txhost;
    int tx_xmit_port;
    int tx_recv_port;

    /* rx side */
    gchar *rxhost;
    int rx_xmit_port;
    int rx_recv_port;

    /* Fake delay */
    int tx_delay_ms;
    int rx_delay_ms;
} globals;

GMainLoop *loop;

void signal_handler(int signum);

void usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [options]...\n", arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "-d: list hardware input devices\n");
    fprintf(stderr, "-h: this help\n");
    fprintf(stderr, "--------------\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-t: host   tx side: xmit data to host\n");
    fprintf(stderr, "-p: port   tx side: portnum to xmit to\n");
    fprintf(stderr, "-l: port   tx side: portnum to listen on\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-r: host   rx side: xmit data to host\n");
    fprintf(stderr, "-q: port   rx side: portnum to xmit to\n");
    fprintf(stderr, "-a: port   rx side: portnum to listen on\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-m: ms     number of milliseconds of delay to simulate\n");
}

void parse_command_line(int argc, char *argv[])
{
    globals.txhost = NULL;
    globals.tx_xmit_port = PORTNUM;
    globals.tx_recv_port = PORTNUM;

    globals.rxhost = NULL;
    globals.rx_xmit_port = 0;
    globals.rx_recv_port = 0;

    globals.tx_delay_ms = 0;
    globals.rx_delay_ms = 0;

    int c;

    /* TODO: keeping these straight is a nightmare. Use long options */

    opterr = 0;
    while ((c = getopt(argc, argv, "hdt:r:p:l:q:a:m:n:")) != -1)
    {
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
            fprintf(stderr, "Unknown option %c.\n", optopt);
            exit(1);
        }
    }
}

void init_sig_handlers(void)
{
    signal(SIGINT, signal_handler);
}

void signal_handler(int signum)
{
    switch(signum)
    {
    case SIGINT:
        g_main_loop_quit(loop);
        break;
    }
}

int main(int argc, char *argv[])
{
    parse_command_line(argc, argv);

    init_sig_handlers();

    hybrid *h = hybrid_new();
    hybrid_simulate_tx_delay(h, globals.tx_delay_ms);
    hybrid_simulate_rx_delay(h, globals.rx_delay_ms);
    hybrid_setup_echo_cancel(h);

    if (globals.txhost)
    {
        setup_network_xmit(h, globals.txhost, globals.tx_xmit_port, tx);
        // Yes, we receive on the tx side. Trust me
        setup_network_recv(h, globals.tx_recv_port, tx);
    }

    if (globals.rxhost)
    {
        setup_network_recv(h, globals.rx_recv_port, rx);
        setup_network_xmit(h, globals.rxhost, globals.rx_xmit_port, rx);
    }
    else
    {
        /* By default, the rx side is hooked up to the microphone and speaker */
        setup_hw_in(h);
        setup_hw_out(h);
    }

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    return 0;
}
