#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

char *hexify(const unsigned char *buf, const int data_len);
unsigned int read_uint24_be(const unsigned char *buf);
unsigned int read_uint32_be(const unsigned char *buf);

#endif
