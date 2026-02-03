/* mqueue.c - POSIX Message Queues Implementation */

#include "mqueue.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../debug/debug.h"
#include "../sched/thread.h"
#include "../sched/sched.h"
#include "../fs/vfs.h"  /* For O_* flags */

/* Global message queue table */
static mqueue_t *mqueues[MQ_MAX];
static spinlock_t mq_table_lock = SPINLOCK_INIT;

/* Per-process message queue handle table */
#define MQ_HANDLE_MAX 64
static mqueue_t *mq_handles[MQ_HANDLE_MAX];
static spinlock_t mq_handle_lock = SPINLOCK_INIT;

/*
 * Initialize message queue subsystem
 */
void mq_init_subsystem(void) {
    memset(mqueues, 0, sizeof(mqueues));
    memset(mq_handles, 0, sizeof(mq_handles));
    spin_init(&mq_table_lock);
    spin_init(&mq_handle_lock);
    INFO("POSIX message queues initialized");
}

/*
 * Find a message queue by name
 */
static mqueue_t *mq_find_by_name(const char *name) {
    for (int i = 0; i < MQ_MAX; i++) {
        if (mqueues[i] && strcmp(mqueues[i]->name, name) == 0) {
            return mqueues[i];
        }
    }
    return NULL;
}

/*
 * Allocate a message queue handle
 */
static int mq_alloc_handle(mqueue_t *mq) {
    uint64_t flags = spin_lock_irqsave(&mq_handle_lock);

    for (int i = 0; i < MQ_HANDLE_MAX; i++) {
        if (!mq_handles[i]) {
            mq_handles[i] = mq;
            spin_unlock_irqrestore(&mq_handle_lock, flags);
            return i;
        }
    }

    spin_unlock_irqrestore(&mq_handle_lock, flags);
    return -EMFILE;
}

/*
 * Get message queue from handle
 */
static mqueue_t *mq_get_by_handle(int handle) {
    if (handle < 0 || handle >= MQ_HANDLE_MAX) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&mq_handle_lock);
    mqueue_t *mq = mq_handles[handle];
    spin_unlock_irqrestore(&mq_handle_lock, flags);

    return mq;
}

/*
 * Free a message queue handle
 */
static void mq_free_handle(int handle) {
    if (handle < 0 || handle >= MQ_HANDLE_MAX) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&mq_handle_lock);
    mq_handles[handle] = NULL;
    spin_unlock_irqrestore(&mq_handle_lock, flags);
}

/*
 * Insert message into queue (priority-ordered)
 */
static void mq_insert_message(mqueue_t *mq, mq_message_t *msg) {
    /* Insert in priority order (higher priority first) */
    if (!mq->head || msg->priority > mq->head->priority) {
        msg->next = mq->head;
        mq->head = msg;
        return;
    }

    mq_message_t *prev = mq->head;
    while (prev->next && prev->next->priority >= msg->priority) {
        prev = prev->next;
    }

    msg->next = prev->next;
    prev->next = msg;
}

/*
 * Remove and return highest priority message
 */
static mq_message_t *mq_remove_message(mqueue_t *mq) {
    if (!mq->head) {
        return NULL;
    }

    mq_message_t *msg = mq->head;
    mq->head = msg->next;
    msg->next = NULL;
    return msg;
}

/*
 * Add waiter to send wait queue
 */
static int mq_add_send_waiter(mqueue_t *mq, uint64_t thread_id) {
    if (mq->send_waiter_count >= mq->send_waiter_capacity) {
        int new_capacity = mq->send_waiter_capacity == 0 ? 4 : mq->send_waiter_capacity * 2;
        uint64_t *new_waiters = kmalloc(new_capacity * sizeof(uint64_t));
        if (!new_waiters) {
            return -ENOMEM;
        }

        if (mq->send_waiters) {
            memcpy(new_waiters, mq->send_waiters, mq->send_waiter_count * sizeof(uint64_t));
            kfree(mq->send_waiters);
        }

        mq->send_waiters = new_waiters;
        mq->send_waiter_capacity = new_capacity;
    }

    mq->send_waiters[mq->send_waiter_count++] = thread_id;
    return 0;
}

/*
 * Add waiter to receive wait queue
 */
static int mq_add_recv_waiter(mqueue_t *mq, uint64_t thread_id) {
    if (mq->recv_waiter_count >= mq->recv_waiter_capacity) {
        int new_capacity = mq->recv_waiter_capacity == 0 ? 4 : mq->recv_waiter_capacity * 2;
        uint64_t *new_waiters = kmalloc(new_capacity * sizeof(uint64_t));
        if (!new_waiters) {
            return -ENOMEM;
        }

        if (mq->recv_waiters) {
            memcpy(new_waiters, mq->recv_waiters, mq->recv_waiter_count * sizeof(uint64_t));
            kfree(mq->recv_waiters);
        }

        mq->recv_waiters = new_waiters;
        mq->recv_waiter_capacity = new_capacity;
    }

    mq->recv_waiters[mq->recv_waiter_count++] = thread_id;
    return 0;
}

/*
 * Wake one send waiter
 */
static void mq_wake_send_waiter(mqueue_t *mq) {
    if (mq->send_waiter_count > 0) {
        uint64_t thread_id = mq->send_waiters[0];
        /* Shift remaining waiters */
        for (int i = 1; i < mq->send_waiter_count; i++) {
            mq->send_waiters[i - 1] = mq->send_waiters[i];
        }
        mq->send_waiter_count--;

        if (thread_id != 0) {
            thread_t *waiter = (thread_t *)thread_id;
            thread_unblock(waiter);
        }
    }
}

/*
 * Wake one receive waiter
 */
static void mq_wake_recv_waiter(mqueue_t *mq) {
    if (mq->recv_waiter_count > 0) {
        uint64_t thread_id = mq->recv_waiters[0];
        /* Shift remaining waiters */
        for (int i = 1; i < mq->recv_waiter_count; i++) {
            mq->recv_waiters[i - 1] = mq->recv_waiters[i];
        }
        mq->recv_waiter_count--;

        if (thread_id != 0) {
            thread_t *waiter = (thread_t *)thread_id;
            thread_unblock(waiter);
        }
    }
}

/*
 * Open a message queue
 */
int mq_open_syscall(const char *name, int oflag, mode_t mode, mq_attr_t *attr) {
    (void)mode;  /* Mode not used for now */

    if (!name || name[0] != '/') {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&mq_table_lock);

    /* Check if already exists */
    mqueue_t *mq = mq_find_by_name(name);

    if (mq) {
        /* Queue exists */
        if (oflag & O_EXCL) {
            spin_unlock_irqrestore(&mq_table_lock, flags);
            return -EEXIST;
        }

        if (mq->unlinked) {
            spin_unlock_irqrestore(&mq_table_lock, flags);
            return -ENOENT;
        }

        mq->refcount++;
        spin_unlock_irqrestore(&mq_table_lock, flags);

        return mq_alloc_handle(mq);
    }

    /* Create new queue if O_CREAT specified */
    if (!(oflag & O_CREAT)) {
        spin_unlock_irqrestore(&mq_table_lock, flags);
        return -ENOENT;
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MQ_MAX; i++) {
        if (!mqueues[i]) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spin_unlock_irqrestore(&mq_table_lock, flags);
        return -ENOSPC;
    }

    /* Allocate message queue */
    mq = kmalloc(sizeof(mqueue_t));
    if (!mq) {
        spin_unlock_irqrestore(&mq_table_lock, flags);
        return -ENOMEM;
    }

    memset(mq, 0, sizeof(mqueue_t));
    strncpy(mq->name, name, sizeof(mq->name) - 1);
    mq->name[sizeof(mq->name) - 1] = '\0';
    mq->refcount = 1;
    mq->unlinked = false;
    spin_init(&mq->lock);

    /* Set attributes */
    if (attr) {
        mq->attr = *attr;
    } else {
        mq->attr.mq_flags = 0;
        mq->attr.mq_maxmsg = MQ_DEFAULT_MAXMSG;
        mq->attr.mq_msgsize = MQ_DEFAULT_MSGSIZE;
        mq->attr.mq_curmsgs = 0;
    }

    mq->head = NULL;
    mq->send_waiters = NULL;
    mq->send_waiter_count = 0;
    mq->send_waiter_capacity = 0;
    mq->recv_waiters = NULL;
    mq->recv_waiter_count = 0;
    mq->recv_waiter_capacity = 0;

    mqueues[slot] = mq;
    spin_unlock_irqrestore(&mq_table_lock, flags);

    return mq_alloc_handle(mq);
}

/*
 * Close a message queue
 */
int mq_close_syscall(int mqdes) {
    mqueue_t *mq = mq_get_by_handle(mqdes);
    if (!mq) {
        return -EBADF;
    }

    uint64_t flags = spin_lock_irqsave(&mq->lock);
    mq->refcount--;

    bool should_free = (mq->refcount == 0 && mq->unlinked);
    spin_unlock_irqrestore(&mq->lock, flags);

    mq_free_handle(mqdes);

    /* If unlinked and no references, free it */
    if (should_free) {
        uint64_t table_flags = spin_lock_irqsave(&mq_table_lock);
        for (int i = 0; i < MQ_MAX; i++) {
            if (mqueues[i] == mq) {
                mqueues[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&mq_table_lock, table_flags);

        /* Free all messages */
        while (mq->head) {
            mq_message_t *msg = mq_remove_message(mq);
            if (msg) {
                if (msg->data) {
                    kfree(msg->data);
                }
                kfree(msg);
            }
        }

        if (mq->send_waiters) {
            kfree(mq->send_waiters);
        }
        if (mq->recv_waiters) {
            kfree(mq->recv_waiters);
        }
        kfree(mq);
    }

    return 0;
}

/*
 * Remove a message queue
 */
int mq_unlink_syscall(const char *name) {
    if (!name || name[0] != '/') {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&mq_table_lock);

    mqueue_t *mq = mq_find_by_name(name);
    if (!mq) {
        spin_unlock_irqrestore(&mq_table_lock, flags);
        return -ENOENT;
    }

    spin_unlock_irqrestore(&mq_table_lock, flags);

    uint64_t mq_flags = spin_lock_irqsave(&mq->lock);
    mq->unlinked = true;

    /* If no references, free it immediately */
    if (mq->refcount == 0) {
        spin_unlock_irqrestore(&mq->lock, mq_flags);

        uint64_t table_flags = spin_lock_irqsave(&mq_table_lock);
        for (int i = 0; i < MQ_MAX; i++) {
            if (mqueues[i] == mq) {
                mqueues[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&mq_table_lock, table_flags);

        /* Free all messages */
        while (mq->head) {
            mq_message_t *msg = mq_remove_message(mq);
            if (msg) {
                if (msg->data) {
                    kfree(msg->data);
                }
                kfree(msg);
            }
        }

        if (mq->send_waiters) {
            kfree(mq->send_waiters);
        }
        if (mq->recv_waiters) {
            kfree(mq->recv_waiters);
        }
        kfree(mq);
    } else {
        spin_unlock_irqrestore(&mq->lock, mq_flags);
    }

    return 0;
}

/*
 * Send a message
 */
int mq_send_syscall(int mqdes, const char *msg_data, size_t msglen, unsigned int prio) {
    mqueue_t *mq = mq_get_by_handle(mqdes);
    if (!mq) {
        return -EBADF;
    }

    if (msglen > (size_t)mq->attr.mq_msgsize) {
        return -EMSGSIZE;
    }

    uint64_t flags = spin_lock_irqsave(&mq->lock);

    /* Wait if queue is full (unless O_NONBLOCK) */
    while (mq->attr.mq_curmsgs >= mq->attr.mq_maxmsg) {
        if (mq->attr.mq_flags & O_NONBLOCK) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return -EAGAIN;
        }

        thread_t *current = thread_current();
        if (!current) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return -EINVAL;
        }

        int ret = mq_add_send_waiter(mq, (uint64_t)current);
        if (ret < 0) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return ret;
        }

        spin_unlock_irqrestore(&mq->lock, flags);
        thread_block();
        sched_reschedule();

        flags = spin_lock_irqsave(&mq->lock);
    }

    /* Allocate message */
    mq_message_t *msg = kmalloc(sizeof(mq_message_t));
    if (!msg) {
        spin_unlock_irqrestore(&mq->lock, flags);
        return -ENOMEM;
    }

    msg->data = kmalloc(msglen);
    if (!msg->data) {
        kfree(msg);
        spin_unlock_irqrestore(&mq->lock, flags);
        return -ENOMEM;
    }

    memcpy(msg->data, msg_data, msglen);
    msg->size = msglen;
    msg->priority = prio;
    msg->next = NULL;

    /* Insert into queue */
    mq_insert_message(mq, msg);
    mq->attr.mq_curmsgs++;

    /* Wake a receive waiter if any */
    mq_wake_recv_waiter(mq);

    spin_unlock_irqrestore(&mq->lock, flags);

    return 0;
}

/*
 * Receive a message
 */
int mq_receive_syscall(int mqdes, char *msg_buf, size_t msg_len, unsigned int *msg_prio) {
    mqueue_t *mq = mq_get_by_handle(mqdes);
    if (!mq) {
        return -EBADF;
    }

    if (msg_len < (size_t)mq->attr.mq_msgsize) {
        return -EMSGSIZE;
    }

    uint64_t flags = spin_lock_irqsave(&mq->lock);

    /* Wait if queue is empty (unless O_NONBLOCK) */
    while (mq->attr.mq_curmsgs == 0) {
        if (mq->attr.mq_flags & O_NONBLOCK) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return -EAGAIN;
        }

        thread_t *current = thread_current();
        if (!current) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return -EINVAL;
        }

        int ret = mq_add_recv_waiter(mq, (uint64_t)current);
        if (ret < 0) {
            spin_unlock_irqrestore(&mq->lock, flags);
            return ret;
        }

        spin_unlock_irqrestore(&mq->lock, flags);
        thread_block();
        sched_reschedule();

        flags = spin_lock_irqsave(&mq->lock);
    }

    /* Remove message from queue */
    mq_message_t *msg = mq_remove_message(mq);
    if (!msg) {
        spin_unlock_irqrestore(&mq->lock, flags);
        return -EINVAL;
    }

    mq->attr.mq_curmsgs--;

    /* Copy message data */
    size_t copy_len = msg->size < msg_len ? msg->size : msg_len;
    memcpy(msg_buf, msg->data, copy_len);

    if (msg_prio) {
        *msg_prio = msg->priority;
    }

    size_t ret_len = msg->size;

    /* Free message */
    kfree(msg->data);
    kfree(msg);

    /* Wake a send waiter if any */
    mq_wake_send_waiter(mq);

    spin_unlock_irqrestore(&mq->lock, flags);

    return (int)ret_len;
}

/*
 * Get queue attributes
 */
int mq_getattr_syscall(int mqdes, mq_attr_t *attr) {
    mqueue_t *mq = mq_get_by_handle(mqdes);
    if (!mq) {
        return -EBADF;
    }

    if (!attr) {
        return -EFAULT;
    }

    uint64_t flags = spin_lock_irqsave(&mq->lock);
    *attr = mq->attr;
    spin_unlock_irqrestore(&mq->lock, flags);

    return 0;
}

/*
 * Set queue attributes
 */
int mq_setattr_syscall(int mqdes, const mq_attr_t *newattr, mq_attr_t *oldattr) {
    mqueue_t *mq = mq_get_by_handle(mqdes);
    if (!mq) {
        return -EBADF;
    }

    if (!newattr) {
        return -EFAULT;
    }

    uint64_t flags = spin_lock_irqsave(&mq->lock);

    if (oldattr) {
        *oldattr = mq->attr;
    }

    /* Only mq_flags can be modified */
    mq->attr.mq_flags = newattr->mq_flags;

    spin_unlock_irqrestore(&mq->lock, flags);

    return 0;
}
