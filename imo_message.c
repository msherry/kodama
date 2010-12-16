#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "imo_message.h"
#include "kodama.h"

/* Header format:
   Message length (including header)      - 4 bytes
   Type                                   - 1 byte
   Stream name length                     - 1 byte
   Stream name                            - variable length
*/

/* Caller must free stream_name and packet_data */
void decode_imo_message(const unsigned char *msg, const int msg_length, char *type,
        unsigned char **stream_name, unsigned char **packet_data, int *data_len)
{
    UNUSED(msg_length);
    UNUSED(packet_data);

    int offset = 4;             /* sizeof(int) on java */
    *type = msg[offset++];

    int stream_name_length = (int)msg[offset++];

    *stream_name = malloc(stream_name_length+1); /* trailing \0 */

    memcpy(*stream_name, msg+offset, stream_name_length);
    (*stream_name)[stream_name_length] = '\0';

    offset += stream_name_length;

    /* account for header */
    int packet_length = msg_length - (6+stream_name_length);
    *packet_data = malloc(packet_length);
    memcpy(*packet_data, msg+offset, packet_length);

    *data_len = packet_length;
}
