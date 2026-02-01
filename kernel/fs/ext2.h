/* ext2.h - ext2 filesystem structures and constants */
#ifndef _KERNEL_EXT2_H
#define _KERNEL_EXT2_H

#include <stdint.h>
#include <stdbool.h>

/*
 * ext2 filesystem magic number
 */
#define EXT2_SUPER_MAGIC    0xEF53

/*
 * Special inode numbers
 */
#define EXT2_BAD_INO        1   /* Bad blocks inode */
#define EXT2_ROOT_INO       2   /* Root directory inode */
#define EXT2_ACL_IDX_INO    3   /* ACL index inode (deprecated) */
#define EXT2_ACL_DATA_INO   4   /* ACL data inode (deprecated) */
#define EXT2_BOOT_LOADER_INO 5  /* Boot loader inode */
#define EXT2_UNDEL_DIR_INO  6   /* Undelete directory inode */
#define EXT2_FIRST_INO      11  /* First non-reserved inode */

/*
 * File type bits in i_mode
 */
#define EXT2_S_IFSOCK   0xC000  /* Socket */
#define EXT2_S_IFLNK    0xA000  /* Symbolic link */
#define EXT2_S_IFREG    0x8000  /* Regular file */
#define EXT2_S_IFBLK    0x6000  /* Block device */
#define EXT2_S_IFDIR    0x4000  /* Directory */
#define EXT2_S_IFCHR    0x2000  /* Character device */
#define EXT2_S_IFIFO    0x1000  /* FIFO */
#define EXT2_S_IFMT     0xF000  /* File type mask */

/*
 * Permission bits in i_mode
 */
#define EXT2_S_ISUID    0x0800  /* Set user ID */
#define EXT2_S_ISGID    0x0400  /* Set group ID */
#define EXT2_S_ISVTX    0x0200  /* Sticky bit */
#define EXT2_S_IRUSR    0x0100  /* User read */
#define EXT2_S_IWUSR    0x0080  /* User write */
#define EXT2_S_IXUSR    0x0040  /* User execute */
#define EXT2_S_IRGRP    0x0020  /* Group read */
#define EXT2_S_IWGRP    0x0010  /* Group write */
#define EXT2_S_IXGRP    0x0008  /* Group execute */
#define EXT2_S_IROTH    0x0004  /* Other read */
#define EXT2_S_IWOTH    0x0002  /* Other write */
#define EXT2_S_IXOTH    0x0001  /* Other execute */

/*
 * Directory entry file types (in de->file_type)
 */
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

/*
 * Inode flags
 */
#define EXT2_SECRM_FL       0x00000001  /* Secure deletion */
#define EXT2_UNRM_FL        0x00000002  /* Record for undelete */
#define EXT2_COMPR_FL       0x00000004  /* Compressed file */
#define EXT2_SYNC_FL        0x00000008  /* Synchronous updates */
#define EXT2_IMMUTABLE_FL   0x00000010  /* Immutable file */
#define EXT2_APPEND_FL      0x00000020  /* Append only */
#define EXT2_NODUMP_FL      0x00000040  /* Do not dump file */
#define EXT2_NOATIME_FL     0x00000080  /* No atime update */

/*
 * Feature flags
 */
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC    0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES   0x0002
#define EXT2_FEATURE_COMPAT_HAS_JOURNAL     0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR        0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INO      0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX       0x0020

#define EXT2_FEATURE_INCOMPAT_COMPRESSION   0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE      0x0002
#define EXT2_FEATURE_INCOMPAT_RECOVER       0x0004
#define EXT2_FEATURE_INCOMPAT_JOURNAL_DEV   0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG       0x0010

#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR    0x0004

/*
 * Block pointers in inode
 */
#define EXT2_NDIR_BLOCKS    12  /* Direct blocks */
#define EXT2_IND_BLOCK      12  /* Single indirect block */
#define EXT2_DIND_BLOCK     13  /* Double indirect block */
#define EXT2_TIND_BLOCK     14  /* Triple indirect block */
#define EXT2_N_BLOCKS       15  /* Total block pointers */

/*
 * Superblock structure (located at byte offset 1024)
 */
typedef struct ext2_superblock {
    uint32_t s_inodes_count;        /* Total inodes */
    uint32_t s_blocks_count;        /* Total blocks */
    uint32_t s_r_blocks_count;      /* Reserved blocks for superuser */
    uint32_t s_free_blocks_count;   /* Free blocks */
    uint32_t s_free_inodes_count;   /* Free inodes */
    uint32_t s_first_data_block;    /* First data block (0 or 1) */
    uint32_t s_log_block_size;      /* Block size = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;       /* Fragment size (obsolete) */
    uint32_t s_blocks_per_group;    /* Blocks per group */
    uint32_t s_frags_per_group;     /* Fragments per group (obsolete) */
    uint32_t s_inodes_per_group;    /* Inodes per group */
    uint32_t s_mtime;               /* Last mount time */
    uint32_t s_wtime;               /* Last write time */
    uint16_t s_mnt_count;           /* Mount count since last fsck */
    uint16_t s_max_mnt_count;       /* Max mounts before fsck */
    uint16_t s_magic;               /* Magic signature (0xEF53) */
    uint16_t s_state;               /* Filesystem state */
    uint16_t s_errors;              /* Error handling behavior */
    uint16_t s_minor_rev_level;     /* Minor revision level */
    uint32_t s_lastcheck;           /* Last fsck time */
    uint32_t s_checkinterval;       /* Max time between fscks */
    uint32_t s_creator_os;          /* Creator OS */
    uint32_t s_rev_level;           /* Revision level */
    uint16_t s_def_resuid;          /* Default UID for reserved blocks */
    uint16_t s_def_resgid;          /* Default GID for reserved blocks */

    /* Extended superblock fields (rev >= 1) */
    uint32_t s_first_ino;           /* First non-reserved inode */
    uint16_t s_inode_size;          /* Inode structure size */
    uint16_t s_block_group_nr;      /* Block group of this superblock */
    uint32_t s_feature_compat;      /* Compatible features */
    uint32_t s_feature_incompat;    /* Incompatible features */
    uint32_t s_feature_ro_compat;   /* Read-only compatible features */
    uint8_t  s_uuid[16];            /* Volume UUID */
    char     s_volume_name[16];     /* Volume name */
    char     s_last_mounted[64];    /* Last mount path */
    uint32_t s_algo_bitmap;         /* Compression algorithm bitmap */

    /* Performance hints */
    uint8_t  s_prealloc_blocks;     /* Blocks to preallocate for files */
    uint8_t  s_prealloc_dir_blocks; /* Blocks to preallocate for dirs */
    uint16_t s_padding1;

    /* Journaling support (ext3) */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;

    /* Directory indexing support */
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint8_t  s_reserved[760];       /* Padding to 1024 bytes */
} __attribute__((packed)) ext2_superblock_t;

/*
 * Block group descriptor (32 bytes)
 */
typedef struct ext2_group_desc {
    uint32_t bg_block_bitmap;       /* Block bitmap block */
    uint32_t bg_inode_bitmap;       /* Inode bitmap block */
    uint32_t bg_inode_table;        /* Inode table block */
    uint16_t bg_free_blocks_count;  /* Free blocks in group */
    uint16_t bg_free_inodes_count;  /* Free inodes in group */
    uint16_t bg_used_dirs_count;    /* Directories in group */
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed)) ext2_group_desc_t;

/*
 * Inode structure (128 bytes minimum, may be larger)
 */
typedef struct ext2_inode {
    uint16_t i_mode;                /* File mode */
    uint16_t i_uid;                 /* Owner UID (low 16 bits) */
    uint32_t i_size;                /* File size in bytes (low 32 bits) */
    uint32_t i_atime;               /* Access time */
    uint32_t i_ctime;               /* Creation time */
    uint32_t i_mtime;               /* Modification time */
    uint32_t i_dtime;               /* Deletion time */
    uint16_t i_gid;                 /* Group ID (low 16 bits) */
    uint16_t i_links_count;         /* Link count */
    uint32_t i_blocks;              /* Blocks count (512-byte sectors) */
    uint32_t i_flags;               /* File flags */
    uint32_t i_osd1;                /* OS-dependent value 1 */
    uint32_t i_block[EXT2_N_BLOCKS]; /* Block pointers */
    uint32_t i_generation;          /* File version (for NFS) */
    uint32_t i_file_acl;            /* File ACL */
    uint32_t i_dir_acl;             /* Directory ACL (or i_size_high) */
    uint32_t i_faddr;               /* Fragment address (obsolete) */
    uint8_t  i_osd2[12];            /* OS-dependent value 2 */
} __attribute__((packed)) ext2_inode_t;

/*
 * Directory entry (variable length, minimum 8 bytes)
 */
typedef struct ext2_dir_entry {
    uint32_t inode;                 /* Inode number */
    uint16_t rec_len;               /* Directory entry length */
    uint8_t  name_len;              /* Name length */
    uint8_t  file_type;             /* File type (EXT2_FT_*) */
    char     name[];                /* File name (not null-terminated) */
} __attribute__((packed)) ext2_dir_entry_t;

/*
 * Per-mount ext2 filesystem state
 */
typedef struct ext2_fs {
    struct block_device *dev;       /* Underlying block device */
    ext2_superblock_t sb;           /* Cached superblock */
    ext2_group_desc_t *groups;      /* Block group descriptors */
    uint32_t block_size;            /* Block size in bytes */
    uint32_t inodes_per_group;      /* Inodes per group */
    uint32_t blocks_per_group;      /* Blocks per group */
    uint32_t group_count;           /* Number of block groups */
    uint32_t first_data_block;      /* First data block */
    uint32_t inode_size;            /* Inode size */
    uint32_t ptrs_per_block;        /* Block pointers per block */
    bool read_only;                 /* Mounted read-only */
    bool dirty;                     /* Superblock needs writeback */
} ext2_fs_t;

/*
 * Per-inode cached data
 */
typedef struct ext2_inode_info {
    ext2_fs_t *fs;                  /* Pointer to filesystem */
    uint32_t ino;                   /* Inode number */
    ext2_inode_t inode;             /* Cached on-disk inode */
    bool dirty;                     /* Inode needs writeback */
} ext2_inode_info_t;

/*
 * Initialize ext2 filesystem driver
 */
void ext2_init(void);

#endif /* _KERNEL_EXT2_H */
