#ifndef _AV_H_
#define _AV_H_

#define DECODE_STREAM 0 // Decode from FLV to raw
#define ENCODE_STREAM 1 // Encode from raw to FLV

struct AVCodecContext;
struct FLVStream;

void init_av(void);
int setup_decode_context(struct FLVStream *flv, unsigned char formatByte);
int setup_encode_context(struct FLVStream *flv);
int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size);
int get_sample_rate(const unsigned char formatbyte);
void local_flv_set_audio_codec(struct AVCodecContext *acodec, int flv_codecid);

#endif
