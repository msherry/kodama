#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hybrid.h"
#include "interface_hardware.h"
#include "kodama.h"

struct globals {
    int i;
} globals;

void usage(char *arg0)
{
    fprintf(stderr, "Usage: %s [options]...\n", arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "-l: list hardware input devices\n");
    fprintf(stderr, "-h: this help\n");
}

void parse_command_line(int argc, char *argv[])
{
  int c;

  globals.i = 0;

  opterr = 0;
  while ((c = getopt(argc, argv, "hl")) != -1)
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
    /* case 'i': */
    /*   globals.in_file = g_strdup_printf("%s", optarg); */
    /*   break; */
    case '?':
      fprintf(stderr, "Unknown option %c.\n", optopt);
      exit(1);
    }
  }
}

void shortcircuit_tx_to_rx(hybrid *h)
{
  fprintf(stderr, "shortcircuit_tx_to_rx\n");

  CBuffer *tx_buf = h->tx_buf;
  CBuffer *rx_buf = h->rx_buf;

  while (cbuffer_get_count(tx_buf) > 0)
  {
    SAMPLE s = cbuffer_pop(tx_buf);
    cbuffer_push(rx_buf, s);
  }

  if (h->rx_cb_fn)
    (*h->rx_cb_fn)(h);
}

int main(int argc, char *argv[])
{
  parse_command_line(argc, argv);

  hybrid *h = calloc(1, sizeof(hybrid));
  {
      h->tx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);
      h->rx_buf = cbuffer_init(1000 * SAMPLE_RATE * NUM_CHANNELS);

      h->tx_count = 0;
      h->rx_count = 0;

      /* Default callback fn - shortcircuit tx to rx */
      h->tx_cb_fn = shortcircuit_tx_to_rx;

      /* Dummy initial data to simulate delay */
      float NUM_SECONDS = 0;
      int i;
      for (i=0; i<NUM_SECONDS * SAMPLE_RATE * NUM_CHANNELS; i++)
      {
          printf("%d\n", i);
          cbuffer_push(h->rx_buf, SAMPLE_SILENCE);
      }
  }

  setup_hw_in(h);
  setup_hw_out(h);

  GMainLoop *loop;
  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  return 0;
}
