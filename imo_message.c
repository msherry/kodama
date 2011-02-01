#include <arpa/inet.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "imo_message.h"
#include "kodama.h"

/* Header format:
   Message length (including header)      - 4 bytes (big-endian)
   Type                                   - 1 byte
   Stream name length                     - 1 byte
   Stream name                            - variable length
*/

/* Caller must free stream_name and packet_data */
/* TODO: error checking would be good */
void decode_imo_message(const unsigned char *msg, const int msg_length, char *type,
        char **stream_name, unsigned char **packet_data, int *packet_len)
{
    int offset = 4;             /* sizeof(int) on java */
    *type = msg[offset++];

    int stream_name_length = (int)msg[offset++];

    *stream_name = malloc(stream_name_length+1); /* trailing \0 */

    memcpy(*stream_name, msg+offset, stream_name_length);
    (*stream_name)[stream_name_length] = '\0';

    offset += stream_name_length;

    /* account for header */
    *packet_len = msg_length - (6+stream_name_length);

    if (*packet_len)
    {
        *packet_data = malloc(*packet_len);
        memcpy(*packet_data, msg+offset, *packet_len);
    }
    else
    {
        /* g_debug("Got an imo message with zero packet len (type %c)", *type); */
        *packet_data = NULL;
    }
}

/* msg must eventually be freed */
void create_imo_message(unsigned char **msg, int *msg_length, char type,
        const char *stream_name, unsigned char *packet_data, int packet_len)
{
    int stream_name_len = strlen(stream_name); // Hope this fits in a byte
    int total_len = 4 + 1 + 1 + stream_name_len + packet_len;
    int offset;
    uint32_t msg_len_be = htonl(total_len);

    *msg = malloc(total_len);
    memcpy(*msg, &msg_len_be, 4); /* 4 bytes exactly, not sizeof(int) */
    offset = 4;

    *(*msg+offset++) = type;

    *(*msg+offset++) = stream_name_len;

    memcpy(*msg+offset, stream_name, stream_name_len);
    offset += stream_name_len;

    memcpy(*msg+offset, packet_data, packet_len);

    *msg_length = total_len;
}
