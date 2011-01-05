#include <stdint.h>

typedef enum protocol {
    raw = 1,
    imo1 = 2
} protocol;

typedef struct proto_header {
    uint16_t proto;
} proto_header;

/* Protocol 1 - UDP */
struct SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes);
gchar *samples_to_message(struct SAMPLE_BLOCK *sb, gint *num_bytes, protocol proto);

/* Protocol 2 -TCP (Wowza) */
SAMPLE_BLOCK *imo_message_to_samples(const unsigned char *msg, int msg_length);
