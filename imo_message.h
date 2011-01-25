#ifndef _IMO_MESSAGE_H_
#define _IMO_MESSAGE_H_

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
void decode_imo_message(const unsigned char *msg, const int length, char *type,
        char **stream_name, unsigned char **packet_data, int *data_len);

void create_imo_message(unsigned char **msg, int *msg_length, char type,
        const char *stream_name, unsigned char *packet_data, int packet_len);


#endif
