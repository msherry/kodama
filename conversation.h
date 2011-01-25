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

/**
 * Handles processing incoming imo messages - extracts audio data, echo cancels,
 * creates a return message, and queues it to be sent back over the network.
 *
 * @param msg The incoming message
 * @param msg_length The length of the incoming message
 *
 * @return zero on success, non-zero on error.
 */
int r(const unsigned char *msg, int msg_length);

#endif
