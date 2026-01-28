/* ramfs.c - RAM filesystem implementation */

#include "ramfs.h"
#include "vfs.h"
#include "../lib/string.h"
#include "../mm/slab.h"
#include "../debug/debug.h"

/* Ramfs-specific inode data */
typedef struct ramfs_data {
    /* For files: data blocks */
    uint8_t **blocks;
    uint32_t block_count;
    uint32_t block_capacity;
} ramfs_data_t;

/* Slab cache for ramfs data */
static kmem_cache_t *ramfs_data_cache;

/* Forward declarations */
static node_ops_t ramfs_file_ops;
static node_ops_t ramfs_dir_ops;

/*
 * Helper: Allocate ramfs data
 */
static ramfs_data_t *ramfs_data_alloc(void) {
    ramfs_data_t *data = kmem_cache_alloc(ramfs_data_cache);
    if (data) {
        memset(data, 0, sizeof(ramfs_data_t));
    }
    return data;
}

/*
 * Helper: Free ramfs data and all blocks
 */
static void ramfs_data_free(ramfs_data_t *data) {
    if (!data) return;

    /* Free all data blocks */
    if (data->blocks) {
        for (uint32_t i = 0; i < data->block_count; i++) {
            if (data->blocks[i]) {
                kfree(data->blocks[i]);
            }
        }
        kfree(data->blocks);
    }

    kmem_cache_free(ramfs_data_cache, data);
}

/*
 * Helper: Ensure file has enough blocks
 */
static int ramfs_ensure_blocks(ramfs_data_t *data, uint32_t needed) {
    if (needed <= data->block_capacity) {
        return VFS_OK;
    }

    /* Grow block array */
    uint32_t new_capacity = data->block_capacity ? data->block_capacity * 2 : RAMFS_INITIAL_BLOCKS;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    uint8_t **new_blocks = kmalloc(new_capacity * sizeof(uint8_t *));
    if (!new_blocks) {
        return VFS_ENOMEM;
    }

    /* Copy existing pointers */
    if (data->blocks) {
        memcpy(new_blocks, data->blocks, data->block_count * sizeof(uint8_t *));
        kfree(data->blocks);
    }

    /* Zero new entries */
    for (uint32_t i = data->block_count; i < new_capacity; i++) {
        new_blocks[i] = NULL;
    }

    data->blocks = new_blocks;
    data->block_capacity = new_capacity;
    return VFS_OK;
}

/*
 * Helper: Create a new ramfs node
 */
static vfs_node_t *ramfs_create_node(const char *name, uint32_t type) {
    vfs_node_t *node = vfs_node_alloc();
    if (!node) {
        return NULL;
    }

    strncpy(node->name, name, VFS_NAME_MAX);
    node->type = type;
    node->permissions = 0755;
    node->uid = 0;
    node->gid = 0;
    node->size = 0;
    node->nlink = 1;

    if (type == VFS_DIR) {
        node->ops = &ramfs_dir_ops;
    } else {
        node->ops = &ramfs_file_ops;
        /* Allocate ramfs data for files */
        ramfs_data_t *data = ramfs_data_alloc();
        if (!data) {
            vfs_node_free(node);
            return NULL;
        }
        node->private = data;
    }

    return node;
}

/*
 * Helper: Add child to directory
 */
static void ramfs_add_child(vfs_node_t *parent, vfs_node_t *child) {
    child->parent = parent;
    child->next = parent->children;
    child->prev = NULL;

    if (parent->children) {
        parent->children->prev = child;
    }
    parent->children = child;
}

/*
 * Helper: Remove child from directory
 */
static void ramfs_remove_child(vfs_node_t *parent, vfs_node_t *child) {
    if (child->prev) {
        child->prev->next = child->next;
    } else {
        parent->children = child->next;
    }

    if (child->next) {
        child->next->prev = child->prev;
    }

    child->parent = NULL;
    child->next = NULL;
    child->prev = NULL;
}

/*
 * File Operations
 */
static int ramfs_file_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void ramfs_file_close(vfs_node_t *node) {
    (void)node;
}

static ssize_t ramfs_file_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    ramfs_data_t *data = (ramfs_data_t *)node->private;
    if (!data) {
        return VFS_EIO;
    }

    /* Check bounds */
    if (offset >= node->size) {
        return 0;
    }

    /* Limit read to available data */
    if (offset + size > node->size) {
        size = node->size - offset;
    }

    /* Read from blocks */
    uint8_t *dst = (uint8_t *)buf;
    size_t remaining = size;
    uint64_t pos = offset;

    while (remaining > 0) {
        uint32_t block_idx = pos / RAMFS_BLOCK_SIZE;
        uint32_t block_off = pos % RAMFS_BLOCK_SIZE;
        uint32_t chunk = RAMFS_BLOCK_SIZE - block_off;
        if (chunk > remaining) {
            chunk = remaining;
        }

        if (block_idx < data->block_count && data->blocks[block_idx]) {
            memcpy(dst, data->blocks[block_idx] + block_off, chunk);
        } else {
            /* Sparse block - return zeros */
            memset(dst, 0, chunk);
        }

        dst += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    return size;
}

static ssize_t ramfs_file_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    ramfs_data_t *data = (ramfs_data_t *)node->private;
    if (!data) {
        return VFS_EIO;
    }

    /* Calculate blocks needed */
    uint64_t end_offset = offset + size;
    uint32_t blocks_needed = (end_offset + RAMFS_BLOCK_SIZE - 1) / RAMFS_BLOCK_SIZE;

    /* Ensure we have enough block pointers */
    int err = ramfs_ensure_blocks(data, blocks_needed);
    if (err != VFS_OK) {
        return err;
    }

    /* Write to blocks */
    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = size;
    uint64_t pos = offset;

    while (remaining > 0) {
        uint32_t block_idx = pos / RAMFS_BLOCK_SIZE;
        uint32_t block_off = pos % RAMFS_BLOCK_SIZE;
        uint32_t chunk = RAMFS_BLOCK_SIZE - block_off;
        if (chunk > remaining) {
            chunk = remaining;
        }

        /* Allocate block if needed */
        if (block_idx >= data->block_count || !data->blocks[block_idx]) {
            uint8_t *new_block = kmalloc(RAMFS_BLOCK_SIZE);
            if (!new_block) {
                /* Return partial write */
                if (pos > offset) {
                    if (pos > node->size) {
                        node->size = pos;
                    }
                    return pos - offset;
                }
                return VFS_ENOMEM;
            }
            memset(new_block, 0, RAMFS_BLOCK_SIZE);
            data->blocks[block_idx] = new_block;
            if (block_idx >= data->block_count) {
                data->block_count = block_idx + 1;
            }
        }

        memcpy(data->blocks[block_idx] + block_off, src, chunk);

        src += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    /* Update size if we grew the file */
    if (end_offset > node->size) {
        node->size = end_offset;
    }

    return size;
}

static int ramfs_file_truncate(vfs_node_t *node, uint64_t size) {
    ramfs_data_t *data = (ramfs_data_t *)node->private;
    if (!data) {
        return VFS_EIO;
    }

    if (size < node->size) {
        /* Shrinking - free excess blocks */
        uint32_t new_blocks = (size + RAMFS_BLOCK_SIZE - 1) / RAMFS_BLOCK_SIZE;
        for (uint32_t i = new_blocks; i < data->block_count; i++) {
            if (data->blocks[i]) {
                kfree(data->blocks[i]);
                data->blocks[i] = NULL;
            }
        }
        data->block_count = new_blocks;
    }
    /* Growing is handled lazily on write */

    node->size = size;
    return VFS_OK;
}

static int ramfs_file_stat(vfs_node_t *node, vfs_stat_t *st) {
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

/*
 * Directory Operations
 */
static int ramfs_dir_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void ramfs_dir_close(vfs_node_t *node) {
    (void)node;
}

static int ramfs_dir_readdir(vfs_node_t *node, dirent_t *dent, uint32_t index) {
    vfs_node_t *child = node->children;
    uint32_t i = 0;

    while (child) {
        if (i == index) {
            strncpy(dent->name, child->name, VFS_NAME_MAX);
            dent->type = child->type;
            dent->inode = (uint64_t)(uintptr_t)child;  /* Use pointer as inode for ramfs */
            return VFS_OK;
        }
        child = child->next;
        i++;
    }

    return VFS_ENOENT;  /* No more entries */
}

static vfs_node_t *ramfs_dir_finddir(vfs_node_t *node, const char *name) {
    vfs_node_t *child = node->children;

    while (child) {
        if (strcmp(child->name, name) == 0) {
            vfs_node_ref(child);
            return child;
        }
        child = child->next;
    }

    return NULL;
}

static int ramfs_dir_create(vfs_node_t *parent, const char *name, uint32_t type) {
    /* Check if already exists */
    if (ramfs_dir_finddir(parent, name)) {
        return VFS_EEXIST;
    }

    vfs_node_t *node = ramfs_create_node(name, type);
    if (!node) {
        return VFS_ENOMEM;
    }

    node->mount = parent->mount;
    ramfs_add_child(parent, node);

    return VFS_OK;
}

static int ramfs_dir_unlink(vfs_node_t *parent, const char *name) {
    vfs_node_t *child = parent->children;
    vfs_node_t *node = NULL;

    /* Find the child */
    while (child) {
        if (strcmp(child->name, name) == 0) {
            node = child;
            break;
        }
        child = child->next;
    }

    if (!node) {
        return VFS_ENOENT;
    }

    /* Can't unlink directories with unlink */
    if (node->type == VFS_DIR) {
        return VFS_EISDIR;
    }

    /* Can't unlink if still open */
    if (node->open_count > 0) {
        return VFS_EBUSY;
    }

    /* Remove from parent */
    ramfs_remove_child(parent, node);

    /* Free resources */
    if (node->private) {
        ramfs_data_free((ramfs_data_t *)node->private);
    }
    vfs_node_unref(node);

    return VFS_OK;
}

static int ramfs_dir_mkdir(vfs_node_t *parent, const char *name) {
    return ramfs_dir_create(parent, name, VFS_DIR);
}

static int ramfs_dir_rmdir(vfs_node_t *parent, const char *name) {
    vfs_node_t *child = parent->children;
    vfs_node_t *node = NULL;

    /* Find the child */
    while (child) {
        if (strcmp(child->name, name) == 0) {
            node = child;
            break;
        }
        child = child->next;
    }

    if (!node) {
        return VFS_ENOENT;
    }

    /* Must be a directory */
    if (node->type != VFS_DIR) {
        return VFS_ENOTDIR;
    }

    /* Must be empty */
    if (node->children != NULL) {
        return VFS_ENOTEMPTY;
    }

    /* Can't remove if still open */
    if (node->open_count > 0) {
        return VFS_EBUSY;
    }

    /* Remove from parent */
    ramfs_remove_child(parent, node);
    vfs_node_unref(node);

    return VFS_OK;
}

/*
 * Operation tables
 */
static node_ops_t ramfs_file_ops = {
    .open = ramfs_file_open,
    .close = ramfs_file_close,
    .read = ramfs_file_read,
    .write = ramfs_file_write,
    .truncate = ramfs_file_truncate,
    .stat = ramfs_file_stat,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
};

static node_ops_t ramfs_dir_ops = {
    .open = ramfs_dir_open,
    .close = ramfs_dir_close,
    .read = NULL,
    .write = NULL,
    .truncate = NULL,
    .stat = ramfs_file_stat,
    .readdir = ramfs_dir_readdir,
    .finddir = ramfs_dir_finddir,
    .create = ramfs_dir_create,
    .unlink = ramfs_dir_unlink,
    .mkdir = ramfs_dir_mkdir,
    .rmdir = ramfs_dir_rmdir,
};

/*
 * Mount operation
 */
static vfs_node_t *ramfs_mount(const char *source, uint32_t flags) {
    (void)source;
    (void)flags;

    /* Create root directory */
    vfs_node_t *root = ramfs_create_node("", VFS_DIR);
    if (!root) {
        return NULL;
    }

    return root;
}

static int ramfs_unmount(vfs_mount_t *mount) {
    /* TODO: Free all nodes recursively */
    (void)mount;
    return VFS_OK;
}

/*
 * Filesystem operations
 */
static fs_ops_t ramfs_fs_ops = {
    .name = "ramfs",
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
};

/*
 * Initialize ramfs
 */
void ramfs_init(void) {
    INFO("Initializing ramfs...");

    /* Create slab cache for ramfs data */
    ramfs_data_cache = kmem_cache_create("ramfs_data", sizeof(ramfs_data_t), 8, SLAB_ZERO);
    if (!ramfs_data_cache) {
        panic("ramfs: Failed to create data cache");
    }

    /* Register with VFS */
    int err = vfs_register_fs(&ramfs_fs_ops);
    if (err != VFS_OK) {
        panic("ramfs: Failed to register filesystem: %d", err);
    }

    INFO("ramfs initialized");
}
