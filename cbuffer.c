#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"

CBuffer *cbuffer_init(size_t capacity)
{
    CBuffer *cb = malloc(sizeof(CBuffer));

    cb->buf = malloc(capacity * sizeof(SAMPLE));
    cb->end = (SAMPLE *)cb->buf + capacity;
    cb->capacity = capacity;
    cb->count = 0;
    cb->head = cb->buf;
    cb->tail = cb->buf;

    return cb;
}


void cbuffer_destroy(CBuffer *cb)
{
    if(cb == NULL)
        return;

    free(cb->buf);
    cb->buf = NULL;
    free(cb);
}

void cbuffer_push(CBuffer *cb, SAMPLE elem)
{
    *(cb->head) = elem;
    cb->head++;
    if (cb->head == cb->end)
    {
        cb->head = cb->buf;
    }
    if (cb->count == cb->capacity)
    {
        /* Drop a sample from the tail */
        cb->tail++;
        if (cb->tail == cb->end)
        {
            cb->tail = cb->buf;
        }
    }
    else
    {
        cb->count++;
    }
}

SAMPLE cbuffer_pop(CBuffer *cb)
{
    SAMPLE ret;

    if (cb->count == 0)
        return SAMPLE_SILENCE;  /* This is really an error */

    ret = *(cb->tail);
    cb->tail++;
    if (cb->tail == cb->end)
    {
        cb->tail = cb->buf;
    }
    cb->count--;

    return ret;
}

size_t cbuffer_get_count(CBuffer *cb)
{
    return cb->count;
}

size_t cbuffer_get_free(CBuffer *cb)
{
    return cb->capacity - cb->count;
}

/* Caller is responsible for freeing allocated memory */
SAMPLE_BLOCK *cbuffer_get_samples(CBuffer *cb, size_t count)
{
    SAMPLE_BLOCK *sb = malloc(sizeof(SAMPLE_BLOCK));
    sb->s = malloc(count * sizeof(SAMPLE));
    sb->count = count;

    /* TODO: this is probably less efficient than it could be */
    SAMPLE *head = sb->s;
    while (count--)
    {
        *(head++) = cbuffer_pop(cb);
    }

    return sb;
}

void cbuffer_push_bulk(CBuffer *cb, SAMPLE_BLOCK *sb)
{
    SAMPLE *s = sb->s;
    while ((unsigned)(s - sb->s) < sb->count)
    {
        cbuffer_push(cb, *s++);
    }
}

void sample_block_destroy(SAMPLE_BLOCK *sb)
{
    free(sb->s);
    free(sb);
}
