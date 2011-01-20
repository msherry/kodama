#ifndef _AV_H_
#define _AV_H_

#define DECODE_STREAM 0 // Decode from FLV to raw
#define ENCODE_STREAM 1 // Encode from raw to FLV

struct AVCodecContext;

void init_av(void);
int decode_format_byte(const unsigned char formatByte, int *codecid,
        int *sampleRate, int *channels, int *sampleSize, int *flags_size);
int get_sample_rate(const unsigned char formatbyte);
void local_flv_set_audio_codec(struct AVCodecContext *acodec, int flv_codecid);

#endif
