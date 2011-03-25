#ifndef _READ_WRITE_H_
#define _READ_WRITE_H_

#include <glib.h>               /* Sucks, but oh, well */

typedef enum read_result {
    REMOTE_CLOSED = -9, WRITE_ERROR = -3, READ_ERROR = -2, FD_NOT_FOUND = -1,
} read_result;


typedef struct fd_buffer {
    unsigned char *buffer;
    unsigned int buffer_len;

    GSList *read_head;   /**< Completely read messages */
    GSList *read_tail;

    GSList *write_head;         /**< Messages queued to send */
    GSList *write_tail;

    GMutex *mutex;              /**< per-fd mutex */
} fd_buffer;

struct imo_message;

void init_read_write(void);
void register_fd(int fd);
void unregister_fd(int fd);
int read_data(int fd);
int write_data(int fd);
int get_next_message(int fd, struct imo_message **msg);
int queue_message(int fd, struct imo_message *msg);

#endif
