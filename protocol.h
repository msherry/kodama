#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>

typedef enum protocol {
    raw = 1,
    imo1 = 2
} protocol;

typedef struct proto_header {
    uint16_t proto;
} proto_header;

struct imo_message;

void init_protocol(void);
void exit_all_threads(void);


void queue_imo_message_for_worker(struct imo_message *msg);

/* Protocol 1 - UDP */
struct SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes);
gchar *samples_to_message(struct SAMPLE_BLOCK *sb, gint *num_bytes, protocol proto);

/* Protocol 2 - imo messages */
void handle_imo_message(struct imo_message *msg);

#endif
