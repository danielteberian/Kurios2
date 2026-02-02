/* partition.h - Partition table structures and parsing */
#ifndef _KERNEL_PARTITION_H
#define _KERNEL_PARTITION_H

#include <stdint.h>
#include <stdbool.h>
#include "block.h"

/*
 * MBR Partition Entry (16 bytes)
 * Located at offset 0x1BE in the MBR sector
 */
typedef struct mbr_partition {
    uint8_t  boot_flag;         /* 0x80 = bootable, 0x00 = not bootable */
    uint8_t  start_chs[3];      /* Start CHS address (legacy) */
    uint8_t  type;              /* Partition type */
    uint8_t  end_chs[3];        /* End CHS address (legacy) */
    uint32_t start_lba;         /* Start sector (LBA) */
    uint32_t sector_count;      /* Number of sectors */
} __attribute__((packed)) mbr_partition_t;

/*
 * MBR Sector (512 bytes)
 */
typedef struct mbr {
    uint8_t         bootstrap[446];     /* Bootstrap code */
    mbr_partition_t partitions[4];      /* Four partition entries */
    uint16_t        signature;          /* Boot signature (0xAA55) */
} __attribute__((packed)) mbr_t;

/* MBR constants */
#define MBR_SIGNATURE           0xAA55
#define MBR_BOOT_FLAG           0x80
#define MBR_PARTITION_OFFSET    0x1BE

/* Common MBR partition types */
#define MBR_TYPE_EMPTY          0x00
#define MBR_TYPE_FAT12          0x01
#define MBR_TYPE_FAT16_SMALL    0x04
#define MBR_TYPE_EXTENDED       0x05
#define MBR_TYPE_FAT16          0x06
#define MBR_TYPE_NTFS           0x07
#define MBR_TYPE_FAT32          0x0B
#define MBR_TYPE_FAT32_LBA      0x0C
#define MBR_TYPE_FAT16_LBA      0x0E
#define MBR_TYPE_EXTENDED_LBA   0x0F
#define MBR_TYPE_LINUX_SWAP     0x82
#define MBR_TYPE_LINUX          0x83
#define MBR_TYPE_LINUX_LVM      0x8E
#define MBR_TYPE_GPT_PROTECTIVE 0xEE

/*
 * GPT Header (92 bytes, padded to sector size)
 * Located at LBA 1
 */
typedef struct gpt_header {
    uint8_t  signature[8];          /* "EFI PART" */
    uint32_t revision;              /* GPT revision (typically 0x00010000) */
    uint32_t header_size;           /* Header size (usually 92) */
    uint32_t header_crc32;          /* CRC32 of header (with this field zeroed) */
    uint32_t reserved;              /* Must be zero */
    uint64_t current_lba;           /* Location of this header (LBA 1) */
    uint64_t backup_lba;            /* Location of backup header (last LBA) */
    uint64_t first_usable_lba;      /* First usable LBA for partitions */
    uint64_t last_usable_lba;       /* Last usable LBA for partitions */
    uint8_t  disk_guid[16];         /* Disk GUID */
    uint64_t partition_table_lba;   /* Starting LBA of partition entries */
    uint32_t num_partition_entries; /* Number of partition entries */
    uint32_t partition_entry_size;  /* Size of each partition entry (usually 128) */
    uint32_t partition_table_crc32; /* CRC32 of partition entries */
} __attribute__((packed)) gpt_header_t;

/* GPT signature */
#define GPT_SIGNATURE       "EFI PART"
#define GPT_REVISION_1_0    0x00010000

/*
 * GPT Partition Entry (128 bytes)
 */
typedef struct gpt_partition {
    uint8_t  type_guid[16];         /* Partition type GUID */
    uint8_t  unique_guid[16];       /* Unique partition GUID */
    uint64_t first_lba;             /* First LBA */
    uint64_t last_lba;              /* Last LBA (inclusive) */
    uint64_t attributes;            /* Attribute flags */
    uint16_t name[36];              /* Partition name (UTF-16LE, up to 36 chars) */
} __attribute__((packed)) gpt_partition_t;

/* GPT attribute flags */
#define GPT_ATTR_REQUIRED       (1ULL << 0)     /* Required for platform */
#define GPT_ATTR_NO_BLOCK_IO    (1ULL << 1)     /* EFI: don't expose as block device */
#define GPT_ATTR_LEGACY_BOOT    (1ULL << 2)     /* Legacy BIOS bootable */

/* Common GPT partition type GUIDs (stored in little-endian) */
/* Empty partition GUID */
static const uint8_t GPT_TYPE_UNUSED[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* EFI System Partition */
static const uint8_t GPT_TYPE_EFI_SYSTEM[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* Microsoft Basic Data (also used for FAT32) */
static const uint8_t GPT_TYPE_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

/* Linux filesystem */
static const uint8_t GPT_TYPE_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

/*
 * Runtime partition device structure
 */
typedef struct partition_dev {
    block_device_t *parent;         /* Parent block device */
    uint64_t start_sector;          /* First sector of partition */
    uint64_t sector_count;          /* Number of sectors in partition */
    uint8_t  partition_number;      /* Partition number (1-based) */
    uint8_t  type;                  /* MBR type or 0 for GPT */
    bool     is_gpt;                /* True if GPT partition */
    uint8_t  type_guid[16];         /* GPT type GUID (if GPT) */
    block_device_t dev;             /* Wrapper block device */
} partition_dev_t;

/* Maximum partitions per disk */
#define PARTITION_MAX       128     /* GPT supports up to 128 */

/*
 * Initialize partition subsystem
 */
void partition_init(void);

/*
 * Scan a block device for partitions
 * Creates partition devices for each found partition
 *
 * @param dev   Block device to scan
 * @return Number of partitions found, or negative error
 */
int partition_scan(block_device_t *dev);

/*
 * Get partition type name
 *
 * @param type  MBR partition type
 * @return Human-readable type name
 */
const char *partition_type_name(uint8_t type);

/*
 * Check if two GUIDs are equal
 */
static inline bool guid_equal(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

#endif /* _KERNEL_PARTITION_H */
