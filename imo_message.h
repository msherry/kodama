#ifndef _IMO_MESSAGE_H_
#define _IMO_MESSAGE_H_

void decode_imo_message(const unsigned char *msg, const int length, char *type,
        char **stream_name, unsigned char **packet_data, int *data_len);


#endif
