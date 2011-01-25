#ifndef _CONVERSATION_H_
#define _CONVERSATION_H_

/// Holds information about a single 2-party conversation.
typedef struct Conversation {
    struct hybrid *h0, *h1;
} Conversation;

/**
 * Must be called before using any of the functions in conversation.c.
 *
 */
void init_conversations(void);
void conversation_start(const char *stream_name);
void conversation_end(const char *stream_name);

/**
 * Handles the audio processing for a message - decodes FLV, resampling if
 * necessary, cancels echo, creates a new FLV packet, then a new imo message,
 * and sends it off.
 *
 * @param stream_name Name of the stream which sent us this message.
 * @param flv_data The FLV data containing our audio samples.
 * @param flv_len Length of the FLV packet.
 *
 * @return Zero on success, non-zero on failure.
 */
int r(const char *stream_name, const unsigned char *flv_data, int flv_len);

#endif
