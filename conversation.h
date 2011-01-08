#ifndef _CONVERSATION_H_
#define _CONVERSATION_H_

typedef struct Conversation {
    struct hybrid *h0, *h1;

} Conversation;

void r(const unsigned char *msg, int msg_length);

#endif
