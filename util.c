#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

unsigned int read_uint24_be(uint8_t *buf)
{
    unsigned int ret;
    ret = (buf[0] << 16) | (buf[1] << 8) | (buf[2]);

    return ret;
}

unsigned int read_uint32_be(uint8_t *buf)
{
    unsigned int ret;
    ret = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);

    return ret;
}
