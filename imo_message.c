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
void decode_imo_message(const char *msg, const int msg_length, char *type,
        char **stream_name, char **packet_data)
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
}
