#include <glib.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "kodama.h"
#include "util.h"

char *hexify(const unsigned char *buf, const int data_len)
{
    char *ret = malloc(data_len*2 + 1);
    char *ret_off = ret;

    int offset = 0;
    while(offset < data_len)
    {
        sprintf(ret_off, "%.2X", buf[offset]);
        offset++;
        ret_off += 2;
    }

    ret[data_len*2] = '\0';

    return ret;
}

/* This just assumes that samples are 16 bits - we seem unlikely to change
 * this */
/* Returns a string of the format "(num samples) s1 s2 ... */
char *samples_to_text(const SAMPLE *samples, const int num_samples)
{
    char *ret;
    gchar **sample_strings = malloc((num_samples+2) * sizeof(gchar *));

    sample_strings[0] = g_strdup_printf("(%d)", num_samples);
    sample_strings[num_samples] = 0;

    int i;
    for(i = 1; i < num_samples; i++)
    {
        sample_strings[i] = g_strdup_printf("%d", samples[i]);
    }

    ret = g_strjoinv(" ", sample_strings);

    for(i = 0; i < num_samples; i++)
    {
        g_free(sample_strings[i]);
    }

    return ret;
}

char *floats_to_text(const float *samples, const int num_samples)
{
    char *ret;
    gchar **sample_strings = malloc((num_samples+2) * sizeof(gchar *));

    sample_strings[0] = g_strdup_printf("(%d)", num_samples);
    sample_strings[num_samples] = 0;

    int i;
    for(i = 1; i < num_samples; i++)
    {
        sample_strings[i] = g_strdup_printf("%f", samples[i]);
    }

    ret = g_strjoinv(" ", sample_strings);

    for(i = 0; i < num_samples; i++)
    {
        g_free(sample_strings[i]);
    }

    return ret;
}

unsigned int read_uint24_be(const unsigned char *buf)
{
    unsigned int ret;
    ret = (buf[0] << 16) | (buf[1] << 8) | (buf[2]);

    return ret;
}

unsigned int read_uint32_be(const unsigned char *buf)
{
    unsigned int ret;
    ret = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);

    return ret;
}

void write_uint24_be(unsigned char *buf, unsigned int val)
{
    buf[0] = (val >> 16) & 0xff;
    buf[1] = (val >> 8) & 0xff;
    buf[2] = (val >> 0) & 0xff;
}

void write_uint32_be(unsigned char *buf, unsigned int val)
{
    buf[0] = (val >> 24) & 0xff;
    buf[1] = (val >> 16) & 0xff;
    buf[2] = (val >> 8) & 0xff;
    buf[3] = (val >> 0) & 0xff;
}

long delta(struct timeval *x, struct timeval *y)
{
  long int xx = x->tv_sec * 1000000 + x->tv_usec;
  long int yy = y->tv_sec * 1000000 + y->tv_usec;
  long diff = abs(xx - yy);

  return diff;
}

/* TODO: inline this */
uint64_t cycles(void)
{
    uint64_t x;
    __asm__ volatile ("rdtsc\n\t" : "=A" (x));
    return x;
}
