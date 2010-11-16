typedef enum protocol {
    raw = 1,
    imo1 = 2
} protocol;

struct SAMPLE_BLOCK *message_to_samples(gchar *buf, gint num_bytes);
gchar *samples_to_message(struct SAMPLE_BLOCK *sb, gint *num_bytes);
