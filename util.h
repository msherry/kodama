#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

char *hexify(const unsigned char *buf, const int data_len);
char *hexify_16(const short *buf, const int num_shorts);
/**
 * Create a hex representation of the given samples
 *
 * @param samples buffer containing the samples to represent as hex
 * @param num_samples number of samples in buffer
 *
 * @return string representation of samples
 */
char *samples_to_text(const SAMPLE *samples, const int num_samples);
unsigned int read_uint24_be(const unsigned char *buf);
unsigned int read_uint32_be(const unsigned char *buf);
void write_uint24_be(unsigned char *buf, const unsigned int val);
void write_uint32_be(unsigned char *buf, const unsigned int val);
long delta(struct timeval *x, struct timeval *y);


#endif
