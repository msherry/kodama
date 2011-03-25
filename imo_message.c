#include <arpa/inet.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "imo_message.h"

/* Header format:
   Message length (including header)      - 4 bytes (big-endian)
   Type                                   - 1 byte
   Stream name length                     - 1 byte
   Stream name                            - variable length
*/

/* Caller must free stream_name and packet_data */
/* TODO: error checking would be good */
void decode_imo_message(const imo_message *msg, char *type,
        char **stream_name, unsigned char **packet_data, int *packet_len)
{
    int offset = 4;             /* sizeof(int) on java */
    unsigned char *text = msg->text;
    *type = text[offset++];

    int stream_name_length = (int)text[offset++];

    *stream_name = malloc(stream_name_length+1); /* trailing \0 */

    memcpy(*stream_name, text+offset, stream_name_length);
    (*stream_name)[stream_name_length] = '\0';

    offset += stream_name_length;

    /* account for header */
    *packet_len = msg->length - (6+stream_name_length);

    if (*packet_len)
    {
        *packet_data = malloc(*packet_len);
        memcpy(*packet_data, text+offset, *packet_len);
    }
    else
    {
        /* g_debug("Got an imo message with zero packet len (type %c)", *type); */
        *packet_data = NULL;
    }
}

/* returned imo_message must eventually be freed */
imo_message *create_imo_message(char type,
        const char *stream_name, unsigned char *packet_data, int packet_len)
{
    int stream_name_len = strlen(stream_name); // Hope this fits in a byte
    int total_len = 4 + 1 + 1 + stream_name_len + packet_len;
    int offset;
    uint32_t msg_len_be = htonl(total_len);

    unsigned char *text;

    text = malloc(total_len);
    memcpy(text, &msg_len_be, 4); /* 4 bytes exactly, not sizeof(int) */
    offset = 4;

    *(text+offset++) = type;

    *(text+offset++) = stream_name_len;

    memcpy(text+offset, stream_name, stream_name_len);
    offset += stream_name_len;

    memcpy(text+offset, packet_data, packet_len);

    imo_message *msg = create_imo_message_from_text(text, total_len);

    return msg;
}

imo_message *create_imo_message_from_text(unsigned char *text, int msg_len)
{
    imo_message *msg = malloc(sizeof(imo_message));

    msg->text = text;
    msg->length = msg_len;
    msg->ts = malloc(sizeof(struct timeval));
    gettimeofday(msg->ts, NULL);

    return msg;
}

void imo_message_destroy(imo_message *msg)
{
    if (msg == NULL)
    {
        return;
    }

    free(msg->text);
    free(msg);
}
