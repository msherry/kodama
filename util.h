#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

char *hexify(const unsigned char *buf, const int data_len);
unsigned int read_uint24_be(uint8_t *buf);

#endif
