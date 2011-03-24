// http://cekirdek.uludag.org.tr/~ismail/ffmpeg-docs/libavcodec_2utils_8c.html

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <glib.h>
#include <sys/time.h>

#include "av.h"
#include "cbuffer.h"
#include "flv.h"
#include "kodama.h"
#include "util.h"

extern globals_t globals;       /* Needed for FLV_LOG */

static FLVStream *create_flv_stream(void);
static void destroy_flv_stream(FLVStream *flv);

static GHashTable *id_to_flvstream;
G_LOCK_DEFINE(id_to_flvstream);

void flv_init(void)
{
    G_LOCK(id_to_flvstream);
    id_to_flvstream = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free,
            /* We don't put the destroy function here - explained below */
            NULL);
    G_UNLOCK(id_to_flvstream);
}

static FLVStream *create_flv_stream(void)
{
    FLVStream *flv = malloc(sizeof(FLVStream));

    flv->d_format_byte = '\0';

    /* TODO: would this be better in the separate setup_encode/decode_context
     * functions */
    flv->d_codec_ctx = avcodec_alloc_context2(CODEC_TYPE_AUDIO);
    flv->d_codec_ctx->codec_id = CODEC_ID_NONE;
    flv->d_resample_ctx = NULL;

    flv->e_codec_ctx = avcodec_alloc_context2(CODEC_TYPE_AUDIO);
    flv->e_codec_ctx->codec_id = CODEC_ID_NONE;
    flv->e_codec_ctx->sample_fmt = SAMPLE_FMT_S16;
    flv->e_resample_ctx = NULL;

    flv->d_mutex = g_mutex_new();
    flv->e_mutex = g_mutex_new();

    return flv;
}

static void destroy_flv_stream(FLVStream *flv)
{
    g_return_if_fail(flv != NULL);

    av_free(flv->d_codec_ctx);
    av_free(flv->d_resample_ctx); /* Ok if this is NULL */

    av_free(flv->e_codec_ctx);
    av_free(flv->e_resample_ctx);

    g_mutex_free(flv->d_mutex);
    g_mutex_free(flv->e_mutex);

    free(flv);
}

void flv_parse_header(void)
{
    /* TODO: */
}

void flv_start_stream(const char *stream_name)
{
    G_LOCK(id_to_flvstream);
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    if (flv)
    {
        g_warning("FLVStream already exists for stream %s - "
            "this is suspicious", stream_name);
    }
    else
    {
        FLV_LOG("Creating FLVStream for %s\n", stream_name);
        flv = create_flv_stream();
        g_hash_table_insert(id_to_flvstream, g_strdup(stream_name), flv);
    }
    G_UNLOCK(id_to_flvstream);
}

void flv_end_stream(const char *stream_name)
{
    FLV_LOG("Destroying FLVStream for %s\n", stream_name);

    /* This will be called while holding a lock on the conversation associated
     * with this FLVStream, so the locking we're doing is probably excessive */
    /* TODO: is this still correct? Conversations now have a mutex for each
     * side */

    G_LOCK(id_to_flvstream);
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);

    /* Ok to call even if the item isn't present */
    g_hash_table_remove(id_to_flvstream, stream_name);

    G_UNLOCK(id_to_flvstream);

    if (flv)
    {
        g_mutex_lock(flv->d_mutex);
        g_mutex_lock(flv->e_mutex);

        g_mutex_unlock(flv->e_mutex);
        g_mutex_unlock(flv->d_mutex);
        destroy_flv_stream(flv);
    }
}

int flv_parse_tag(const unsigned char *packet_data, const int packet_len,
    const char *stream_name, SAMPLE_BLOCK **sb)
{
    /* For details of this format, see:
       http://osflash.org/flv
    */

    struct timeval start, end;
    struct timeval t1, t2;
    long d_us;

    gettimeofday(&start, NULL);
    gettimeofday(&t1, NULL);

    G_LOCK(id_to_flvstream);
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    G_UNLOCK(id_to_flvstream);

    if (!flv)
    {
        /* We'll require that FLVStreams are only created in response to 'S'
         * messages for now */
        g_warning("(%s:%d) No FLVStream found for stream %s - aborting",
            __FILE__, __LINE__, stream_name);
        return -1;
    }

    /* Only one thread should work on this stream at a time */

    /* The locking situation isn't as bad as it is in conversation.c --
     * FLVStreams are only created/destroyed while holding a lock on the parent
     * conversation, so they shouldn't be destroyed from under us between
     * obtaining flv above, and using it below. This probably makes locking flv
     * unnecessary, but we'll keep it here for now */

    g_mutex_lock(flv->d_mutex);

    unsigned char type_code, type;
    int offset = 0;
    int ret = -1;

    FLV_LOG("Packet length: %d\n", packet_len);

    type_code = packet_data[offset++];
    switch(type_code)
    {
    case 0x08:
        /* Audio */
        type = 'A';
        break;
    case 0x09:
        /* Video */
        type = 'V';
        break;
    case 0x12:
        /* Meta */
        type = 'M';
        break;
    default:
        /* Unknown */
        type = 'U';
        FLV_LOG("Unknown packet type: %c\n", type_code);
        goto exit;
        break;
    }
    FLV_LOG("Type: %c\n", type);

    /* 3 bytes, big-endian */
    unsigned int bodyLength;
    bodyLength = read_uint24_be(packet_data+offset);
    offset += 3;
    FLV_LOG("BodyLength: %d\n", bodyLength);

    /* Timestamp - 4 bytes, crazy order */
    unsigned int timestamp;
    timestamp = read_uint24_be(packet_data+offset);
    offset += 3;
    timestamp |= (packet_data[offset++] << 24);
    FLV_LOG("Timestamp: %u  (%#.8x)\n", timestamp, timestamp);

    /* stream id is 3 bytes, and always zero - skip it */
    offset += 3;

    /* The rest is packet data (audio/video), except the last 4 bytes, which
     * should contain the size of this packet */

    /* Figure out what we can from the body */
    if (type == 'A')
    {
        unsigned char formatByte = *(packet_data+offset);

        if (!flv->d_format_byte || flv->d_format_byte != formatByte)
        {
            FLV_LOG("Setting up decode context\n");
            if (setup_decode_context(flv, formatByte))
            {
                FLV_LOG("Error setting up decode context\n");
                goto exit;
            }

            FLV_LOG("Setting up encode context\n");
            if (setup_encode_context(flv))
            {
                FLV_LOG("Error setting up encode context\n");
                goto exit;
            }
        }

        /* ffmpeg claims that memory should be 16-byte aligned for decoding, but
         * it seems to make no difference speed-wise either way */
        uint8_t *aligned;
        int pos_ret;
        pos_ret = posix_memalign((void **)&aligned, 16,
            bodyLength+FF_INPUT_BUFFER_PADDING_SIZE);
        if (pos_ret)
        {
            g_warning("posix_memalign failed with value %d", pos_ret);
            goto exit;
        }
        memset(aligned, 0, bodyLength+FF_INPUT_BUFFER_PADDING_SIZE);
        memcpy(aligned, packet_data+offset+flv->d_flags_size,
            bodyLength-flv->d_flags_size);

        /* The codec context should be set at this point. Convert data */
        AVPacket avpkt;
        av_init_packet(&avpkt);
        avpkt.data = aligned; //packet_data+offset+flv->d_flags_size;
        avpkt.size = bodyLength-flv->d_flags_size;

        /* libspeex forces us to use a buffer this large to decode a
         * frame. Perhaps we should allocate it dynamically, rather than on the
         * stack */
        SAMPLE sample_array[AVCODEC_MAX_AUDIO_FRAME_SIZE];
        int frame_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;

        gettimeofday(&t2, NULL);
        d_us = delta(&t1, &t2);
        /* VERBOSE_LOG("F: Time to set up for decoding: %li\n", d_us); */

        /*
          Warning:
          You must set frame_size_ptr to the allocated size of the output buffer
          before calling avcodec_decode_audio3().

          The input buffer must be FF_INPUT_BUFFER_PADDING_SIZE larger than the
          actual read bytes because some optimized bitstream readers read 32 or
          64 bits at once and could read over the end.

          The end of the input buffer avpkt->data should be set to 0 to ensure
          that no overreading happens for damaged MPEG streams.

          Note:

          You might have to align the input buffer avpkt->data and output buffer
          samples. The alignment requirements depend on the CPU: On some CPUs it
          isn't necessary at all, on others it won't work at all if not aligned
          and on others it will work but it will have an impact on performance.
        */

        gettimeofday(&t1, NULL);

        int bytesDecoded = avcodec_decode_audio3(flv->d_codec_ctx, sample_array,
                &frame_size, &avpkt);

        gettimeofday(&t2, NULL);
        d_us = delta(&t1, &t2);
        /* VERBOSE_LOG("F: Time to decode audio: %li\n", d_us); */

        FLV_LOG("Bytes decoded: %d\n", bytesDecoded);

        free(aligned);

        if (bytesDecoded > 0)
        {
            int numSamples;
            SAMPLE resampled[KODAMA_MAX_AUDIO_FRAME_SIZE];
            SAMPLE *sample_buf = sample_array;

            if (bytesDecoded < avpkt.size)
            {
                g_warning("There was leftover data in avpkt: %d bytes (parsed %d)",
                        avpkt.size-bytesDecoded, bytesDecoded);
            }
            numSamples = frame_size / sizeof(SAMPLE);
            FLV_LOG("Samples decoded: %d\n", numSamples);

            if (flv->d_resample_ctx)
            {
                /* Need to resample */
                FLV_LOG("Resampling from %d to %d Hz\n",
                    flv->d_codec_ctx->sample_rate, SAMPLE_RATE);

                gettimeofday(&t1, NULL);

                int newrate_num_samples;

                newrate_num_samples = audio_resample(flv->d_resample_ctx,
                    resampled, sample_array, numSamples);

                if (newrate_num_samples == 0)
                {
                    g_warning("There was an error resampling");
                    goto exit;
                }

                gettimeofday(&t2, NULL);
                d_us = delta(&t1, &t2);

                /* VERBOSE_LOG("F: Time to resample audio: %li\n", d_us); */

                numSamples = newrate_num_samples;
                sample_buf = resampled;
            }

            char *hex;

            /* hex = hexify(packet_data, packet_len); */
            /* FLV_LOG("Incoming FLV packet: %s\n", hex); */
            /* free(hex); */

            /* hex = samples_to_text(sample_buf, numSamples); */
            /* FLV_LOG("Decoded incoming samples: %s\n", hex); */
            /* free(hex); */

            gettimeofday(&t1, NULL);
            *sb = sample_block_create(numSamples);
            memcpy((*sb)->s, sample_buf, numSamples*sizeof(SAMPLE));
            (*sb)->pts = timestamp; /* We're just going to pass this back to the
                                     * encoder */
            gettimeofday(&t2, NULL);
            d_us = delta(&t1, &t2);
            /* VERBOSE_LOG("F: Time to create SB: %li\n", d_us); */

            ret = 0;
        }
    }
    else if (type == 'V')
    {
        g_warning("Got video frame - I don't know how to handle those yet");
        goto exit;
    }

    offset = (packet_len - 4);
    /* A full 4-byte integer, big-endian. Read it the hard way since ints are
     * probably 8 bytes for us */
    unsigned int prev_tag_size;
    prev_tag_size = read_uint32_be(packet_data + offset);
    offset += 4;
    FLV_LOG("PrevTagSize: %u  (%#.8x)\n", prev_tag_size, prev_tag_size);
    if (prev_tag_size != (unsigned)(packet_len - 4))
    {
        g_warning("********* prev_tag_size = %i, expected %i",
                prev_tag_size, packet_len-4);
    }

exit:
    g_mutex_unlock(flv->d_mutex);

    gettimeofday(&end, NULL);
    d_us = delta(&start, &end);

    /* VERBOSE_LOG("F: All of flv_parse_tag: %li\n", d_us); */

    return ret;
}

int flv_create_tag(unsigned char **flv_packet, int *packet_len,
    const char *stream_name, SAMPLE_BLOCK *sb)
{
    /* This should always only create FLV tags of type 'A' */

    struct timeval t1, t2;
    long d_us;

    char *hex;

    G_LOCK(id_to_flvstream);
    FLVStream *flv = g_hash_table_lookup(id_to_flvstream, stream_name);
    G_UNLOCK(id_to_flvstream);
    if (!flv)
    {
        g_warning("FLVStream not found for stream %s for encoding - aborting",
            stream_name);
        return -1;
    }

    /* Only one thread should work on this stream at a time */
    g_mutex_lock(flv->e_mutex);

    SAMPLE *sample_buf = sb->s;
    int numSamples = sb->count;

    /* hex = samples_to_text(sample_buf, numSamples); */
    /* FLV_LOG("Decoded outgoing samples: %s\n", hex); */
    /* free(hex); */

    /* First we (maybe) need to resample from SAMPLE_RATE to the sample rate the
     * client was originally transmitting */
    SAMPLE resampled[KODAMA_MAX_AUDIO_FRAME_SIZE];
    if (flv->e_resample_ctx)
    {
        FLV_LOG("Resampling from %d to %d Hz\n",
                SAMPLE_RATE, flv->d_codec_ctx->sample_rate);

        int newrate_num_samples = audio_resample(flv->e_resample_ctx,
                resampled, sb->s, sb->count);

        if (newrate_num_samples == 0)
        {
            g_warning("There was an error resampling");
            /* TODO: We should signal this to the caller, but this function
             * doesn't return errors currently */
            goto exit;
        }

        FLV_LOG("Resampled from %d to %d samples\n", (int)sb->count,
            newrate_num_samples);

        sample_buf = resampled;
        numSamples = newrate_num_samples;
    }

    gettimeofday(&t1, NULL);

    uint8_t encoded_audio[FF_MIN_BUFFER_SIZE];
    int bytesEncoded = avcodec_encode_audio(flv->e_codec_ctx, encoded_audio,
                FF_MIN_BUFFER_SIZE, sample_buf);

    gettimeofday(&t2, NULL);
    d_us = delta(&t1, &t2);
    /* VERBOSE_LOG("F: Time to encode audio: %li\n", d_us); */

    FLV_LOG("Bytes encoded: %d\n", bytesEncoded);

    int bodyLength = bytesEncoded + 1; /* +1 for format byte */

    /* We have our audio data (Body part of an FLV tag). Time to create the
     * tag. */
    *packet_len = 1 + 3 + 3 + 1 + 3 + bodyLength + 4;
    *flv_packet = malloc(*packet_len);

    FLV_LOG("Return flv packet len: %d\n", *packet_len);

    int offset = 0;

    *(*flv_packet + offset++) = 0x08; /* Audio packet */

    write_uint24_be((*flv_packet + offset), bodyLength);
    offset += 3;

    int timestamp = sb->pts;
    FLV_LOG("Outgoing timestamp: %d\n", timestamp);
    write_uint24_be((*flv_packet + offset), timestamp);
    offset += 3;
    *(*flv_packet + offset++) = ((timestamp >> 24) & 0xff);

    /* Stream id - three zeros */
    *(*flv_packet + offset++) = 0;
    *(*flv_packet + offset++) = 0;
    *(*flv_packet + offset++) = 0;

    /* Format byte */
    *(*flv_packet + offset++) = flv->d_format_byte;

    /* Body */
    memcpy((*flv_packet + offset), encoded_audio, bytesEncoded);
    offset += bytesEncoded;

    /* Previous tag size */
    write_uint32_be((*flv_packet + offset), *packet_len - 4);
    offset += 4;

    hex = hexify(*flv_packet, *packet_len);
    FLV_LOG("Outgoing FLV packet: %s\n", hex);
    free(hex);

exit:
    g_mutex_unlock(flv->e_mutex);
    return 0;
}
