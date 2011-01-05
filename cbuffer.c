#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbuffer.h"
#include "kodama.h"

CBuffer *cbuffer_init(size_t capacity)
{
    CBuffer *cb = malloc(sizeof(CBuffer));

    /* TODO: this only works if SAMPLE_SILENCE == 0 */
    cb->buf = calloc(capacity, sizeof(SAMPLE));
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
    *(cb->head++) = elem;
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
    {
        /* TODO: Can we do better here? If we decide to return silence, should
         * we freeze echo cancellation updating? */
        return SAMPLE_SILENCE;
    }

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
    SAMPLE_BLOCK *sb = sample_block_create(count);

    /* TODO: this is probably less efficient than it could be */
    SAMPLE *head = sb->s;
    while (count--)
    {
        *(head++) = cbuffer_pop(cb);
    }

    return sb;
}

/* Returns count samples, but leaves the samples in the buffer. Caller is
 * responsible for freeing allocated memory */
SAMPLE_BLOCK *cbuffer_peek_samples(CBuffer *cb, size_t count)
{
    SAMPLE_BLOCK *sb = sample_block_create(count);

    SAMPLE *s = sb->s;
    SAMPLE *fake_head = cb->head;
    size_t i=0;
    while (i++ < count)
    {
        SAMPLE sample = *(fake_head--);
        /* If that wasn't valid (not enough entries in the cbuffer, use silence
         * instead) */
        if (i > sb->count)
        {
            sample = SAMPLE_SILENCE;
        }
        *(s++) = sample;
        if (fake_head == (cb->buf-1))
        {
            fake_head = cb->end-1;
        }
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

SAMPLE_BLOCK *sample_block_create(size_t count)
{
    SAMPLE_BLOCK *sb = malloc(sizeof(SAMPLE_BLOCK));
    sb->s = malloc(count * sizeof(SAMPLE));
    sb->count = count;

    return sb;
}

void sample_block_destroy(SAMPLE_BLOCK *sb)
{
    if (!sb)
    {
        return;
    }
    free(sb->s);
    free(sb);
}
