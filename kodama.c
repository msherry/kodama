#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hybrid.h"
#include "interface_hardware.h"
#include "kodama.h"

struct globals {
    gchar *xmit;
    gchar *recv;
} globals;

void usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [options]...\n", arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "-l: list hardware input devices\n");
    fprintf(stderr, "-h: this help\n");
    fprintf(stderr, "--------------\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "-x: host:port  Hostname and port to xmit to\n");
    fprintf(stderr, "-r: port       Portnum to listen on\n");
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
      globals.xmit = g_strdup_printf("%s", optarg);
      break;
    case 'r':
      globals.recv = g_strdup_printf("%s", optarg);
      break;
    case '?':
      fprintf(stderr, "Unknown option %c.\n", optopt);
      exit(1);
    }
  }
}

void shortcircuit_tx_to_rx(hybrid *h)
{
  fprintf(stderr, "shortcircuit_tx_to_rx\n");

  SAMPLE_BLOCK *sb = hybrid_get_tx_samples(h);

  hybrid_put_rx_samples(h, sb);

  sample_block_destroy(sb);

  if (h->rx_cb_fn)
    (*h->rx_cb_fn)(h);
}

int main(int argc, char *argv[])
{
  parse_command_line(argc, argv);

  hybrid *h = hybrid_new();
  /* Default callback fn - shortcircuit tx to rx */
  h->tx_cb_fn = shortcircuit_tx_to_rx;

  if (!globals.xmit)
  {
      setup_hw_out(h);
  }
  if (!globals.recv)
  {
      setup_hw_in(h);
  }

  GMainLoop *loop;
  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  return 0;
}
