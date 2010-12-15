#ifndef _IMO_MESSAGE_H_
#define _IMO_MESSAGE_H_

void decode_imo_message(const char *msg, const int length, char *type,
        char **stream_name, char **packet_type);


#endif
