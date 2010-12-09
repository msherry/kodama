#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "imolist.h"
#include "read_write.h"

/* Map of fd to fd_buffer structs */
GHashTable *fd_to_buffer;

int register_fd(int fd)
{
    /* TODO: create an fd_buffer, insert it into the hashtable. Create the
     * hashtable if necessary */
}

int extract_messages(fd_buffer *fd_buf)
{
    /* Messages will be prepended with their length as a 4-byte integer */

    char *buf = fd_buf->buffer;
    int buf_len = fd_buf->buffer_len;
    int offset = 0;

    while (buf_len - offset > (int)sizeof(int))
    {
        /* First int in the buffer at this offset should be a message length */
        int msg_length;

        memcpy(&msg_length, buf+offset, sizeof(int));
        msg_length = ntohl(msg_length);

        /* We know the size of the next message - do we have that many bytes? */
        if (buf_len >= msg_length + offset + (int)sizeof(int))
        {
            char *temp = malloc(msg_length);
            memcpy(temp, buf+offset+sizeof(int), msg_length);

            /* Queue the message in the full message list */
            slist_append(&(fd_buf->read_head), &(fd_buf->read_tail), temp);

            offset += sizeof(int) + msg_length;
        }
        else
        {
            /* This message is still incomplete */
            break;
        }
    }

    
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
