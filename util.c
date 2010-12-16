#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

char *hexify(const char *buf, const int data_len)
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
