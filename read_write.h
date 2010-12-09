#ifndef _READ_WRITE_H_
#define _READ_WRITE_H_

#include <glib.h>               /* Sucks, but oh, well */

typedef enum read_result {
    REMOTE_CLOSED = -9, READ_ERROR = -2, FD_NOT_FOUND = -1,
} read_result;


typedef struct fd_buffer {
    char *buffer;
    int buffer_len;

    GSList *read_head;   /* Completely read messages */
    GSList *read_tail;
} fd_buffer;

void register_fd(int fd);
void unregister_fd(int fd);
int read_data(int fd);

#endif
