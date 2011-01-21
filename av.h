#ifndef _AV_H_
#define _AV_H_

#define DECODE_STREAM 0 // Decode from FLV to raw
#define ENCODE_STREAM 1 // Encode from raw to FLV

struct AVCodecContext;
struct FLVStream;

void init_av(void);
int setup_decode_context(struct FLVStream *flv, unsigned char formatByte);
int setup_encode_context(struct FLVStream *flv);

#endif
