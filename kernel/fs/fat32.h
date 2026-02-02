/* fat32.h - FAT32 filesystem structures and constants */
#ifndef _KERNEL_FAT32_H
#define _KERNEL_FAT32_H

#include <stdint.h>
#include <stdbool.h>

/*
 * FAT32 Boot Sector / BIOS Parameter Block (BPB)
 * Located at sector 0 of the FAT32 volume
 */
typedef struct fat32_bpb {
    /* Common BPB fields (offset 0-35) */
    uint8_t  jmp_boot[3];           /* Jump instruction to boot code */
    char     oem_name[8];           /* OEM identifier */
    uint16_t bytes_per_sector;      /* Bytes per sector (usually 512) */
    uint8_t  sectors_per_cluster;   /* Sectors per cluster (1,2,4,8,16,32,64,128) */
    uint16_t reserved_sectors;      /* Reserved sectors before first FAT */
    uint8_t  num_fats;              /* Number of FATs (usually 2) */
    uint16_t root_entry_count;      /* FAT12/16 root entries (0 for FAT32) */
    uint16_t total_sectors_16;      /* Total sectors (0 if > 65535) */
    uint8_t  media_type;            /* Media type (0xF8 = fixed disk) */
    uint16_t fat_size_16;           /* FAT12/16 FAT size (0 for FAT32) */
    uint16_t sectors_per_track;     /* Sectors per track (for CHS) */
    uint16_t num_heads;             /* Number of heads (for CHS) */
    uint32_t hidden_sectors;        /* Hidden sectors before this volume */
    uint32_t total_sectors_32;      /* Total sectors (if > 65535) */

    /* FAT32 Extended BPB (offset 36-89) */
    uint32_t fat_size_32;           /* Sectors per FAT */
    uint16_t ext_flags;             /* Extended flags */
    uint16_t fs_version;            /* Filesystem version (0.0) */
    uint32_t root_cluster;          /* First cluster of root directory */
    uint16_t fsinfo_sector;         /* FSInfo sector number */
    uint16_t backup_boot_sector;    /* Backup boot sector location */
    uint8_t  reserved[12];          /* Reserved */
    uint8_t  drive_number;          /* BIOS drive number */
    uint8_t  reserved1;             /* Reserved */
    uint8_t  boot_signature;        /* Extended boot signature (0x29) */
    uint32_t volume_id;             /* Volume serial number */
    char     volume_label[11];      /* Volume label */
    char     fs_type[8];            /* Filesystem type string "FAT32   " */
} __attribute__((packed)) fat32_bpb_t;

/*
 * FSInfo Structure
 * Located at fsinfo_sector (usually sector 1)
 */
typedef struct fat32_fsinfo {
    uint32_t lead_signature;        /* 0x41615252 */
    uint8_t  reserved1[480];        /* Reserved */
    uint32_t struct_signature;      /* 0x61417272 */
    uint32_t free_count;            /* Free cluster count (0xFFFFFFFF = unknown) */
    uint32_t next_free;             /* Next free cluster hint */
    uint8_t  reserved2[12];         /* Reserved */
    uint32_t trail_signature;       /* 0xAA550000 */
} __attribute__((packed)) fat32_fsinfo_t;

/* FSInfo signatures */
#define FAT32_FSINFO_LEAD_SIG       0x41615252
#define FAT32_FSINFO_STRUCT_SIG     0x61417272
#define FAT32_FSINFO_TRAIL_SIG      0xAA550000

/*
 * FAT Directory Entry (32 bytes)
 * 8.3 short filename format
 */
typedef struct fat_dirent {
    char     name[8];               /* Short name (space-padded) */
    char     ext[3];                /* Extension (space-padded) */
    uint8_t  attr;                  /* File attributes */
    uint8_t  nt_reserved;           /* Reserved for NT */
    uint8_t  create_time_tenths;    /* Creation time (tenths of second) */
    uint16_t create_time;           /* Creation time */
    uint16_t create_date;           /* Creation date */
    uint16_t access_date;           /* Last access date */
    uint16_t cluster_high;          /* High 16 bits of first cluster */
    uint16_t modify_time;           /* Last modification time */
    uint16_t modify_date;           /* Last modification date */
    uint16_t cluster_low;           /* Low 16 bits of first cluster */
    uint32_t file_size;             /* File size in bytes */
} __attribute__((packed)) fat_dirent_t;

/* Directory entry attributes */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F    /* Long filename entry */

/* Special directory entry markers */
#define FAT_DIRENT_FREE     0xE5    /* Entry is free */
#define FAT_DIRENT_END      0x00    /* Entry and all following are free */
#define FAT_DIRENT_KANJI    0x05    /* First char is actually 0xE5 */

/*
 * Long Filename (LFN) Entry (32 bytes)
 * Stores 13 UTF-16 characters per entry
 */
typedef struct fat_lfn_entry {
    uint8_t  order;                 /* LFN entry order (1-20), 0x40 = last */
    uint16_t name1[5];              /* Characters 1-5 (UTF-16LE) */
    uint8_t  attr;                  /* Attributes (always 0x0F) */
    uint8_t  type;                  /* Type (always 0) */
    uint8_t  checksum;              /* Checksum of short name */
    uint16_t name2[6];              /* Characters 6-11 (UTF-16LE) */
    uint16_t cluster;               /* Always 0 */
    uint16_t name3[2];              /* Characters 12-13 (UTF-16LE) */
} __attribute__((packed)) fat_lfn_entry_t;

/* LFN constants */
#define FAT_LFN_LAST        0x40    /* Last LFN entry (OR'd with order) */
#define FAT_LFN_MAX_ENTRIES 20      /* Max LFN entries (255 chars / 13) */
#define FAT_LFN_CHARS       13      /* Characters per LFN entry */
#define FAT_LFN_MAX_LEN     255     /* Maximum LFN length */

/* FAT32 cluster values */
#define FAT32_FREE          0x00000000  /* Free cluster */
#define FAT32_RESERVED_MIN  0x00000001  /* Reserved range start */
#define FAT32_RESERVED_MAX  0x00000001  /* Reserved range end */
#define FAT32_DATA_MIN      0x00000002  /* First valid data cluster */
#define FAT32_BAD           0x0FFFFFF7  /* Bad cluster */
#define FAT32_EOC_MIN       0x0FFFFFF8  /* End of chain (min value) */
#define FAT32_EOC           0x0FFFFFFF  /* End of chain (typical) */
#define FAT32_MASK          0x0FFFFFFF  /* Mask for 28-bit cluster number */

/* FAT32 limits */
#define FAT32_MIN_CLUSTERS  65525       /* Minimum clusters for FAT32 */
#define FAT32_MAX_CLUSTERS  0x0FFFFFF6  /* Maximum clusters for FAT32 */

/* Boot sector signature */
#define FAT_BOOT_SIG        0xAA55

/* Convenience macros */
#define FAT32_IS_EOC(c)     (((c) & FAT32_MASK) >= FAT32_EOC_MIN)
#define FAT32_IS_FREE(c)    ((c) == FAT32_FREE)
#define FAT32_IS_BAD(c)     (((c) & FAT32_MASK) == FAT32_BAD)
#define FAT32_IS_VALID(c)   ((c) >= FAT32_DATA_MIN && (c) < FAT32_BAD)

/* Get first cluster from directory entry */
#define FAT_GET_CLUSTER(de) \
    (((uint32_t)(de)->cluster_high << 16) | (de)->cluster_low)

/* Set first cluster in directory entry */
#define FAT_SET_CLUSTER(de, c) do { \
    (de)->cluster_high = (uint16_t)((c) >> 16); \
    (de)->cluster_low = (uint16_t)(c); \
} while (0)

/*
 * Per-mount FAT32 filesystem state
 */
typedef struct fat32_fs {
    struct block_device *dev;       /* Underlying block device */
    fat32_bpb_t bpb;                /* Cached BPB */

    /* Computed values */
    uint32_t fat_start_sector;      /* First sector of FAT */
    uint32_t fat_sectors;           /* Sectors per FAT */
    uint32_t data_start_sector;     /* First sector of data region */
    uint32_t root_cluster;          /* First cluster of root directory */
    uint32_t total_clusters;        /* Total data clusters */
    uint32_t cluster_size;          /* Bytes per cluster */
    uint32_t sectors_per_cluster;   /* Sectors per cluster */
    uint32_t entries_per_cluster;   /* Directory entries per cluster */

    /* FAT cache (one sector) */
    uint32_t *fat_cache;            /* Cached FAT sector */
    uint32_t fat_cache_sector;      /* Which FAT sector is cached */
    bool     fat_cache_dirty;       /* Cache needs writeback */

    /* FSInfo */
    uint32_t fsinfo_sector;         /* FSInfo sector number */
    uint32_t free_clusters;         /* Free cluster count */
    uint32_t next_free;             /* Next free cluster hint */

    bool read_only;                 /* Mounted read-only */
    bool dirty;                     /* Filesystem needs sync */
} fat32_fs_t;

/*
 * Per-inode FAT32 cached data
 */
typedef struct fat32_inode_info {
    fat32_fs_t *fs;                 /* Pointer to filesystem */
    uint32_t first_cluster;         /* First cluster of file/dir */
    uint32_t dir_cluster;           /* Parent directory cluster */
    uint32_t dir_index;             /* Index within parent directory */
    fat_dirent_t dirent;            /* Cached directory entry */
    bool dirty;                     /* Dirent needs writeback */
} fat32_inode_info_t;

/*
 * Initialize FAT32 filesystem driver
 */
void fat32_init(void);

#endif /* _KERNEL_FAT32_H */
