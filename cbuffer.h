#ifndef _CBUFFER_H_
#define _CBUFFER_H_

#include <unistd.h>

#include "kodama.h"

typedef struct CBuffer
{
    SAMPLE *buf;
    SAMPLE *end;
    size_t capacity;
    size_t count;
    size_t elem_size;
    SAMPLE *head;
    SAMPLE *tail;
} CBuffer;


typedef struct SAMPLE_BLOCK
{
    SAMPLE *s;
    size_t count;
} SAMPLE_BLOCK;


/* CBuffer methods */
CBuffer *cbuffer_init(size_t capacity);
void cbuffer_destroy(CBuffer *cb);

void cbuffer_push(CBuffer *cb, SAMPLE elem);
SAMPLE cbuffer_pop(CBuffer *cb);

size_t cbuffer_get_count(CBuffer *cb);
size_t cbuffer_get_free(CBuffer *cb);

void cbuffer_push_bulk(CBuffer *cb, SAMPLE_BLOCK *sb);
SAMPLE_BLOCK *cbuffer_get_all(CBuffer *cb);

/* SAMPLE_BLOCK methods */
void sample_block_destroy(SAMPLE_BLOCK *sb);
#endif
