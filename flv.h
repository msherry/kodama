#ifndef _FLV_H_
#define _FLV_H_

/*************************CONSTANTS FROM FFMEG******************/
/* offsets for packed values */
#define FLV_AUDIO_SAMPLESSIZE_OFFSET 1
#define FLV_AUDIO_SAMPLERATE_OFFSET  2
#define FLV_AUDIO_CODECID_OFFSET     4

#define FLV_VIDEO_FRAMETYPE_OFFSET   4

/* bitmasks to isolate specific values */
#define FLV_AUDIO_CHANNEL_MASK    0x01
#define FLV_AUDIO_SAMPLESIZE_MASK 0x02
#define FLV_AUDIO_SAMPLERATE_MASK 0x0c
#define FLV_AUDIO_CODECID_MASK    0xf0

#define FLV_VIDEO_CODECID_MASK    0x0f
#define FLV_VIDEO_FRAMETYPE_MASK  0xf0

#define AMF_END_OF_OBJECT         0x09

enum {
    FLV_HEADER_FLAG_HASVIDEO = 1,
    FLV_HEADER_FLAG_HASAUDIO = 4,
};

enum {
    FLV_TAG_TYPE_AUDIO = 0x08,
    FLV_TAG_TYPE_VIDEO = 0x09,
    FLV_TAG_TYPE_META  = 0x12,
};

enum {
    FLV_MONO   = 0,
    FLV_STEREO = 1,
};

enum {
    FLV_SAMPLESSIZE_8BIT  = 0,
    FLV_SAMPLESSIZE_16BIT = 1 << FLV_AUDIO_SAMPLESSIZE_OFFSET,
};

enum {
    FLV_SAMPLERATE_SPECIAL = 0, /**< signifies 5512Hz and 8000Hz in the case of NELLYMOSER */
    FLV_SAMPLERATE_11025HZ = 1 << FLV_AUDIO_SAMPLERATE_OFFSET,
    FLV_SAMPLERATE_22050HZ = 2 << FLV_AUDIO_SAMPLERATE_OFFSET,
    FLV_SAMPLERATE_44100HZ = 3 << FLV_AUDIO_SAMPLERATE_OFFSET,
};

enum {
    FLV_CODECID_PCM                  = 0,
    FLV_CODECID_ADPCM                = 1 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_MP3                  = 2 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_PCM_LE               = 3 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_NELLYMOSER_8KHZ_MONO = 5 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_NELLYMOSER           = 6 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_AAC                  = 10<< FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_SPEEX                = 11<< FLV_AUDIO_CODECID_OFFSET,
};

enum {
    FLV_CODECID_H263    = 2,
    FLV_CODECID_SCREEN  = 3,
    FLV_CODECID_VP6     = 4,
    FLV_CODECID_VP6A    = 5,
    FLV_CODECID_SCREEN2 = 6,
    FLV_CODECID_H264    = 7,
};

enum {
    FLV_FRAME_KEY        = 1 << FLV_VIDEO_FRAMETYPE_OFFSET,
    FLV_FRAME_INTER      = 2 << FLV_VIDEO_FRAMETYPE_OFFSET,
    FLV_FRAME_DISP_INTER = 3 << FLV_VIDEO_FRAMETYPE_OFFSET,
};

/******************** END FFMPEG CONSTANTS ********************/

#define KODAMA_MAX_AUDIO_FRAME_SIZE (32000) /// 1 second of 16khz 16bit mono

/// Context for decoding/encoding an FLV stream
typedef struct FLVStream {
    /* Decode */
    unsigned char d_format_byte; /**< The last format byte received */
    int d_flags_size;             /**< contained in format byte */
    struct AVCodecContext *d_codec_ctx;
    struct ReSampleContext *d_resample_ctx; // NULL if not needed

    /* Encode */
    struct AVCodecContext *e_codec_ctx;
    struct ReSampleContext *e_resample_ctx;

    /** Both libavcodec and libspeex are reentrant, but not
     * thread-safe. */
    GMutex *d_mutex, *e_mutex;
} FLVStream;

struct SAMPLE_BLOCK;

/**
 * Must be called before using any of the functions in flv.c.
 *
 */
void flv_init(void);

void flv_parse_header(void);

void flv_start_stream(const char *stream_name);
void flv_end_stream(const char *stream_name);

/**
 * Given an FLV tag, decode it and create a SAMPLE_BLOCK if possible, possibly
 * resampling in the process.
 *
 * \note Caller must free sb.
 *
 * @param packet_data The FLV packet data.
 * @param packet_len The length of the FLV packet data in bytes.
 * @param stream_name The name of the stream this packet is associated with.
 * @param sb The address of a SAMPLE_BLOCK pointer to allocate to contain the
 * decoded samples.
 *
 * @return zero on success, nonzero on failure.
 */int flv_parse_tag(const unsigned char *packet_data, const int packet_len,
    const char *stream_name, struct SAMPLE_BLOCK **sb);

/**
 * Given a SAMPLE_BLOCK and stream name, create an FLV packet ready to be packed
 * into an imo message.
 *
 * \note Caller must free flv_packet.
 *
 * @param flv_packet Address of the packet to create.
 * @param packet_len Will contain the length of the created packet.
 * @param stream_name Used to look up the encoding context.
 * @param sb The samples to include in this packet.
 *
 * @return Zero on success, non-zero on failure.
 */
int flv_create_tag(unsigned char **flv_packet, int *packet_len,
    const char *stream_name, struct SAMPLE_BLOCK *sb);

#endif
