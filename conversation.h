#ifndef _CONVERSATION_H_
#define _CONVERSATION_H_

/// Holds information about a single 2-party conversation.
typedef struct Conversation {
    struct hybrid *h0, *h1;
} Conversation;

void r(const unsigned char *msg, int msg_length);

#endif
