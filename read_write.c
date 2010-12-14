#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "imolist.h"
#include "read_write.h"

static int extract_messages(fd_buffer *fd_buf);

/* Map of fd to fd_buffer structs */
GHashTable *fd_to_buffer;

void register_fd(int fd)
{
    /* Create an fd_buffer, insert it into the hashtable. Create the hashtable
     * if necessary */

    if (!fd_to_buffer)
    {
        fd_to_buffer = g_hash_table_new(g_direct_hash, g_int_equal);
    }

    fd_buffer *fd_buf = malloc(sizeof(fd_buffer));

    /* Init all fields */
    fd_buf->buffer = NULL;
    fd_buf->buffer_len = 0;

    fd_buf->read_head = fd_buf->read_tail = NULL;
    fd_buf->read_msg_size = g_array_new(FALSE, TRUE, sizeof(int));

    /* TODO: make sure this fd isn't already present as a key */

    g_hash_table_insert(fd_to_buffer, GINT_TO_POINTER(fd), fd_buf);
}

void unregister_fd(int fd)
{
    /* TODO: implement */
}

static int extract_messages(fd_buffer *fd_buf)
{
    /* Messages will be prepended with their length as a 4-byte integer */

    int buf_len = fd_buf->buffer_len;
    int offset = 0;
    int num_msgs = 0;
    char *buf = fd_buf->buffer;
    char *temp;

    while (buf_len - offset > (int)sizeof(int))
    {
        /* Header format:
           Message length (including header)      - 4 bytes
           Type                                   - 1 byte
           Stream name length                     - 1 byte
           Stream name                            - variable length
        */

        /* First int in the buffer at this offset should be a message length */
        int msg_length;

        memcpy(&msg_length, buf+offset, sizeof(int));
        msg_length = ntohl(msg_length);

        /* We know the size of the next message - do we have that many bytes? */
        if (buf_len >= msg_length + offset)
        {
            temp = malloc(msg_length);
            memcpy(temp, buf+offset, msg_length);

            /* Queue the message in the full message list. temp will have to be
             * freed later */
            slist_append(&(fd_buf->read_head), &(fd_buf->read_tail), temp);
            g_array_append_val(fd_buf->read_msg_size, msg_length);

            num_msgs++;

            offset += msg_length;
        }
        else
        {
            /* This message is still incomplete */
            break;
        }
    }

    if (offset == 0)
    {
        /* We didn't get any new messages - there's at most 1 partial message in
         * buffer */
        return 0;
    }

    /* There may be data from a partial message left over in the buffer,
     * starting at offset. Allocate a new buffer for it */
    temp = NULL;
    if (buf_len > offset)
    {
        int partial_len = buf_len - offset;
        temp = malloc(partial_len);
        memcpy(temp, buf+offset, partial_len);
    }

    /* Clean up old buffer info */
    g_free(buf);
    fd_buf->buffer = temp;
    fd_buf->buffer_len = buf_len - offset;

    return num_msgs;
}

int read_data(int fd)
{
    int bytes;
    int num_msgs;
    char *tbuf;
    fd_buffer *fd_buf;

    /* Do we know about this fd? */
    fd_buf = g_hash_table_lookup(fd_to_buffer, GINT_TO_POINTER(fd));
    if (fd_buf == NULL)
    {
        return FD_NOT_FOUND;
    }

    /* How many bytes can be read? */
    if (ioctl(fd, FIONREAD, &bytes))
    {
        /* TODO: check errno here and do something appropriate */
        return READ_ERROR;
    }

    if (bytes > 0)
    {
        tbuf = malloc(bytes);
        if (read(fd, tbuf, bytes) < bytes)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                free(tbuf);
                return 0;
            }

            /* TODO: if we were building up a partial message, I guess it's dead
             * now */
            free(tbuf);
            /* TODO: clean up this fd_buf? */
            return READ_ERROR;
        }

        /* We have at least part of a message in tbuf. Append it to any current
         * incomplete message we have */

        /* Make the buffer large enough to hold all previous data, plus this
         * data */
        fd_buf->buffer = realloc(fd_buf->buffer, fd_buf->buffer_len + bytes);

        /* Append the new data */
        memcpy(fd_buf->buffer + fd_buf->buffer_len, tbuf, bytes);
        fd_buf->buffer_len += bytes;

        free(tbuf);
    }
    else if (bytes == 0)
    {
        /* fd closed - other end closed it? */
        /* TODO: clean up this fd_buf */
        return REMOTE_CLOSED;
    }
    else {
        /* TODO: how does this happen? Does this even happen? */
        return 0;
    }

    num_msgs = extract_messages(fd_buf);

    return num_msgs;
}

int get_next_message(int fd, char **msg, int *msg_length)
{

    fd_buffer *fd_buf;

    /* This existed in a call to read_data immediately prior to this, so should
     * still be around */
    fd_buf = g_hash_table_lookup(fd_to_buffer, GINT_TO_POINTER(fd));

    /* If there are no messages in the sll, give up */
    if (g_slist_length(fd_buf->read_head) <= 0)
    {
        return 0;
    }

    *msg = g_slist_nth_data(fd_buf->read_head, 0);
    slist_delete_first(&(fd_buf->read_head), &(fd_buf->read_tail));

    *msg_length = g_array_index(fd_buf->read_msg_size, int, 0);
    g_array_remove_index(fd_buf->read_msg_size, 0);

    return g_slist_length(fd_buf->read_head);
}
