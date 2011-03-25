#ifndef _IMO_MESSAGE_H_
#define _IMO_MESSAGE_H_

/// A message to/from Wowza
typedef struct imo_message {
    unsigned char *text;
    int length;
} imo_message;

/**
 * Parse an incoming imo message and extract the FLV tag, if any.
 *
 * @param msg The incoming message.
 * @param length The length of the incoming message.
 * @param type Will be set to the type of the imo message - 'S', 'D', or 'E'.
 * @param stream_name Will be set to the name of the stream contained in the
 * message. Must be freed by caller
 * @param packet_data Will be set to the embedded FLV tag, or NULL if no FLV tag
 * is present. Must be freed by caller.
 * @param data_len Will be set to the length of the embedded FLV tag.
 */
void decode_imo_message(const imo_message *msg, char *type,
        char **stream_name, unsigned char **packet_data, int *data_len);

imo_message *create_imo_message(char type,
        const char *stream_name, unsigned char *packet_data, int packet_len);

imo_message *create_imo_message_from_text(unsigned char *text, int msg_len);
void imo_message_destroy(imo_message *msg);

#endif
