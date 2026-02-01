/* pipe.h - Pipe implementation */
#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pipe buffer size (4KB) */
#define PIPE_BUF_SIZE   4096

/* Pipe structure */
typedef struct pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;          /* Read position */
    uint32_t write_pos;         /* Write position */
    uint32_t count;             /* Bytes in buffer */
    uint32_t readers;           /* Number of read ends open */
    uint32_t writers;           /* Number of write ends open */
} pipe_t;

/*
 * Create a pipe
 * Returns 0 on success, negative error on failure
 * read_fd and write_fd are set to the file descriptors
 */
int pipe_create(int *read_fd, int *write_fd);

/*
 * Initialize pipe subsystem
 */
void pipe_init(void);

#endif /* _KERNEL_PIPE_H */
