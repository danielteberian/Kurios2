/* ext2.c - ext2 filesystem implementation */

#include "ext2.h"
#include "vfs.h"
#include "../drivers/block.h"
#include "../lib/string.h"
#include "../mm/slab.h"
#include "../debug/debug.h"

/* Slab caches */
static kmem_cache_t *ext2_inode_info_cache;

/* Forward declarations */
static node_ops_t ext2_file_ops;
static node_ops_t ext2_dir_ops;
static vfs_node_t *ext2_mount(const char *source, uint32_t flags);
static int ext2_unmount(vfs_mount_t *mount);

/* Filesystem operations */
static fs_ops_t ext2_fs_ops = {
    .name = "ext2",
    .mount = ext2_mount,
    .unmount = ext2_unmount,
};

/*
 * Read blocks from the underlying device
 */
static int ext2_read_blocks(ext2_fs_t *fs, uint64_t block, uint32_t count, void *buf) {
    uint64_t sector = (block * fs->block_size) / BLOCK_SECTOR_SIZE;
    uint32_t sector_count = (count * fs->block_size) / BLOCK_SECTOR_SIZE;
    return block_read(fs->dev, sector, sector_count, buf);
}

/*
 * Write blocks to the underlying device
 */
static int ext2_write_blocks(ext2_fs_t *fs, uint64_t block, uint32_t count, const void *buf) {
    if (fs->read_only) {
        return -1;
    }
    uint64_t sector = (block * fs->block_size) / BLOCK_SECTOR_SIZE;
    uint32_t sector_count = (count * fs->block_size) / BLOCK_SECTOR_SIZE;
    return block_write(fs->dev, sector, sector_count, buf);
}

/*
 * Read a single block
 */
static int ext2_read_block(ext2_fs_t *fs, uint64_t block, void *buf) {
    return ext2_read_blocks(fs, block, 1, buf);
}

/*
 * Write a single block
 */
static int ext2_write_block(ext2_fs_t *fs, uint64_t block, const void *buf) {
    return ext2_write_blocks(fs, block, 1, buf);
}

/*
 * Read an inode from disk
 */
static int ext2_read_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0 || ino > fs->sb.s_inodes_count) {
        return VFS_EINVAL;
    }

    /* Calculate block group */
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t local_idx = (ino - 1) % fs->inodes_per_group;

    /* Get inode table block */
    uint32_t inode_table = fs->groups[group].bg_inode_table;
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = local_idx / inodes_per_block;
    uint32_t inode_offset = (local_idx % inodes_per_block) * fs->inode_size;

    /* Read the block containing the inode */
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    int err = ext2_read_block(fs, inode_table + block_offset, block_buf);
    if (err != 0) {
        kfree(block_buf);
        return VFS_EIO;
    }

    /* Copy inode data */
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    kfree(block_buf);

    return VFS_OK;
}

/*
 * Write an inode to disk
 */
static int ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const ext2_inode_t *inode) {
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    if (ino == 0 || ino > fs->sb.s_inodes_count) {
        return VFS_EINVAL;
    }

    /* Calculate block group */
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t local_idx = (ino - 1) % fs->inodes_per_group;

    /* Get inode table block */
    uint32_t inode_table = fs->groups[group].bg_inode_table;
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = local_idx / inodes_per_block;
    uint32_t inode_offset = (local_idx % inodes_per_block) * fs->inode_size;

    /* Read the block containing the inode */
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    int err = ext2_read_block(fs, inode_table + block_offset, block_buf);
    if (err != 0) {
        kfree(block_buf);
        return VFS_EIO;
    }

    /* Update inode data */
    memcpy(block_buf + inode_offset, inode, sizeof(ext2_inode_t));

    /* Write back */
    err = ext2_write_block(fs, inode_table + block_offset, block_buf);
    kfree(block_buf);

    return (err == 0) ? VFS_OK : VFS_EIO;
}

/*
 * Map a logical block number to a physical block number
 * Handles direct, indirect, double indirect, and triple indirect blocks
 */
static uint32_t ext2_get_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical) {
    uint32_t ptrs = fs->ptrs_per_block;
    uint32_t *block_buf = NULL;
    uint32_t result = 0;

    /* Direct blocks (0-11) */
    if (logical < EXT2_NDIR_BLOCKS) {
        return inode->i_block[logical];
    }
    logical -= EXT2_NDIR_BLOCKS;

    /* Single indirect (12 to ptrs+11) */
    if (logical < ptrs) {
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            return 0;
        }

        block_buf = kmalloc(fs->block_size);
        if (!block_buf) {
            return 0;
        }

        if (ext2_read_block(fs, inode->i_block[EXT2_IND_BLOCK], block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        result = block_buf[logical];
        kfree(block_buf);
        return result;
    }
    logical -= ptrs;

    /* Double indirect */
    if (logical < ptrs * ptrs) {
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            return 0;
        }

        block_buf = kmalloc(fs->block_size);
        if (!block_buf) {
            return 0;
        }

        /* Read first level */
        if (ext2_read_block(fs, inode->i_block[EXT2_DIND_BLOCK], block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        uint32_t ind_block = block_buf[logical / ptrs];
        if (ind_block == 0) {
            kfree(block_buf);
            return 0;
        }

        /* Read second level */
        if (ext2_read_block(fs, ind_block, block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        result = block_buf[logical % ptrs];
        kfree(block_buf);
        return result;
    }
    logical -= ptrs * ptrs;

    /* Triple indirect */
    if (logical < (uint64_t)ptrs * ptrs * ptrs) {
        if (inode->i_block[EXT2_TIND_BLOCK] == 0) {
            return 0;
        }

        block_buf = kmalloc(fs->block_size);
        if (!block_buf) {
            return 0;
        }

        /* Read first level */
        if (ext2_read_block(fs, inode->i_block[EXT2_TIND_BLOCK], block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        uint32_t dind_block = block_buf[logical / (ptrs * ptrs)];
        if (dind_block == 0) {
            kfree(block_buf);
            return 0;
        }

        /* Read second level */
        if (ext2_read_block(fs, dind_block, block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        uint32_t ind_block = block_buf[(logical / ptrs) % ptrs];
        if (ind_block == 0) {
            kfree(block_buf);
            return 0;
        }

        /* Read third level */
        if (ext2_read_block(fs, ind_block, block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }

        result = block_buf[logical % ptrs];
        kfree(block_buf);
        return result;
    }

    /* Block number too large */
    return 0;
}

/*
 * Allocate a new block from a block group
 */
static uint32_t ext2_alloc_block_in_group(ext2_fs_t *fs, uint32_t group) {
    if (fs->groups[group].bg_free_blocks_count == 0) {
        return 0;
    }

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) {
        return 0;
    }

    /* Read block bitmap */
    if (ext2_read_block(fs, fs->groups[group].bg_block_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return 0;
    }

    /* Find a free block */
    for (uint32_t i = 0; i < fs->blocks_per_group; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if ((bitmap[byte_idx] & (1 << bit_idx)) == 0) {
            /* Mark as used */
            bitmap[byte_idx] |= (1 << bit_idx);

            /* Write back bitmap */
            if (ext2_write_block(fs, fs->groups[group].bg_block_bitmap, bitmap) != 0) {
                kfree(bitmap);
                return 0;
            }

            /* Update group descriptor */
            fs->groups[group].bg_free_blocks_count--;
            fs->sb.s_free_blocks_count--;
            fs->dirty = true;

            kfree(bitmap);

            /* Return absolute block number */
            return group * fs->blocks_per_group + i + fs->first_data_block;
        }
    }

    kfree(bitmap);
    return 0;
}

/*
 * Allocate a new block
 */
static uint32_t ext2_alloc_block(ext2_fs_t *fs, uint32_t preferred_group) {
    /* Try preferred group first */
    uint32_t block = ext2_alloc_block_in_group(fs, preferred_group);
    if (block) {
        return block;
    }

    /* Search all groups */
    for (uint32_t g = 0; g < fs->group_count; g++) {
        if (g != preferred_group) {
            block = ext2_alloc_block_in_group(fs, g);
            if (block) {
                return block;
            }
        }
    }

    return 0;  /* No free blocks */
}

/*
 * Free a block
 */
static int ext2_free_block(ext2_fs_t *fs, uint32_t block) {
    if (block < fs->first_data_block || block >= fs->sb.s_blocks_count) {
        return VFS_EINVAL;
    }

    uint32_t group = (block - fs->first_data_block) / fs->blocks_per_group;
    uint32_t local = (block - fs->first_data_block) % fs->blocks_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) {
        return VFS_ENOMEM;
    }

    /* Read block bitmap */
    if (ext2_read_block(fs, fs->groups[group].bg_block_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return VFS_EIO;
    }

    /* Clear bit */
    uint32_t byte_idx = local / 8;
    uint32_t bit_idx = local % 8;
    bitmap[byte_idx] &= ~(1 << bit_idx);

    /* Write back */
    if (ext2_write_block(fs, fs->groups[group].bg_block_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return VFS_EIO;
    }

    /* Update counts */
    fs->groups[group].bg_free_blocks_count++;
    fs->sb.s_free_blocks_count++;
    fs->dirty = true;

    kfree(bitmap);
    return VFS_OK;
}

/*
 * Allocate a new inode from a block group
 */
static uint32_t ext2_alloc_inode_in_group(ext2_fs_t *fs, uint32_t group, bool is_dir) {
    if (fs->groups[group].bg_free_inodes_count == 0) {
        return 0;
    }

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) {
        return 0;
    }

    /* Read inode bitmap */
    if (ext2_read_block(fs, fs->groups[group].bg_inode_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return 0;
    }

    /* Find a free inode */
    for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if ((bitmap[byte_idx] & (1 << bit_idx)) == 0) {
            /* Mark as used */
            bitmap[byte_idx] |= (1 << bit_idx);

            /* Write back bitmap */
            if (ext2_write_block(fs, fs->groups[group].bg_inode_bitmap, bitmap) != 0) {
                kfree(bitmap);
                return 0;
            }

            /* Update group descriptor */
            fs->groups[group].bg_free_inodes_count--;
            if (is_dir) {
                fs->groups[group].bg_used_dirs_count++;
            }
            fs->sb.s_free_inodes_count--;
            fs->dirty = true;

            kfree(bitmap);

            /* Return absolute inode number (1-based) */
            return group * fs->inodes_per_group + i + 1;
        }
    }

    kfree(bitmap);
    return 0;
}

/*
 * Allocate a new inode
 */
static uint32_t ext2_alloc_inode(ext2_fs_t *fs, uint32_t preferred_group, bool is_dir) {
    /* Try preferred group first */
    uint32_t ino = ext2_alloc_inode_in_group(fs, preferred_group, is_dir);
    if (ino) {
        return ino;
    }

    /* Search all groups */
    for (uint32_t g = 0; g < fs->group_count; g++) {
        if (g != preferred_group) {
            ino = ext2_alloc_inode_in_group(fs, g, is_dir);
            if (ino) {
                return ino;
            }
        }
    }

    return 0;  /* No free inodes */
}

/*
 * Free an inode
 */
static int ext2_free_inode(ext2_fs_t *fs, uint32_t ino, bool is_dir) {
    if (ino < EXT2_FIRST_INO || ino > fs->sb.s_inodes_count) {
        return VFS_EINVAL;
    }

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t local = (ino - 1) % fs->inodes_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) {
        return VFS_ENOMEM;
    }

    /* Read inode bitmap */
    if (ext2_read_block(fs, fs->groups[group].bg_inode_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return VFS_EIO;
    }

    /* Clear bit */
    uint32_t byte_idx = local / 8;
    uint32_t bit_idx = local % 8;
    bitmap[byte_idx] &= ~(1 << bit_idx);

    /* Write back */
    if (ext2_write_block(fs, fs->groups[group].bg_inode_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return VFS_EIO;
    }

    /* Update counts */
    fs->groups[group].bg_free_inodes_count++;
    if (is_dir) {
        fs->groups[group].bg_used_dirs_count--;
    }
    fs->sb.s_free_inodes_count++;
    fs->dirty = true;

    kfree(bitmap);
    return VFS_OK;
}

/*
 * Set a block pointer in an inode
 * Allocates indirect blocks as needed
 */
static int ext2_set_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical, uint32_t phys_block, uint32_t inode_group) {
    uint32_t ptrs = fs->ptrs_per_block;

    /* Direct blocks (0-11) */
    if (logical < EXT2_NDIR_BLOCKS) {
        inode->i_block[logical] = phys_block;
        return VFS_OK;
    }
    logical -= EXT2_NDIR_BLOCKS;

    /* Single indirect */
    if (logical < ptrs) {
        /* Allocate indirect block if needed */
        if (inode->i_block[EXT2_IND_BLOCK] == 0) {
            uint32_t ind_block = ext2_alloc_block(fs, inode_group);
            if (ind_block == 0) {
                return VFS_ENOSPC;
            }

            /* Zero the block */
            uint8_t *zero_buf = kmalloc(fs->block_size);
            if (!zero_buf) {
                ext2_free_block(fs, ind_block);
                return VFS_ENOMEM;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ind_block, zero_buf);
            kfree(zero_buf);

            inode->i_block[EXT2_IND_BLOCK] = ind_block;
            inode->i_blocks += fs->block_size / 512;
        }

        uint32_t *block_buf = kmalloc(fs->block_size);
        if (!block_buf) {
            return VFS_ENOMEM;
        }

        ext2_read_block(fs, inode->i_block[EXT2_IND_BLOCK], block_buf);
        block_buf[logical] = phys_block;
        ext2_write_block(fs, inode->i_block[EXT2_IND_BLOCK], block_buf);
        kfree(block_buf);
        return VFS_OK;
    }
    logical -= ptrs;

    /* Double indirect */
    if (logical < ptrs * ptrs) {
        /* Allocate double indirect block if needed */
        if (inode->i_block[EXT2_DIND_BLOCK] == 0) {
            uint32_t dind_block = ext2_alloc_block(fs, inode_group);
            if (dind_block == 0) {
                return VFS_ENOSPC;
            }

            uint8_t *zero_buf = kmalloc(fs->block_size);
            if (!zero_buf) {
                ext2_free_block(fs, dind_block);
                return VFS_ENOMEM;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, dind_block, zero_buf);
            kfree(zero_buf);

            inode->i_block[EXT2_DIND_BLOCK] = dind_block;
            inode->i_blocks += fs->block_size / 512;
        }

        uint32_t *block_buf = kmalloc(fs->block_size);
        if (!block_buf) {
            return VFS_ENOMEM;
        }

        /* Read first level */
        ext2_read_block(fs, inode->i_block[EXT2_DIND_BLOCK], block_buf);

        uint32_t first_idx = logical / ptrs;
        if (block_buf[first_idx] == 0) {
            /* Allocate indirect block */
            uint32_t ind_block = ext2_alloc_block(fs, inode_group);
            if (ind_block == 0) {
                kfree(block_buf);
                return VFS_ENOSPC;
            }

            /* Zero the block */
            uint8_t *zero_buf = kmalloc(fs->block_size);
            if (!zero_buf) {
                ext2_free_block(fs, ind_block);
                kfree(block_buf);
                return VFS_ENOMEM;
            }
            memset(zero_buf, 0, fs->block_size);
            ext2_write_block(fs, ind_block, zero_buf);
            kfree(zero_buf);

            block_buf[first_idx] = ind_block;
            ext2_write_block(fs, inode->i_block[EXT2_DIND_BLOCK], block_buf);
            inode->i_blocks += fs->block_size / 512;
        }

        uint32_t ind_block = block_buf[first_idx];

        /* Read second level */
        ext2_read_block(fs, ind_block, block_buf);
        block_buf[logical % ptrs] = phys_block;
        ext2_write_block(fs, ind_block, block_buf);
        kfree(block_buf);
        return VFS_OK;
    }

    /* Triple indirect not implemented - files would need to be > 4GB */
    return VFS_EINVAL;
}

/*
 * Convert ext2 file type to VFS file type
 */
static uint32_t ext2_mode_to_vfs_type(uint16_t mode) {
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG:  return VFS_FILE;
        case EXT2_S_IFDIR:  return VFS_DIR;
        case EXT2_S_IFLNK:  return VFS_SYMLINK;
        case EXT2_S_IFCHR:  return VFS_CHARDEV;
        case EXT2_S_IFBLK:  return VFS_BLKDEV;
        case EXT2_S_IFIFO:  return VFS_PIPE;
        case EXT2_S_IFSOCK: return VFS_SOCKET;
        default:            return VFS_FILE;
    }
}

/*
 * Convert ext2 directory entry type to VFS type
 */
static uint32_t ext2_ftype_to_vfs_type(uint8_t file_type) {
    switch (file_type) {
        case EXT2_FT_REG_FILE:  return VFS_FILE;
        case EXT2_FT_DIR:       return VFS_DIR;
        case EXT2_FT_SYMLINK:   return VFS_SYMLINK;
        case EXT2_FT_CHRDEV:    return VFS_CHARDEV;
        case EXT2_FT_BLKDEV:    return VFS_BLKDEV;
        case EXT2_FT_FIFO:      return VFS_PIPE;
        case EXT2_FT_SOCK:      return VFS_SOCKET;
        default:                return VFS_FILE;
    }
}

/*
 * Convert VFS type to ext2 file type for directory entries
 */
static uint8_t vfs_type_to_ext2_ftype(uint32_t vfs_type) {
    switch (vfs_type) {
        case VFS_FILE:    return EXT2_FT_REG_FILE;
        case VFS_DIR:     return EXT2_FT_DIR;
        case VFS_SYMLINK: return EXT2_FT_SYMLINK;
        case VFS_CHARDEV: return EXT2_FT_CHRDEV;
        case VFS_BLKDEV:  return EXT2_FT_BLKDEV;
        case VFS_PIPE:    return EXT2_FT_FIFO;
        case VFS_SOCKET:  return EXT2_FT_SOCK;
        default:          return EXT2_FT_UNKNOWN;
    }
}

/*
 * Convert VFS type to ext2 i_mode
 */
static uint16_t vfs_type_to_ext2_mode(uint32_t vfs_type) {
    switch (vfs_type) {
        case VFS_FILE:    return EXT2_S_IFREG;
        case VFS_DIR:     return EXT2_S_IFDIR;
        case VFS_SYMLINK: return EXT2_S_IFLNK;
        case VFS_CHARDEV: return EXT2_S_IFCHR;
        case VFS_BLKDEV:  return EXT2_S_IFBLK;
        case VFS_PIPE:    return EXT2_S_IFIFO;
        case VFS_SOCKET:  return EXT2_S_IFSOCK;
        default:          return EXT2_S_IFREG;
    }
}

/*
 * Create a VFS node from an ext2 inode
 */
static vfs_node_t *ext2_create_node(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode, const char *name) {
    vfs_node_t *node = vfs_node_alloc();
    if (!node) {
        return NULL;
    }

    ext2_inode_info_t *info = kmem_cache_alloc(ext2_inode_info_cache);
    if (!info) {
        vfs_node_free(node);
        return NULL;
    }

    /* Initialize inode info */
    info->fs = fs;
    info->ino = ino;
    memcpy(&info->inode, inode, sizeof(ext2_inode_t));
    info->dirty = false;

    /* Initialize VFS node */
    if (name) {
        strncpy(node->name, name, VFS_NAME_MAX);
    }
    node->type = ext2_mode_to_vfs_type(inode->i_mode);
    node->permissions = inode->i_mode & 0x1FF;
    node->uid = inode->i_uid;
    node->gid = inode->i_gid;
    node->size = inode->i_size;
    node->atime = inode->i_atime;
    node->mtime = inode->i_mtime;
    node->ctime = inode->i_ctime;
    node->nlink = inode->i_links_count;
    node->private = info;

    if (node->type == VFS_DIR) {
        node->ops = &ext2_dir_ops;
    } else {
        node->ops = &ext2_file_ops;
    }

    return node;
}

/*
 * Flush an inode's changes to disk
 */
static int ext2_sync_inode(ext2_inode_info_t *info) {
    if (!info->dirty) {
        return VFS_OK;
    }

    int err = ext2_write_inode(info->fs, info->ino, &info->inode);
    if (err == VFS_OK) {
        info->dirty = false;
    }
    return err;
}

/*
 * File Operations
 */
static int ext2_file_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void ext2_file_close(vfs_node_t *node) {
    ext2_inode_info_t *info = node->private;
    if (info && info->dirty) {
        ext2_sync_inode(info);
    }
}

static ssize_t ext2_file_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    ext2_inode_info_t *info = node->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    ext2_inode_t *inode = &info->inode;

    /* Check bounds */
    if (offset >= inode->i_size) {
        return 0;
    }

    /* Limit read to file size */
    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }

    uint8_t *dst = (uint8_t *)buf;
    size_t remaining = size;
    uint64_t pos = offset;

    /* Allocate block buffer */
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    while (remaining > 0) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t block_offset = pos % fs->block_size;
        uint32_t chunk = fs->block_size - block_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);
        if (phys_block == 0) {
            /* Sparse file - return zeros */
            memset(dst, 0, chunk);
        } else {
            if (ext2_read_block(fs, phys_block, block_buf) != 0) {
                kfree(block_buf);
                return VFS_EIO;
            }
            memcpy(dst, block_buf + block_offset, chunk);
        }

        dst += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    kfree(block_buf);
    return size;
}

static ssize_t ext2_file_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    ext2_inode_info_t *info = node->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    ext2_inode_t *inode = &info->inode;
    uint32_t inode_group = (info->ino - 1) / fs->inodes_per_group;

    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = size;
    uint64_t pos = offset;

    /* Allocate block buffer */
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    while (remaining > 0) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t block_offset = pos % fs->block_size;
        uint32_t chunk = fs->block_size - block_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }

        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);
        if (phys_block == 0) {
            /* Allocate new block */
            phys_block = ext2_alloc_block(fs, inode_group);
            if (phys_block == 0) {
                kfree(block_buf);
                if (pos > offset) {
                    /* Partial write */
                    node->size = (pos > node->size) ? pos : node->size;
                    inode->i_size = node->size;
                    info->dirty = true;
                    return pos - offset;
                }
                return VFS_ENOSPC;
            }

            /* Set the block pointer */
            int err = ext2_set_block(fs, inode, logical_block, phys_block, inode_group);
            if (err != VFS_OK) {
                ext2_free_block(fs, phys_block);
                kfree(block_buf);
                return err;
            }
            inode->i_blocks += fs->block_size / 512;

            /* Zero the new block if partial write */
            if (block_offset > 0 || chunk < fs->block_size) {
                memset(block_buf, 0, fs->block_size);
            }
        } else if (block_offset > 0 || chunk < fs->block_size) {
            /* Partial block write - need to read first */
            if (ext2_read_block(fs, phys_block, block_buf) != 0) {
                kfree(block_buf);
                return VFS_EIO;
            }
        }

        /* Copy data */
        memcpy(block_buf + block_offset, src, chunk);

        /* Write block */
        if (ext2_write_block(fs, phys_block, block_buf) != 0) {
            kfree(block_buf);
            return VFS_EIO;
        }

        src += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    kfree(block_buf);

    /* Update file size if extended */
    if (offset + size > inode->i_size) {
        inode->i_size = offset + size;
        node->size = inode->i_size;
    }

    /* Update mtime */
    /* TODO: Get current time */
    info->dirty = true;

    return size;
}

static int ext2_file_truncate(vfs_node_t *node, uint64_t size) {
    ext2_inode_info_t *info = node->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    ext2_inode_t *inode = &info->inode;

    if (size < inode->i_size) {
        /* Shrinking - free blocks beyond new size */
        uint32_t new_blocks = (size + fs->block_size - 1) / fs->block_size;
        uint32_t old_blocks = (inode->i_size + fs->block_size - 1) / fs->block_size;

        for (uint32_t b = new_blocks; b < old_blocks; b++) {
            uint32_t phys = ext2_get_block(fs, inode, b);
            if (phys != 0) {
                ext2_free_block(fs, phys);
                inode->i_blocks -= fs->block_size / 512;
            }
        }

        /* TODO: Free indirect blocks if now empty */
    }
    /* Extending is handled lazily on write */

    inode->i_size = size;
    node->size = size;
    info->dirty = true;

    return VFS_OK;
}

static int ext2_stat(vfs_node_t *node, vfs_stat_t *st) {
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
static int ext2_dir_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void ext2_dir_close(vfs_node_t *node) {
    ext2_inode_info_t *info = node->private;
    if (info && info->dirty) {
        ext2_sync_inode(info);
    }
}

static int ext2_dir_readdir(vfs_node_t *node, dirent_t *dent, uint32_t index) {
    ext2_inode_info_t *info = node->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    ext2_inode_t *inode = &info->inode;

    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    uint32_t current_index = 0;
    uint64_t pos = 0;

    while (pos < inode->i_size) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);

        if (phys_block == 0) {
            pos += fs->block_size;
            continue;
        }

        if (ext2_read_block(fs, phys_block, block_buf) != 0) {
            kfree(block_buf);
            return VFS_EIO;
        }

        uint32_t block_offset = 0;
        while (block_offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(block_buf + block_offset);

            if (de->rec_len == 0) {
                break;  /* Invalid entry */
            }

            if (de->inode != 0) {
                if (current_index == index) {
                    /* Found the entry */
                    size_t name_len = de->name_len;
                    if (name_len > VFS_NAME_MAX) {
                        name_len = VFS_NAME_MAX;
                    }
                    memcpy(dent->name, de->name, name_len);
                    dent->name[name_len] = '\0';
                    dent->inode = de->inode;
                    dent->type = ext2_ftype_to_vfs_type(de->file_type);
                    kfree(block_buf);
                    return VFS_OK;
                }
                current_index++;
            }

            block_offset += de->rec_len;
        }

        pos += fs->block_size;
    }

    kfree(block_buf);
    return VFS_ENOENT;  /* No more entries */
}

static vfs_node_t *ext2_dir_finddir(vfs_node_t *node, const char *name) {
    ext2_inode_info_t *info = node->private;
    if (!info) {
        return NULL;
    }

    ext2_fs_t *fs = info->fs;
    ext2_inode_t *inode = &info->inode;

    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return NULL;
    }

    uint64_t pos = 0;

    while (pos < inode->i_size) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);

        if (phys_block == 0) {
            pos += fs->block_size;
            continue;
        }

        if (ext2_read_block(fs, phys_block, block_buf) != 0) {
            kfree(block_buf);
            return NULL;
        }

        uint32_t block_offset = 0;
        while (block_offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(block_buf + block_offset);

            if (de->rec_len == 0) {
                break;
            }

            if (de->inode != 0 && de->name_len == strlen(name)) {
                if (memcmp(de->name, name, de->name_len) == 0) {
                    /* Found it */
                    uint32_t child_ino = de->inode;
                    kfree(block_buf);

                    /* Read the child inode */
                    ext2_inode_t child_inode;
                    if (ext2_read_inode(fs, child_ino, &child_inode) != VFS_OK) {
                        return NULL;
                    }

                    return ext2_create_node(fs, child_ino, &child_inode, name);
                }
            }

            block_offset += de->rec_len;
        }

        pos += fs->block_size;
    }

    kfree(block_buf);
    return NULL;
}

/*
 * Add a directory entry
 */
static int ext2_add_dir_entry(vfs_node_t *parent, uint32_t inode_num, const char *name, uint8_t file_type) {
    ext2_inode_info_t *info = parent->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    ext2_inode_t *inode = &info->inode;
    uint32_t inode_group = (info->ino - 1) / fs->inodes_per_group;

    size_t name_len = strlen(name);
    /* Entry size must be 4-byte aligned */
    uint16_t needed = ((sizeof(ext2_dir_entry_t) + name_len + 3) / 4) * 4;

    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    /* Search existing blocks for space */
    uint64_t pos = 0;
    while (pos < inode->i_size) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);

        if (phys_block == 0) {
            pos += fs->block_size;
            continue;
        }

        if (ext2_read_block(fs, phys_block, block_buf) != 0) {
            kfree(block_buf);
            return VFS_EIO;
        }

        uint32_t block_offset = 0;
        while (block_offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(block_buf + block_offset);

            if (de->rec_len == 0) {
                break;
            }

            /* Calculate actual size of this entry */
            uint16_t actual_size;
            if (de->inode == 0) {
                actual_size = 0;  /* Empty entry */
            } else {
                actual_size = ((sizeof(ext2_dir_entry_t) + de->name_len + 3) / 4) * 4;
            }

            /* Check if there's room after this entry */
            uint16_t available = de->rec_len - actual_size;
            if (available >= needed) {
                /* Split this entry */
                if (de->inode != 0) {
                    /* Shrink existing entry */
                    de->rec_len = actual_size;
                    block_offset += actual_size;
                }

                /* Create new entry */
                ext2_dir_entry_t *new_de = (ext2_dir_entry_t *)(block_buf + block_offset);
                new_de->inode = inode_num;
                new_de->rec_len = (de->inode == 0) ? de->rec_len : available;
                new_de->name_len = name_len;
                new_de->file_type = file_type;
                memcpy(new_de->name, name, name_len);

                /* Write back block */
                if (ext2_write_block(fs, phys_block, block_buf) != 0) {
                    kfree(block_buf);
                    return VFS_EIO;
                }

                kfree(block_buf);
                return VFS_OK;
            }

            block_offset += de->rec_len;
        }

        pos += fs->block_size;
    }

    /* Need to allocate a new block */
    uint32_t new_block = ext2_alloc_block(fs, inode_group);
    if (new_block == 0) {
        kfree(block_buf);
        return VFS_ENOSPC;
    }

    /* Initialize new directory block */
    memset(block_buf, 0, fs->block_size);
    ext2_dir_entry_t *de = (ext2_dir_entry_t *)block_buf;
    de->inode = inode_num;
    de->rec_len = fs->block_size;  /* Entry spans entire block */
    de->name_len = name_len;
    de->file_type = file_type;
    memcpy(de->name, name, name_len);

    /* Write new block */
    if (ext2_write_block(fs, new_block, block_buf) != 0) {
        ext2_free_block(fs, new_block);
        kfree(block_buf);
        return VFS_EIO;
    }

    /* Add block to inode */
    uint32_t logical_block = inode->i_size / fs->block_size;
    int err = ext2_set_block(fs, inode, logical_block, new_block, inode_group);
    if (err != VFS_OK) {
        ext2_free_block(fs, new_block);
        kfree(block_buf);
        return err;
    }

    inode->i_size += fs->block_size;
    inode->i_blocks += fs->block_size / 512;
    parent->size = inode->i_size;
    info->dirty = true;

    kfree(block_buf);
    return VFS_OK;
}

/*
 * Remove a directory entry
 */
static int ext2_remove_dir_entry(vfs_node_t *parent, const char *name) {
    ext2_inode_info_t *info = parent->private;
    if (!info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = info->fs;
    ext2_inode_t *inode = &info->inode;

    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        return VFS_ENOMEM;
    }

    uint64_t pos = 0;

    while (pos < inode->i_size) {
        uint32_t logical_block = pos / fs->block_size;
        uint32_t phys_block = ext2_get_block(fs, inode, logical_block);

        if (phys_block == 0) {
            pos += fs->block_size;
            continue;
        }

        if (ext2_read_block(fs, phys_block, block_buf) != 0) {
            kfree(block_buf);
            return VFS_EIO;
        }

        uint32_t block_offset = 0;
        ext2_dir_entry_t *prev_de = NULL;

        while (block_offset < fs->block_size) {
            ext2_dir_entry_t *de = (ext2_dir_entry_t *)(block_buf + block_offset);

            if (de->rec_len == 0) {
                break;
            }

            if (de->inode != 0 && de->name_len == strlen(name)) {
                if (memcmp(de->name, name, de->name_len) == 0) {
                    /* Found it - remove by zeroing inode */
                    if (prev_de) {
                        /* Merge with previous entry */
                        prev_de->rec_len += de->rec_len;
                    } else {
                        /* First entry - just clear inode */
                        de->inode = 0;
                    }

                    /* Write back block */
                    if (ext2_write_block(fs, phys_block, block_buf) != 0) {
                        kfree(block_buf);
                        return VFS_EIO;
                    }

                    kfree(block_buf);
                    return VFS_OK;
                }
            }

            if (de->inode != 0) {
                prev_de = de;
            }
            block_offset += de->rec_len;
        }

        pos += fs->block_size;
    }

    kfree(block_buf);
    return VFS_ENOENT;
}

static int ext2_dir_create(vfs_node_t *parent, const char *name, uint32_t type) {
    ext2_inode_info_t *parent_info = parent->private;
    if (!parent_info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = parent_info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    /* Check if name already exists */
    vfs_node_t *existing = ext2_dir_finddir(parent, name);
    if (existing) {
        vfs_node_unref(existing);
        return VFS_EEXIST;
    }

    uint32_t parent_group = (parent_info->ino - 1) / fs->inodes_per_group;

    /* Allocate new inode */
    uint32_t new_ino = ext2_alloc_inode(fs, parent_group, type == VFS_DIR);
    if (new_ino == 0) {
        return VFS_ENOSPC;
    }

    /* Initialize inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = vfs_type_to_ext2_mode(type) | 0755;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;
    /* TODO: Set timestamps */

    /* Write inode */
    if (ext2_write_inode(fs, new_ino, &new_inode) != VFS_OK) {
        ext2_free_inode(fs, new_ino, type == VFS_DIR);
        return VFS_EIO;
    }

    /* Add directory entry */
    uint8_t file_type = vfs_type_to_ext2_ftype(type);
    int err = ext2_add_dir_entry(parent, new_ino, name, file_type);
    if (err != VFS_OK) {
        ext2_free_inode(fs, new_ino, type == VFS_DIR);
        return err;
    }

    return VFS_OK;
}

static int ext2_dir_unlink(vfs_node_t *parent, const char *name) {
    ext2_inode_info_t *parent_info = parent->private;
    if (!parent_info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = parent_info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    /* Find the entry to get inode number */
    vfs_node_t *target = ext2_dir_finddir(parent, name);
    if (!target) {
        return VFS_ENOENT;
    }

    ext2_inode_info_t *target_info = target->private;
    if (!target_info) {
        vfs_node_unref(target);
        return VFS_EIO;
    }

    /* Can't unlink directories */
    if (target->type == VFS_DIR) {
        vfs_node_unref(target);
        return VFS_EISDIR;
    }

    uint32_t target_ino = target_info->ino;
    ext2_inode_t *target_inode = &target_info->inode;

    /* Remove directory entry */
    int err = ext2_remove_dir_entry(parent, name);
    if (err != VFS_OK) {
        vfs_node_unref(target);
        return err;
    }

    /* Decrement link count */
    target_inode->i_links_count--;

    if (target_inode->i_links_count == 0) {
        /* Free all blocks */
        uint32_t num_blocks = (target_inode->i_size + fs->block_size - 1) / fs->block_size;
        for (uint32_t b = 0; b < num_blocks; b++) {
            uint32_t phys = ext2_get_block(fs, target_inode, b);
            if (phys != 0) {
                ext2_free_block(fs, phys);
            }
        }

        /* TODO: Free indirect blocks */

        /* Free inode */
        ext2_free_inode(fs, target_ino, false);
    } else {
        /* Just update link count on disk */
        ext2_write_inode(fs, target_ino, target_inode);
    }

    vfs_node_unref(target);
    return VFS_OK;
}

static int ext2_dir_mkdir(vfs_node_t *parent, const char *name) {
    ext2_inode_info_t *parent_info = parent->private;
    if (!parent_info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = parent_info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    /* Check if name already exists */
    vfs_node_t *existing = ext2_dir_finddir(parent, name);
    if (existing) {
        vfs_node_unref(existing);
        return VFS_EEXIST;
    }

    uint32_t parent_group = (parent_info->ino - 1) / fs->inodes_per_group;

    /* Allocate new inode */
    uint32_t new_ino = ext2_alloc_inode(fs, parent_group, true);
    if (new_ino == 0) {
        return VFS_ENOSPC;
    }

    /* Initialize directory inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = EXT2_S_IFDIR | 0755;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = fs->block_size;
    new_inode.i_links_count = 2;  /* . and parent's link */
    new_inode.i_blocks = fs->block_size / 512;

    /* Allocate block for directory contents */
    uint32_t dir_block = ext2_alloc_block(fs, parent_group);
    if (dir_block == 0) {
        ext2_free_inode(fs, new_ino, true);
        return VFS_ENOSPC;
    }
    new_inode.i_block[0] = dir_block;

    /* Initialize directory block with . and .. */
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_ino, true);
        return VFS_ENOMEM;
    }

    memset(block_buf, 0, fs->block_size);

    /* . entry */
    ext2_dir_entry_t *de = (ext2_dir_entry_t *)block_buf;
    de->inode = new_ino;
    de->rec_len = 12;  /* Minimum size for "." */
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    de->name[0] = '.';

    /* .. entry */
    de = (ext2_dir_entry_t *)(block_buf + 12);
    de->inode = parent_info->ino;
    de->rec_len = fs->block_size - 12;  /* Rest of block */
    de->name_len = 2;
    de->file_type = EXT2_FT_DIR;
    de->name[0] = '.';
    de->name[1] = '.';

    /* Write directory block */
    if (ext2_write_block(fs, dir_block, block_buf) != 0) {
        kfree(block_buf);
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_ino, true);
        return VFS_EIO;
    }
    kfree(block_buf);

    /* Write new inode */
    if (ext2_write_inode(fs, new_ino, &new_inode) != VFS_OK) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_ino, true);
        return VFS_EIO;
    }

    /* Add entry to parent directory */
    int err = ext2_add_dir_entry(parent, new_ino, name, EXT2_FT_DIR);
    if (err != VFS_OK) {
        ext2_free_block(fs, dir_block);
        ext2_free_inode(fs, new_ino, true);
        return err;
    }

    /* Increment parent's link count */
    parent_info->inode.i_links_count++;
    parent_info->dirty = true;

    return VFS_OK;
}

static int ext2_dir_rmdir(vfs_node_t *parent, const char *name) {
    ext2_inode_info_t *parent_info = parent->private;
    if (!parent_info) {
        return VFS_EIO;
    }

    ext2_fs_t *fs = parent_info->fs;
    if (fs->read_only) {
        return VFS_EINVAL;
    }

    /* Find the directory */
    vfs_node_t *target = ext2_dir_finddir(parent, name);
    if (!target) {
        return VFS_ENOENT;
    }

    /* Must be a directory */
    if (target->type != VFS_DIR) {
        vfs_node_unref(target);
        return VFS_ENOTDIR;
    }

    ext2_inode_info_t *target_info = target->private;
    if (!target_info) {
        vfs_node_unref(target);
        return VFS_EIO;
    }

    /* Check if directory is empty (only . and ..) */
    dirent_t dent;
    int count = 0;
    for (uint32_t i = 0; ext2_dir_readdir(target, &dent, i) == VFS_OK; i++) {
        if (strcmp(dent.name, ".") != 0 && strcmp(dent.name, "..") != 0) {
            count++;
        }
    }

    if (count > 0) {
        vfs_node_unref(target);
        return VFS_ENOTEMPTY;
    }

    uint32_t target_ino = target_info->ino;
    ext2_inode_t *target_inode = &target_info->inode;

    /* Remove directory entry from parent */
    int err = ext2_remove_dir_entry(parent, name);
    if (err != VFS_OK) {
        vfs_node_unref(target);
        return err;
    }

    /* Free directory's blocks */
    uint32_t num_blocks = (target_inode->i_size + fs->block_size - 1) / fs->block_size;
    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_block(fs, target_inode, b);
        if (phys != 0) {
            ext2_free_block(fs, phys);
        }
    }

    /* Free inode */
    ext2_free_inode(fs, target_ino, true);

    /* Decrement parent's link count */
    parent_info->inode.i_links_count--;
    parent_info->dirty = true;

    vfs_node_unref(target);
    return VFS_OK;
}

/*
 * Operation tables
 */
static node_ops_t ext2_file_ops = {
    .open = ext2_file_open,
    .close = ext2_file_close,
    .read = ext2_file_read,
    .write = ext2_file_write,
    .truncate = ext2_file_truncate,
    .stat = ext2_stat,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .ioctl = NULL,
};

static node_ops_t ext2_dir_ops = {
    .open = ext2_dir_open,
    .close = ext2_dir_close,
    .read = NULL,
    .write = NULL,
    .truncate = NULL,
    .stat = ext2_stat,
    .readdir = ext2_dir_readdir,
    .finddir = ext2_dir_finddir,
    .create = ext2_dir_create,
    .unlink = ext2_dir_unlink,
    .mkdir = ext2_dir_mkdir,
    .rmdir = ext2_dir_rmdir,
    .ioctl = NULL,
};

/*
 * Write superblock to disk
 */
static int ext2_write_superblock(ext2_fs_t *fs) {
    uint8_t *buf = kmalloc(1024);
    if (!buf) {
        return VFS_ENOMEM;
    }

    /* Read sectors 2-3 (bytes 1024-2047) */
    if (block_read(fs->dev, 2, 2, buf) != 0) {
        kfree(buf);
        return VFS_EIO;
    }

    /* Copy superblock */
    memcpy(buf, &fs->sb, sizeof(ext2_superblock_t));

    /* Write back */
    if (block_write(fs->dev, 2, 2, buf) != 0) {
        kfree(buf);
        return VFS_EIO;
    }

    kfree(buf);
    return VFS_OK;
}

/*
 * Write block group descriptors to disk
 */
static int ext2_write_group_descs(ext2_fs_t *fs) {
    /* Group descriptors start at block 2 (for 1K blocks) or block 1 (for larger) */
    uint32_t gd_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gd_size = fs->group_count * sizeof(ext2_group_desc_t);
    uint32_t gd_blocks = (gd_size + fs->block_size - 1) / fs->block_size;

    return ext2_write_blocks(fs, gd_block, gd_blocks, fs->groups);
}

/*
 * Mount an ext2 filesystem
 */
static vfs_node_t *ext2_mount(const char *source, uint32_t flags) {
    (void)flags;

    if (!source) {
        ERROR("ext2: No source device specified");
        return NULL;
    }

    /* Find the block device */
    block_device_t *dev = block_find(source);
    if (!dev) {
        ERROR("ext2: Device '%s' not found", source);
        return NULL;
    }

    /* Allocate filesystem structure */
    ext2_fs_t *fs = kmalloc(sizeof(ext2_fs_t));
    if (!fs) {
        ERROR("ext2: Failed to allocate fs structure");
        return NULL;
    }
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->dev = dev;

    /* Read superblock (at byte offset 1024, which is sector 2 for 512-byte sectors) */
    uint8_t sb_buf[1024];
    if (block_read(dev, 2, 2, sb_buf) != 0) {
        ERROR("ext2: Failed to read superblock");
        kfree(fs);
        return NULL;
    }

    memcpy(&fs->sb, sb_buf, sizeof(ext2_superblock_t));

    /* Validate magic number */
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        ERROR("ext2: Invalid magic number 0x%04x (expected 0x%04x)",
              fs->sb.s_magic, EXT2_SUPER_MAGIC);
        kfree(fs);
        return NULL;
    }

    /* Calculate filesystem parameters */
    fs->block_size = 1024 << fs->sb.s_log_block_size;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->first_data_block = fs->sb.s_first_data_block;
    fs->group_count = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    fs->ptrs_per_block = fs->block_size / sizeof(uint32_t);

    /* Get inode size (128 for rev 0, from superblock for rev 1+) */
    if (fs->sb.s_rev_level >= 1) {
        fs->inode_size = fs->sb.s_inode_size;
    } else {
        fs->inode_size = 128;
    }

    /* Check for incompatible features */
    if (fs->sb.s_feature_incompat & ~(EXT2_FEATURE_INCOMPAT_FILETYPE)) {
        WARN("ext2: Unsupported incompatible features: 0x%x",
             fs->sb.s_feature_incompat);
        /* Mount read-only */
        fs->read_only = true;
    }

    INFO("ext2: Block size: %u, Groups: %u, Inodes/group: %u",
         fs->block_size, fs->group_count, fs->inodes_per_group);

    /* Read block group descriptors */
    /* Group descriptors start at block 2 (for 1K blocks) or block 1 (for larger blocks) */
    uint32_t gd_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t gd_size = fs->group_count * sizeof(ext2_group_desc_t);
    uint32_t gd_blocks = (gd_size + fs->block_size - 1) / fs->block_size;

    fs->groups = kmalloc(gd_blocks * fs->block_size);
    if (!fs->groups) {
        ERROR("ext2: Failed to allocate group descriptors");
        kfree(fs);
        return NULL;
    }

    if (ext2_read_blocks(fs, gd_block, gd_blocks, fs->groups) != 0) {
        ERROR("ext2: Failed to read group descriptors");
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    /* Read root inode (inode 2) */
    ext2_inode_t root_inode;
    if (ext2_read_inode(fs, EXT2_ROOT_INO, &root_inode) != VFS_OK) {
        ERROR("ext2: Failed to read root inode");
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    /* Create root VFS node */
    vfs_node_t *root = ext2_create_node(fs, EXT2_ROOT_INO, &root_inode, "");
    if (!root) {
        ERROR("ext2: Failed to create root node");
        kfree(fs->groups);
        kfree(fs);
        return NULL;
    }

    INFO("ext2: Mounted %s (blocks: %u, inodes: %u, free: %u/%u)",
         source, fs->sb.s_blocks_count, fs->sb.s_inodes_count,
         fs->sb.s_free_blocks_count, fs->sb.s_free_inodes_count);

    return root;
}

/*
 * Unmount an ext2 filesystem
 */
static int ext2_unmount(vfs_mount_t *mount) {
    if (!mount || !mount->root) {
        return VFS_EINVAL;
    }

    ext2_inode_info_t *info = mount->root->private;
    if (!info) {
        return VFS_EINVAL;
    }

    ext2_fs_t *fs = info->fs;

    /* Sync superblock if dirty */
    if (fs->dirty && !fs->read_only) {
        ext2_write_superblock(fs);
        ext2_write_group_descs(fs);
    }

    /* Free resources */
    kfree(fs->groups);
    kfree(fs);

    /* Free root node's private data */
    kmem_cache_free(ext2_inode_info_cache, info);

    return VFS_OK;
}

/*
 * Initialize ext2 filesystem driver
 */
void ext2_init(void) {
    INFO("Initializing ext2 filesystem...");

    /* Explicitly initialize operation tables */
    ext2_fs_ops.name = "ext2";
    ext2_fs_ops.mount = ext2_mount;
    ext2_fs_ops.unmount = ext2_unmount;

    /* File operations */
    ext2_file_ops.open = ext2_file_open;
    ext2_file_ops.close = ext2_file_close;
    ext2_file_ops.read = ext2_file_read;
    ext2_file_ops.write = ext2_file_write;
    ext2_file_ops.truncate = ext2_file_truncate;
    ext2_file_ops.stat = ext2_stat;
    ext2_file_ops.readdir = NULL;
    ext2_file_ops.finddir = NULL;
    ext2_file_ops.create = NULL;
    ext2_file_ops.unlink = NULL;
    ext2_file_ops.mkdir = NULL;
    ext2_file_ops.rmdir = NULL;

    /* Directory operations */
    ext2_dir_ops.open = ext2_dir_open;
    ext2_dir_ops.close = ext2_dir_close;
    ext2_dir_ops.read = NULL;
    ext2_dir_ops.write = NULL;
    ext2_dir_ops.truncate = NULL;
    ext2_dir_ops.stat = ext2_stat;
    ext2_dir_ops.readdir = ext2_dir_readdir;
    ext2_dir_ops.finddir = ext2_dir_finddir;
    ext2_dir_ops.create = ext2_dir_create;
    ext2_dir_ops.unlink = ext2_dir_unlink;
    ext2_dir_ops.mkdir = ext2_dir_mkdir;
    ext2_dir_ops.rmdir = ext2_dir_rmdir;

    /* Create slab cache for inode info */
    ext2_inode_info_cache = kmem_cache_create("ext2_inode_info",
                                               sizeof(ext2_inode_info_t),
                                               8, SLAB_ZERO);
    if (!ext2_inode_info_cache) {
        panic("ext2: Failed to create inode info cache");
    }

    /* Register with VFS */
    int err = vfs_register_fs(&ext2_fs_ops);
    if (err != VFS_OK) {
        panic("ext2: Failed to register filesystem: %d", err);
    }

    INFO("ext2 filesystem initialized");
}
