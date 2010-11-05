#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

int main(int argc, char *argv[])
{
  parse_command_line(argc, argv);

  return 0;
}
