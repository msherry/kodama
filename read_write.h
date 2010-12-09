#ifndef _READ_WRITE_H_
#define _READ_WRITE_H_

typedef enum read_result {
    REMOTE_CLOSED = -9, READ_ERROR = -2, FD_NOT_FOUND = -1,
} read_result;


typedef struct fd_buffer {
    char *buffer;
    int buffer_len;
} fd_buffer;

int read_data(int fd);

#endif
