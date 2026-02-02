/* vfs.c - Virtual File System implementation */

#include "vfs.h"
#include "fd_table.h"
#include "../lib/string.h"
#include "../mm/slab.h"
#include "../debug/debug.h"
#include "../sync/spinlock.h"
#include "../process/process.h"

/* Slab caches */
static kmem_cache_t *node_cache;
static kmem_cache_t *file_cache;
static kmem_cache_t *mount_cache;

/* Registered filesystems */
static fs_ops_t *registered_fs[VFS_MAX_FS];
static int num_registered_fs = 0;
static spinlock_t fs_lock = SPINLOCK_INIT;

/* Mount points */
static vfs_mount_t *mount_list = NULL;
static spinlock_t mount_lock = SPINLOCK_INIT;

/* Root node */
static vfs_node_t *vfs_root = NULL;

/* File descriptor table (kernel-wide for now) */
static file_t *fd_table[VFS_MAX_FDS];
static spinlock_t fd_lock = SPINLOCK_INIT;

/*
 * Initialization
 */
void vfs_init(void) {
    INFO("Initializing VFS...");

    /* Explicitly initialize static variables in case static init failed */
    num_registered_fs = 0;
    spin_init(&fs_lock);
    mount_list = NULL;
    spin_init(&mount_lock);
    vfs_root = NULL;
    spin_init(&fd_lock);

    /* Clear registered filesystems array */
    for (int i = 0; i < VFS_MAX_FS; i++) {
        registered_fs[i] = NULL;
    }

    /* Clear FD table */
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        fd_table[i] = NULL;
    }

    /* Create slab caches */
    node_cache = kmem_cache_create("vfs_node", sizeof(vfs_node_t), 8, SLAB_ZERO);
    file_cache = kmem_cache_create("vfs_file", sizeof(file_t), 8, SLAB_ZERO);
    mount_cache = kmem_cache_create("vfs_mount", sizeof(vfs_mount_t), 8, SLAB_ZERO);

    if (!node_cache || !file_cache || !mount_cache) {
        panic("VFS: Failed to create slab caches");
    }

    INFO("VFS initialized");
}

/*
 * Node Management
 */
vfs_node_t *vfs_node_alloc(void) {
    vfs_node_t *node = kmem_cache_alloc(node_cache);
    if (node) {
        memset(node, 0, sizeof(vfs_node_t));
        node->ref_count = 1;
        node->nlink = 1;
    }
    return node;
}

void vfs_node_free(vfs_node_t *node) {
    if (node) {
        kmem_cache_free(node_cache, node);
    }
}

void vfs_node_ref(vfs_node_t *node) {
    if (node) {
        node->ref_count++;
    }
}

void vfs_node_unref(vfs_node_t *node) {
    if (node && --node->ref_count == 0) {
        vfs_node_free(node);
    }
}

/*
 * Filesystem Registration
 */
int vfs_register_fs(fs_ops_t *fs) {
    if (!fs || !fs->name) {
        return VFS_EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&fs_lock);

    /* Check if already registered */
    for (int i = 0; i < num_registered_fs; i++) {
        if (strcmp(registered_fs[i]->name, fs->name) == 0) {
            spin_unlock_irqrestore(&fs_lock, flags);
            return VFS_EEXIST;
        }
    }

    if (num_registered_fs >= VFS_MAX_FS) {
        spin_unlock_irqrestore(&fs_lock, flags);
        return VFS_ENOMEM;
    }

    registered_fs[num_registered_fs++] = fs;
    spin_unlock_irqrestore(&fs_lock, flags);

    INFO("VFS: Registered filesystem '%s'", fs->name);
    return VFS_OK;
}

int vfs_unregister_fs(const char *name) {
    if (!name) {
        return VFS_EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&fs_lock);

    for (int i = 0; i < num_registered_fs; i++) {
        if (strcmp(registered_fs[i]->name, name) == 0) {
            /* Shift remaining entries */
            for (int j = i; j < num_registered_fs - 1; j++) {
                registered_fs[j] = registered_fs[j + 1];
            }
            num_registered_fs--;
            spin_unlock_irqrestore(&fs_lock, flags);
            return VFS_OK;
        }
    }

    spin_unlock_irqrestore(&fs_lock, flags);
    return VFS_ENOENT;
}

fs_ops_t *vfs_find_fs(const char *name) {
    uint64_t flags = spin_lock_irqsave(&fs_lock);

    for (int i = 0; i < num_registered_fs; i++) {
        if (strcmp(registered_fs[i]->name, name) == 0) {
            fs_ops_t *fs = registered_fs[i];
            spin_unlock_irqrestore(&fs_lock, flags);
            return fs;
        }
    }

    spin_unlock_irqrestore(&fs_lock, flags);
    return NULL;
}

/*
 * Mount Operations
 */
int vfs_mount(const char *source, const char *target, const char *fstype, uint32_t flags) {
    fs_ops_t *fs = vfs_find_fs(fstype);
    if (!fs) {
        ERROR("VFS: Unknown filesystem type '%s'", fstype);
        return VFS_EINVAL;
    }

    if (!fs->mount) {
        return VFS_EINVAL;
    }

    /* Create mount point */
    vfs_mount_t *mnt = kmem_cache_alloc(mount_cache);
    if (!mnt) {
        return VFS_ENOMEM;
    }
    memset(mnt, 0, sizeof(vfs_mount_t));

    /* Mount the filesystem */
    vfs_node_t *root = fs->mount(source, flags);
    if (!root) {
        kmem_cache_free(mount_cache, mnt);
        return VFS_EIO;
    }

    strncpy(mnt->path, target, VFS_PATH_MAX - 1);
    mnt->root = root;
    mnt->fs = fs;
    root->mount = mnt;

    uint64_t irqflags = spin_lock_irqsave(&mount_lock);

    /* Special case: mounting root */
    if (strcmp(target, "/") == 0) {
        vfs_root = root;
    }

    /* Add to mount list */
    mnt->next = mount_list;
    mount_list = mnt;

    spin_unlock_irqrestore(&mount_lock, irqflags);

    INFO("VFS: Mounted %s at %s (type: %s)", source ? source : "none", target, fstype);
    return VFS_OK;
}

int vfs_unmount(const char *target) {
    uint64_t flags = spin_lock_irqsave(&mount_lock);

    vfs_mount_t *prev = NULL;
    vfs_mount_t *mnt = mount_list;

    while (mnt) {
        if (strcmp(mnt->path, target) == 0) {
            /* Remove from list */
            if (prev) {
                prev->next = mnt->next;
            } else {
                mount_list = mnt->next;
            }

            spin_unlock_irqrestore(&mount_lock, flags);

            /* Unmount callback */
            if (mnt->fs && mnt->fs->unmount) {
                mnt->fs->unmount(mnt);
            }

            /* Clear root if unmounting / */
            if (strcmp(target, "/") == 0) {
                vfs_root = NULL;
            }

            kmem_cache_free(mount_cache, mnt);
            return VFS_OK;
        }
        prev = mnt;
        mnt = mnt->next;
    }

    spin_unlock_irqrestore(&mount_lock, flags);
    return VFS_ENOENT;
}

/*
 * Path Resolution
 */
static vfs_node_t *lookup_in_dir(vfs_node_t *dir, const char *name) {
    if (!dir || !name || dir->type != VFS_DIR) {
        return NULL;
    }

    /* Handle . and .. */
    if (strcmp(name, ".") == 0) {
        vfs_node_ref(dir);
        return dir;
    }
    if (strcmp(name, "..") == 0) {
        vfs_node_t *parent = dir->parent ? dir->parent : dir;
        vfs_node_ref(parent);
        return parent;
    }

    /* Use finddir operation if available */
    if (dir->ops && dir->ops->finddir) {
        return dir->ops->finddir(dir, name);
    }

    return NULL;
}

vfs_node_t *vfs_lookup(const char *path) {
    if (!path || !vfs_root) {
        return NULL;
    }

    /* Handle empty path or just "/" */
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        vfs_node_ref(vfs_root);
        return vfs_root;
    }

    /* Start from root for absolute paths */
    vfs_node_t *current;
    const char *p = path;

    if (path[0] == '/') {
        current = vfs_root;
        p++;
    } else {
        /* Relative paths not supported yet (no current directory) */
        current = vfs_root;
    }
    vfs_node_ref(current);

    /* Parse path components */
    char name[VFS_NAME_MAX + 1];
    while (*p) {
        /* Skip leading slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract component name */
        size_t len = 0;
        while (p[len] && p[len] != '/' && len < VFS_NAME_MAX) {
            name[len] = p[len];
            len++;
        }
        name[len] = '\0';
        p += len;

        /* Look up this component */
        vfs_node_t *next = lookup_in_dir(current, name);
        vfs_node_unref(current);

        if (!next) {
            return NULL;
        }

        current = next;
    }

    return current;
}

vfs_node_t *vfs_lookup_parent(const char *path, char *name_out, size_t name_size) {
    if (!path || !name_out || name_size == 0) {
        return NULL;
    }

    /* Find the last component */
    const char *last_slash = strrchr(path, '/');
    const char *name;
    char parent_path[VFS_PATH_MAX];

    if (!last_slash) {
        /* No slash - parent is root, name is the whole path */
        strncpy(name_out, path, name_size - 1);
        name_out[name_size - 1] = '\0';
        return vfs_lookup("/");
    }

    if (last_slash == path) {
        /* Slash at start - parent is root */
        strncpy(name_out, last_slash + 1, name_size - 1);
        name_out[name_size - 1] = '\0';
        return vfs_lookup("/");
    }

    /* Copy parent path */
    size_t parent_len = last_slash - path;
    if (parent_len >= VFS_PATH_MAX) {
        parent_len = VFS_PATH_MAX - 1;
    }
    memcpy(parent_path, path, parent_len);
    parent_path[parent_len] = '\0';

    /* Copy name */
    name = last_slash + 1;
    strncpy(name_out, name, name_size - 1);
    name_out[name_size - 1] = '\0';

    return vfs_lookup(parent_path);
}

/*
 * File Descriptor Management
 */

/*
 * Get the current process's fd table
 * Falls back to global fd_table if process subsystem not initialized
 */
static fd_table_t *get_current_fd_table(void) {
    if (process_is_initialized()) {
        process_t *proc = process_current();
        if (proc && proc->fd_table) {
            return proc->fd_table;
        }
    }
    /* Fallback: process subsystem not yet initialized, use kernel's table */
    return NULL;
}

static int alloc_fd(file_t *file) {
    fd_table_t *fdt = get_current_fd_table();
    if (fdt) {
        return fd_table_alloc(fdt, file, 0);
    }

    /* Fallback to global table */
    uint64_t flags = spin_lock_irqsave(&fd_lock);

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (fd_table[i] == NULL) {
            fd_table[i] = file;
            spin_unlock_irqrestore(&fd_lock, flags);
            return i;
        }
    }

    spin_unlock_irqrestore(&fd_lock, flags);
    return -1;
}

static void free_fd(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) {
        return;
    }

    fd_table_t *fdt = get_current_fd_table();
    if (fdt) {
        fd_table_free(fdt, fd);
        return;
    }

    /* Fallback to global table */
    uint64_t flags = spin_lock_irqsave(&fd_lock);
    fd_table[fd] = NULL;
    spin_unlock_irqrestore(&fd_lock, flags);
}

static file_t *get_file(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) {
        return NULL;
    }

    fd_table_t *fdt = get_current_fd_table();
    if (fdt) {
        return fd_table_get(fdt, fd);
    }

    /* Fallback to global table */
    return fd_table[fd];
}

/*
 * File Operations
 */
int vfs_open(const char *path, uint32_t flags) {
    if (!path) {
        return VFS_EINVAL;
    }

    vfs_node_t *node = vfs_lookup(path);

    /* Handle O_CREAT */
    if (!node && (flags & O_CREAT)) {
        char name[VFS_NAME_MAX + 1];
        vfs_node_t *parent = vfs_lookup_parent(path, name, sizeof(name));
        if (!parent) {
            return VFS_ENOENT;
        }

        if (parent->type != VFS_DIR) {
            vfs_node_unref(parent);
            return VFS_ENOTDIR;
        }

        if (parent->ops && parent->ops->create) {
            int err = parent->ops->create(parent, name, VFS_FILE);
            vfs_node_unref(parent);
            if (err != VFS_OK) {
                return err;
            }
            node = vfs_lookup(path);
        } else {
            vfs_node_unref(parent);
            return VFS_EIO;
        }
    }

    if (!node) {
        return VFS_ENOENT;
    }

    /* Check O_DIRECTORY */
    if ((flags & O_DIRECTORY) && node->type != VFS_DIR) {
        vfs_node_unref(node);
        return VFS_ENOTDIR;
    }

    /* Create file structure first */
    file_t *file = kmem_cache_alloc(file_cache);
    if (!file) {
        vfs_node_unref(node);
        return VFS_ENOMEM;
    }

    memset(file, 0, sizeof(file_t));
    file->node = node;
    file->flags = flags;
    file->offset = 0;
    file->ref_count = 1;
    file->dir_index = 0;

    /* Allocate file descriptor */
    int fd = alloc_fd(file);
    if (fd < 0) {
        kmem_cache_free(file_cache, file);
        vfs_node_unref(node);
        return VFS_EMFILE;
    }

    /* Call open callback */
    if (node->ops && node->ops->open) {
        int err = node->ops->open(node, flags);
        if (err != VFS_OK) {
            free_fd(fd);
            kmem_cache_free(file_cache, file);
            vfs_node_unref(node);
            return err;
        }
    }

    node->open_count++;

    /* Handle O_TRUNC */
    if ((flags & O_TRUNC) && node->type == VFS_FILE) {
        if (node->ops && node->ops->truncate) {
            node->ops->truncate(node, 0);
        }
    }

    /* Handle O_APPEND */
    if (flags & O_APPEND) {
        file->offset = node->size;
    }

    return fd;
}

void vfs_close(int fd) {
    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1) {
        return;
    }

    vfs_node_t *node = file->node;

    /* Call close callback */
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }

    if (node) {
        node->open_count--;
        vfs_node_unref(node);
    }

    kmem_cache_free(file_cache, file);
    free_fd(fd);
}

ssize_t vfs_read(int fd, void *buf, size_t size) {
    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1 || !buf) {
        return VFS_EBADF;
    }

    vfs_node_t *node = file->node;
    if (!node) {
        return VFS_EBADF;
    }

    /* Check read permission */
    if ((file->flags & O_ACCMODE) == O_WRONLY) {
        return VFS_EBADF;
    }

    /* Directories can't be read like files */
    if (node->type == VFS_DIR) {
        return VFS_EISDIR;
    }

    if (!node->ops || !node->ops->read) {
        return VFS_EIO;
    }

    ssize_t bytes = node->ops->read(node, buf, size, file->offset);
    if (bytes > 0) {
        file->offset += bytes;
    }

    return bytes;
}

ssize_t vfs_write(int fd, const void *buf, size_t size) {
    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1 || !buf) {
        return VFS_EBADF;
    }

    vfs_node_t *node = file->node;
    if (!node) {
        return VFS_EBADF;
    }

    /* Check write permission */
    if ((file->flags & O_ACCMODE) == O_RDONLY) {
        return VFS_EBADF;
    }

    /* Directories can't be written */
    if (node->type == VFS_DIR) {
        return VFS_EISDIR;
    }

    if (!node->ops || !node->ops->write) {
        return VFS_EIO;
    }

    /* Handle append mode */
    if (file->flags & O_APPEND) {
        file->offset = node->size;
    }

    ssize_t bytes = node->ops->write(node, buf, size, file->offset);
    if (bytes > 0) {
        file->offset += bytes;
    }

    return bytes;
}

int64_t vfs_seek(int fd, int64_t offset, int whence) {
    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    vfs_node_t *node = file->node;
    if (!node) {
        return VFS_EBADF;
    }

    int64_t new_offset;
    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = (int64_t)file->offset + offset;
        break;
    case SEEK_END:
        new_offset = (int64_t)node->size + offset;
        break;
    default:
        return VFS_EINVAL;
    }

    if (new_offset < 0) {
        return VFS_EINVAL;
    }

    file->offset = (uint64_t)new_offset;
    return new_offset;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) {
        return VFS_EINVAL;
    }

    vfs_node_t *node = vfs_lookup(path);
    if (!node) {
        return VFS_ENOENT;
    }

    st->size = node->size;
    st->type = node->type;
    st->permissions = node->permissions;
    st->uid = node->uid;
    st->gid = node->gid;
    st->nlink = node->nlink;
    st->atime = node->atime;
    st->mtime = node->mtime;
    st->ctime = node->ctime;

    vfs_node_unref(node);
    return VFS_OK;
}

int vfs_fstat(int fd, vfs_stat_t *st) {
    if (!st) {
        return VFS_EINVAL;
    }

    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    vfs_node_t *node = file->node;
    if (!node) {
        return VFS_EBADF;
    }

    st->size = node->size;
    st->type = node->type;
    st->permissions = node->permissions;
    st->uid = node->uid;
    st->gid = node->gid;
    st->nlink = node->nlink;
    st->atime = node->atime;
    st->mtime = node->mtime;
    st->ctime = node->ctime;

    return VFS_OK;
}

int vfs_truncate(const char *path, uint64_t size) {
    vfs_node_t *node = vfs_lookup(path);
    if (!node) {
        return VFS_ENOENT;
    }

    if (node->type != VFS_FILE) {
        vfs_node_unref(node);
        return VFS_EISDIR;
    }

    int err = VFS_EIO;
    if (node->ops && node->ops->truncate) {
        err = node->ops->truncate(node, size);
    }

    vfs_node_unref(node);
    return err;
}

int vfs_ioctl(int fd, unsigned long request, void *arg) {
    if (fd < 0 || fd >= VFS_MAX_FDS) {
        return VFS_EBADF;
    }

    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    if (!file->node) {
        return VFS_EBADF;
    }

    if (file->node->ops && file->node->ops->ioctl) {
        return file->node->ops->ioctl(file->node, request, arg);
    }

    return VFS_EINVAL;  /* No ioctl handler */
}

/*
 * File Descriptor Duplication
 */
int vfs_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS) {
        return VFS_EBADF;
    }

    file_t *file = get_file(oldfd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    /* Increment reference counts */
    file->ref_count++;
    if (file->node) {
        vfs_node_ref(file->node);
    }

    /* Allocate new fd for same file */
    int newfd = alloc_fd(file);
    if (newfd < 0) {
        /* Rollback ref count increments */
        file->ref_count--;
        if (file->node) {
            vfs_node_unref(file->node);
        }
        return VFS_EMFILE;
    }

    return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS ||
        newfd < 0 || newfd >= VFS_MAX_FDS) {
        return VFS_EBADF;
    }

    /* If same, just return newfd */
    if (oldfd == newfd) {
        file_t *file = get_file(oldfd);
        if (!file || file == (file_t *)1) {
            return VFS_EBADF;
        }
        return newfd;
    }

    file_t *file = get_file(oldfd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    /* Close newfd if it's open */
    file_t *old_file = get_file(newfd);
    if (old_file && old_file != (file_t *)1) {
        vfs_close(newfd);
    }

    /* Increment reference counts */
    file->ref_count++;
    if (file->node) {
        vfs_node_ref(file->node);
    }

    /* Allocate at specific fd */
    fd_table_t *fdt = get_current_fd_table();
    if (fdt) {
        int result = fd_table_alloc_at(fdt, newfd, file, 0);
        if (result < 0) {
            /* Rollback */
            file->ref_count--;
            if (file->node) {
                vfs_node_unref(file->node);
            }
            return VFS_EBADF;
        }
    } else {
        /* Fallback to global table */
        uint64_t flags = spin_lock_irqsave(&fd_lock);
        fd_table[newfd] = file;
        spin_unlock_irqrestore(&fd_lock, flags);
    }

    return newfd;
}

/*
 * Directory Operations
 */
int vfs_mkdir(const char *path) {
    if (!path) {
        return VFS_EINVAL;
    }

    char name[VFS_NAME_MAX + 1];
    vfs_node_t *parent = vfs_lookup_parent(path, name, sizeof(name));
    if (!parent) {
        return VFS_ENOENT;
    }

    if (parent->type != VFS_DIR) {
        vfs_node_unref(parent);
        return VFS_ENOTDIR;
    }

    int err = VFS_EIO;
    if (parent->ops && parent->ops->mkdir) {
        err = parent->ops->mkdir(parent, name);
    }

    vfs_node_unref(parent);
    return err;
}

int vfs_rmdir(const char *path) {
    if (!path) {
        return VFS_EINVAL;
    }

    char name[VFS_NAME_MAX + 1];
    vfs_node_t *parent = vfs_lookup_parent(path, name, sizeof(name));
    if (!parent) {
        return VFS_ENOENT;
    }

    if (parent->type != VFS_DIR) {
        vfs_node_unref(parent);
        return VFS_ENOTDIR;
    }

    int err = VFS_EIO;
    if (parent->ops && parent->ops->rmdir) {
        err = parent->ops->rmdir(parent, name);
    }

    vfs_node_unref(parent);
    return err;
}

int vfs_unlink(const char *path) {
    if (!path) {
        return VFS_EINVAL;
    }

    char name[VFS_NAME_MAX + 1];
    vfs_node_t *parent = vfs_lookup_parent(path, name, sizeof(name));
    if (!parent) {
        return VFS_ENOENT;
    }

    if (parent->type != VFS_DIR) {
        vfs_node_unref(parent);
        return VFS_ENOTDIR;
    }

    int err = VFS_EIO;
    if (parent->ops && parent->ops->unlink) {
        err = parent->ops->unlink(parent, name);
    }

    vfs_node_unref(parent);
    return err;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        return VFS_EINVAL;
    }

    /* Get old parent and name */
    char old_name[VFS_NAME_MAX + 1];
    vfs_node_t *old_parent = vfs_lookup_parent(oldpath, old_name, sizeof(old_name));
    if (!old_parent) {
        return VFS_ENOENT;
    }

    /* Get new parent and name */
    char new_name[VFS_NAME_MAX + 1];
    vfs_node_t *new_parent = vfs_lookup_parent(newpath, new_name, sizeof(new_name));
    if (!new_parent) {
        vfs_node_unref(old_parent);
        return VFS_ENOENT;
    }

    /* Both parents must be directories */
    if (old_parent->type != VFS_DIR || new_parent->type != VFS_DIR) {
        vfs_node_unref(old_parent);
        vfs_node_unref(new_parent);
        return VFS_ENOTDIR;
    }

    int err = VFS_EIO;
    if (old_parent->ops && old_parent->ops->rename) {
        err = old_parent->ops->rename(old_parent, old_name, new_parent, new_name);
    }

    vfs_node_unref(old_parent);
    vfs_node_unref(new_parent);
    return err;
}

int vfs_readdir(int fd, dirent_t *dent) {
    if (!dent) {
        return VFS_EINVAL;
    }

    file_t *file = get_file(fd);
    if (!file || file == (file_t *)1) {
        return VFS_EBADF;
    }

    vfs_node_t *node = file->node;
    if (!node) {
        return VFS_EBADF;
    }

    if (node->type != VFS_DIR) {
        return VFS_ENOTDIR;
    }

    if (!node->ops || !node->ops->readdir) {
        return VFS_EIO;
    }

    int err = node->ops->readdir(node, dent, file->dir_index);
    if (err == VFS_OK) {
        file->dir_index++;
    }

    return err;
}

/*
 * Symbolic Link Operations
 */
int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) {
        return VFS_EINVAL;
    }

    /* Get parent directory and link name */
    char name[VFS_NAME_MAX + 1];
    vfs_node_t *parent = vfs_lookup_parent(linkpath, name, sizeof(name));
    if (!parent) {
        return VFS_ENOENT;
    }

    if (parent->type != VFS_DIR) {
        vfs_node_unref(parent);
        return VFS_ENOTDIR;
    }

    /* Check if symlink already exists */
    vfs_node_t *existing = lookup_in_dir(parent, name);
    if (existing) {
        vfs_node_unref(existing);
        vfs_node_unref(parent);
        return VFS_EEXIST;
    }

    int err = VFS_EIO;
    if (parent->ops && parent->ops->symlink) {
        err = parent->ops->symlink(parent, name, target);
    }

    vfs_node_unref(parent);
    return err;
}

ssize_t vfs_readlink(const char *path, char *buf, size_t size) {
    if (!path || !buf || size == 0) {
        return VFS_EINVAL;
    }

    vfs_node_t *node = vfs_lookup(path);
    if (!node) {
        return VFS_ENOENT;
    }

    if (node->type != VFS_SYMLINK) {
        vfs_node_unref(node);
        return VFS_EINVAL;
    }

    ssize_t result = VFS_EIO;
    if (node->ops && node->ops->readlink) {
        result = node->ops->readlink(node, buf, size);
    }

    vfs_node_unref(node);
    return result;
}

/*
 * Debug
 */
void vfs_dump_tree(vfs_node_t *node, int depth) {
    if (!node) {
        if (vfs_root) {
            vfs_dump_tree(vfs_root, 0);
        }
        return;
    }

    /* Print indentation */
    for (int i = 0; i < depth; i++) {
        kprintf("  ");
    }

    /* Print node info */
    const char *type_str = "???";
    switch (node->type) {
    case VFS_FILE:    type_str = "FILE"; break;
    case VFS_DIR:     type_str = "DIR "; break;
    case VFS_CHARDEV: type_str = "CHR "; break;
    case VFS_BLKDEV:  type_str = "BLK "; break;
    case VFS_PIPE:    type_str = "PIPE"; break;
    case VFS_SYMLINK: type_str = "LNK "; break;
    }

    kprintf("[%s] %s (size=%llu, refs=%u)\n",
            type_str, node->name[0] ? node->name : "/",
            node->size, node->ref_count);

    /* Recurse into children */
    if (node->type == VFS_DIR) {
        vfs_node_t *child = node->children;
        while (child) {
            vfs_dump_tree(child, depth + 1);
            child = child->next;
        }
    }
}
