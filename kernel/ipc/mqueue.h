/* mqueue.h - POSIX Message Queues */

#ifndef MQUEUE_H
#define MQUEUE_H

#include "../include/types.h"
#include "../sync/spinlock.h"

/* Maximum number of message queues */
#define MQ_MAX 128

/* Default limits */
#define MQ_DEFAULT_MAXMSG   10
#define MQ_DEFAULT_MSGSIZE  8192

/* Message queue attributes */
typedef struct mq_attr {
    int32_t mq_flags;       /* Flags (O_NONBLOCK) */
    int32_t mq_maxmsg;      /* Max number of messages */
    int32_t mq_msgsize;     /* Max message size */
    int32_t mq_curmsgs;     /* Current number of messages */
} mq_attr_t;

/* Message in queue */
typedef struct mq_message {
    char *data;             /* Message data */
    size_t size;            /* Message size */
    unsigned int priority;  /* Message priority */
    struct mq_message *next;/* Next in priority order */
} mq_message_t;

/* Message queue structure */
typedef struct mqueue {
    char name[256];         /* Queue name */
    mq_attr_t attr;         /* Queue attributes */
    int refcount;           /* Reference count */
    bool unlinked;          /* Marked for deletion */
    spinlock_t lock;
    /* Message list (priority-ordered) */
    mq_message_t *head;
    /* Wait queues */
    uint64_t *send_waiters;     /* Threads waiting to send */
    int send_waiter_count;
    int send_waiter_capacity;
    uint64_t *recv_waiters;     /* Threads waiting to receive */
    int recv_waiter_count;
    int recv_waiter_capacity;
} mqueue_t;

/*
 * Initialize message queue subsystem
 */
void mq_init_subsystem(void);

/*
 * Open a message queue
 *
 * name: Queue name (must start with '/')
 * oflag: Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_NONBLOCK)
 * mode: Permissions (when creating)
 * attr: Initial attributes (when creating, NULL for defaults)
 *
 * Returns: Queue descriptor on success, negative errno on failure
 */
int mq_open_syscall(const char *name, int oflag, mode_t mode, mq_attr_t *attr);

/*
 * Close a message queue
 *
 * mqdes: Queue descriptor
 *
 * Returns: 0 on success, negative errno on failure
 */
int mq_close_syscall(int mqdes);

/*
 * Remove a message queue
 *
 * name: Queue name
 *
 * Returns: 0 on success, negative errno on failure
 */
int mq_unlink_syscall(const char *name);

/*
 * Send a message (simplified, no timeout)
 *
 * mqdes: Queue descriptor
 * msg: Message data
 * msglen: Message length
 * prio: Message priority
 *
 * Returns: 0 on success, negative errno on failure
 */
int mq_send_syscall(int mqdes, const char *msg, size_t msglen, unsigned int prio);

/*
 * Receive a message (simplified, no timeout)
 *
 * mqdes: Queue descriptor
 * msg: Buffer for message data
 * msglen: Buffer size
 * prio: Pointer to store message priority (optional)
 *
 * Returns: Number of bytes received on success, negative errno on failure
 */
int mq_receive_syscall(int mqdes, char *msg, size_t msglen, unsigned int *prio);

/*
 * Get queue attributes
 *
 * mqdes: Queue descriptor
 * attr: Pointer to store attributes
 *
 * Returns: 0 on success, negative errno on failure
 */
int mq_getattr_syscall(int mqdes, mq_attr_t *attr);

/*
 * Set queue attributes
 *
 * mqdes: Queue descriptor
 * newattr: New attributes (only mq_flags can be set)
 * oldattr: Pointer to store old attributes (optional)
 *
 * Returns: 0 on success, negative errno on failure
 */
int mq_setattr_syscall(int mqdes, const mq_attr_t *newattr, mq_attr_t *oldattr);

#endif /* MQUEUE_H */
