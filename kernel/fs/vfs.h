/* vfs.h - Virtual File System abstraction layer */
#ifndef _KERNEL_VFS_H
#define _KERNEL_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* File types */
#define VFS_FILE      0x01
#define VFS_DIR       0x02
#define VFS_CHARDEV   0x03
#define VFS_BLKDEV    0x04
#define VFS_PIPE      0x05
#define VFS_SYMLINK   0x06
#define VFS_SOCKET    0x07
#define VFS_MOUNTPT   0x08

/* Open flags */
#define O_RDONLY      0x0000
#define O_WRONLY      0x0001
#define O_RDWR        0x0002
#define O_ACCMODE     0x0003
#define O_CREAT       0x0100
#define O_EXCL        0x0200
#define O_TRUNC       0x0400
#define O_APPEND      0x0800
#define O_NONBLOCK    0x1000
#define O_DIRECTORY   0x2000

/* Seek modes */
#define SEEK_SET      0
#define SEEK_CUR      1
#define SEEK_END      2

/* Permissions */
#define VFS_PERM_READ   0x04
#define VFS_PERM_WRITE  0x02
#define VFS_PERM_EXEC   0x01

/* Error codes */
#define VFS_OK          0
#define VFS_ENOENT     -2   /* No such file or directory */
#define VFS_EIO        -5   /* I/O error */
#define VFS_EBADF      -9   /* Bad file descriptor */
#define VFS_ENOMEM    -12   /* Out of memory */
#define VFS_EEXIST    -17   /* File exists */
#define VFS_ENOTDIR   -20   /* Not a directory */
#define VFS_EISDIR    -21   /* Is a directory */
#define VFS_EINVAL    -22   /* Invalid argument */
#define VFS_EMFILE    -24   /* Too many open files */
#define VFS_EBUSY     -16   /* Resource busy */
#define VFS_ENOSPC    -28   /* No space left */
#define VFS_ENOTEMPTY -39   /* Directory not empty */

/* Maximum values */
#define VFS_NAME_MAX    255
#define VFS_PATH_MAX    4096
#define VFS_MAX_FDS     256
#define VFS_MAX_MOUNTS  32
#define VFS_MAX_FS      16

/* Forward declarations */
typedef struct vfs_node vfs_node_t;
typedef struct vfs_mount vfs_mount_t;
typedef struct file file_t;
typedef struct dirent dirent_t;
typedef struct fs_ops fs_ops_t;
typedef struct node_ops node_ops_t;
typedef struct vfs_stat vfs_stat_t;

/* Signed size type for read/write return values */
typedef int64_t ssize_t;

/* Directory entry (for readdir) */
struct dirent {
    char name[VFS_NAME_MAX + 1];
    uint32_t type;
    uint64_t inode;
};

/* File stat structure */
struct vfs_stat {
    uint64_t size;
    uint32_t type;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
};

/* Filesystem operations (per-filesystem type) */
struct fs_ops {
    const char *name;
    vfs_node_t *(*mount)(const char *source, uint32_t flags);
    int (*unmount)(vfs_mount_t *mount);
};

/* Node operations (per-node type) */
struct node_ops {
    int (*open)(vfs_node_t *node, uint32_t flags);
    void (*close)(vfs_node_t *node);
    ssize_t (*read)(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
    ssize_t (*write)(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
    int (*readdir)(vfs_node_t *node, dirent_t *dent, uint32_t index);
    vfs_node_t *(*finddir)(vfs_node_t *node, const char *name);
    int (*create)(vfs_node_t *parent, const char *name, uint32_t type);
    int (*unlink)(vfs_node_t *parent, const char *name);
    int (*mkdir)(vfs_node_t *parent, const char *name);
    int (*rmdir)(vfs_node_t *parent, const char *name);
    int (*rename)(vfs_node_t *old_parent, const char *old_name,
                  vfs_node_t *new_parent, const char *new_name);
    int (*truncate)(vfs_node_t *node, uint64_t size);
    int (*stat)(vfs_node_t *node, vfs_stat_t *st);
};

/* VFS node (inode equivalent) */
struct vfs_node {
    char name[VFS_NAME_MAX + 1];
    uint32_t type;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t nlink;
    uint32_t ref_count;
    uint32_t open_count;

    node_ops_t *ops;
    vfs_mount_t *mount;
    void *private;

    vfs_node_t *parent;
    vfs_node_t *children;       /* First child (for directories) */
    vfs_node_t *next;           /* Next sibling */
    vfs_node_t *prev;           /* Previous sibling */
};

/* Mount point */
struct vfs_mount {
    char path[VFS_PATH_MAX];
    vfs_node_t *root;
    vfs_node_t *mountpoint;
    fs_ops_t *fs;
    void *private;
    vfs_mount_t *next;
};

/* Open file descriptor */
struct file {
    vfs_node_t *node;
    uint64_t offset;
    uint32_t flags;
    uint32_t ref_count;
    uint32_t dir_index;     /* For readdir iteration */
};

/*
 * VFS Initialization
 */
void vfs_init(void);

/*
 * Filesystem Registration
 */
int vfs_register_fs(fs_ops_t *fs);
int vfs_unregister_fs(const char *name);
fs_ops_t *vfs_find_fs(const char *name);

/*
 * Mount Operations
 */
int vfs_mount(const char *source, const char *target, const char *fstype, uint32_t flags);
int vfs_unmount(const char *target);

/*
 * Path Operations
 */
vfs_node_t *vfs_lookup(const char *path);
vfs_node_t *vfs_lookup_parent(const char *path, char *name_out, size_t name_size);

/*
 * File Operations
 */
int vfs_open(const char *path, uint32_t flags);
void vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t size);
ssize_t vfs_write(int fd, const void *buf, size_t size);
int64_t vfs_seek(int fd, int64_t offset, int whence);
int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_fstat(int fd, vfs_stat_t *st);
int vfs_truncate(const char *path, uint64_t size);

/*
 * File Descriptor Duplication
 */
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);

/*
 * Directory Operations
 */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_readdir(int fd, dirent_t *dent);

/*
 * Node Management
 */
vfs_node_t *vfs_node_alloc(void);
void vfs_node_free(vfs_node_t *node);
void vfs_node_ref(vfs_node_t *node);
void vfs_node_unref(vfs_node_t *node);

/*
 * Debug
 */
void vfs_dump_tree(vfs_node_t *node, int depth);

#endif /* _KERNEL_VFS_H */
