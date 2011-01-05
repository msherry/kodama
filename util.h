#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

char *hexify(const unsigned char *buf, const int data_len);
char *hexify_16(const short *buf, const int num_shorts);
char *samples_to_text(const SAMPLE *samples, const int num_samples);
unsigned int read_uint24_be(const unsigned char *buf);
unsigned int read_uint32_be(const unsigned char *buf);

#endif
