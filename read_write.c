#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "imolist.h"
#include "kodama.h"
#include "read_write.h"

static int extract_messages(fd_buffer *fd_buf);

/* Map of fd to fd_buffer structs */
GHashTable *fd_to_buffer = NULL;

void init_read_write(void)
{
    if (!fd_to_buffer)
    {
        fd_to_buffer = g_hash_table_new(g_direct_hash, NULL);
    }
}

void register_fd(int fd)
{
    /* Create an fd_buffer, insert it into the hashtable. Create the hashtable
     * if necessary */

    fd_buffer *fd_buf = malloc(sizeof(fd_buffer));

    /* Init all fields */
    fd_buf->buffer = NULL;
    fd_buf->buffer_len = 0;

    fd_buf->read_head = fd_buf->read_tail = NULL;
    fd_buf->read_msg_size = g_array_new(FALSE, TRUE, sizeof(int));

    fd_buf->write_head = fd_buf->write_tail = NULL;
    fd_buf->write_msg_size = g_array_new(FALSE, TRUE, sizeof(int));

    fd_buf->mutex = g_mutex_new();

    /* TODO: make sure this fd isn't already present as a key */

    g_hash_table_insert(fd_to_buffer, GINT_TO_POINTER(fd), fd_buf);
}

void unregister_fd(int fd)
{
    /* TODO: implement */
    UNUSED(fd);
}

static int extract_messages(fd_buffer *fd_buf)
{
    /* Messages will be prepended with their length as a 4-byte integer */

    int buf_len = fd_buf->buffer_len;
    int offset = 0;
    int num_msgs = 0;
    unsigned char *buf = fd_buf->buffer;
    unsigned char *temp;

    /* We use 4, not sizeof(int), since these come from java, where an int is
     * always 4 bytes */

    while (buf_len - offset > 4)
    {
        /* Header format:
           Message length (including header)      - 4 bytes (big-endian)
           Type                                   - 1 byte
           Stream name length                     - 1 byte
           Stream name                            - variable length
        */

        /* First int (4 bytes only) in the buffer at this offset should be a
         * (big-endian) message length */
        int32_t msg_length;
        msg_length = ntohl(*((int32_t *)(buf+offset)));

        /* We know the size of the next message - do we have that many bytes? */
        if (buf_len >= msg_length + offset)
        {
            temp = malloc(msg_length);
            memcpy(temp, buf+offset, msg_length);

            /* Queue the message in the full message list. temp will have to be
             * freed later */
            g_mutex_lock(fd_buf->mutex);
            slist_append(&(fd_buf->read_head), &(fd_buf->read_tail), temp);
            g_array_append_val(fd_buf->read_msg_size, msg_length);
            g_mutex_unlock(fd_buf->mutex);

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
        g_debug("(%s:%d) fd not found: %d", __FILE__, __LINE__, fd);
        return FD_NOT_FOUND;
    }

    /* How many bytes can be read? */
    if (ioctl(fd, FIONREAD, &bytes))
    {
        /* TODO: check errno here and do something appropriate */
        g_debug("(%s:%d) error performing ioctl on fd: %d",
                __FILE__, __LINE__, fd);
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

            g_debug("(%s:%d) Error other than EINTR or "
                    "EAGAIN when reading from fd: %d", __FILE__, __LINE__, fd);
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
        /* Other end closed the fd. Returning this signals caller to tell us to
         * clean up fd_buf, among other things */
        return REMOTE_CLOSED;
    }
    else {
        /* TODO: how does this happen? Does this even happen? */
        g_debug("(%s:%d) Entering this clause was a complete surprise to me",
                __FILE__, __LINE__);
        return 0;
    }

    num_msgs = extract_messages(fd_buf);

    return num_msgs;
}

/* Sends the message at the head of the write list for a given fd. Returns the
 * number of messages remaining in the write queue. */
int write_data(int fd)
{
    fd_buffer *fd_buf;
    unsigned char *msg = NULL;
    int written = 0, total;

    fd_buf = g_hash_table_lookup(fd_to_buffer, GINT_TO_POINTER(fd));
    if (fd_buf == NULL)
    {
        return FD_NOT_FOUND;
    }

    if (g_slist_length(fd_buf->write_head) <= 0)
    {
        return 0;
    }

    msg = g_slist_nth_data(fd_buf->write_head, 0);
    slist_delete_first(&(fd_buf->write_head), &(fd_buf->write_tail));

    total = g_array_index(fd_buf->write_msg_size, int, 0);
    g_array_remove_index(fd_buf->write_msg_size, 0);

    while (written < total)
    {
        int length;
        if((length = write(fd, msg+written, total-written)) < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                continue;
            }
            /* Some other error */
            free(msg);
            /* TODO: handle this */
            return WRITE_ERROR;
        }
        written += length;
    }

    /* We're done with msg at this point */
    free(msg);

    /* g_debug("Sent an imo packet"); */

    return g_slist_length(fd_buf->write_head);
}

int get_next_message(int fd, unsigned char **msg, int *msg_length)
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

int queue_message(int fd, const unsigned char *msg, int length)
{
    fd_buffer *fd_buf;
    char *new_msg;

    if (!msg || length <= 0)
    {
        return 0;
    }

    fd_buf = g_hash_table_lookup(fd_to_buffer, GINT_TO_POINTER(fd));
    if (fd_buf == NULL)
    {
        g_warning("(%s:%d) No fd_buffer found for fd %d", __FILE__, __LINE__,
                fd);
        return -2;
    }

    /* TODO: this is retarded - we're copying the message just to enqueue
     * it. Avoid this */

    /* length includes the extra 4 bytes for the message size */
    uint32_t length_net_order;

    new_msg = malloc(length);
    length_net_order = htonl(length);
    memcpy(new_msg, &length_net_order, 4); /* not sizeof(int) */
    memcpy(new_msg+4, msg+4, length-4);


    /* Queue the new message in the write list */
    g_mutex_lock(fd_buf->mutex);
    slist_append(&(fd_buf->write_head), &(fd_buf->write_tail), new_msg);
    g_array_append_val(fd_buf->write_msg_size, length);
    g_mutex_unlock(fd_buf->mutex);

    return 0;
}
