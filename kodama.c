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
    gchar *host;
} globals;

GMainLoop *loop;

void signal_handler(int signum);

void usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [options]...\n", arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "-l: list hardware input devices\n");
    fprintf(stderr, "-h: this help\n");
    fprintf(stderr, "--------------\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-x: host    Xmit data to host instead of shortcircuiting\n");
}

void parse_command_line(int argc, char *argv[])
{
    int c;

    opterr = 0;
    while ((c = getopt(argc, argv, "hlx:r:")) != -1)
    {
        switch (c)
        {
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case 'l':
            list_hw_input_devices();
            exit(0);
            break;
        case 'x':
            globals.host = g_strdup_printf("%s", optarg);
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

    if (globals.host)
    {
        setup_network_xmit(h, globals.host, tx);
        setup_network_recv(h, tx); // Yes, we receive on the tx side. Trust me
    }

    setup_hw_in(h);
    setup_hw_out(h);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    return 0;
}
