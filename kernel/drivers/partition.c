/* partition.c - Partition table parsing and partition device creation */

#include "partition.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../debug/debug.h"

/* Slab cache for partition devices */
static kmem_cache_t *partition_cache;

/* Forward declarations */
static int part_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buf);
static int part_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buf);
static int part_flush(block_device_t *dev);

/* Partition block operations - translate sector numbers */
static block_ops_t partition_ops = {
    .read = part_read,
    .write = part_write,
    .flush = part_flush,
    .submit = NULL,     /* Use synchronous I/O */
    .poll = NULL,
};

/*
 * Partition read - translates sector offset
 */
static int part_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buf) {
    partition_dev_t *part = (partition_dev_t *)dev->private;

    /* Check bounds */
    if (sector + count > part->sector_count) {
        ERROR("partition: read beyond end: sector=%llu count=%u max=%llu",
              sector, count, part->sector_count);
        return -1;
    }

    /* Translate to parent device sector */
    return block_read(part->parent, part->start_sector + sector, count, buf);
}

/*
 * Partition write - translates sector offset
 */
static int part_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buf) {
    partition_dev_t *part = (partition_dev_t *)dev->private;

    /* Check bounds */
    if (sector + count > part->sector_count) {
        ERROR("partition: write beyond end: sector=%llu count=%u max=%llu",
              sector, count, part->sector_count);
        return -1;
    }

    /* Translate to parent device sector */
    return block_write(part->parent, part->start_sector + sector, count, buf);
}

/*
 * Partition flush - forward to parent
 */
static int part_flush(block_device_t *dev) {
    partition_dev_t *part = (partition_dev_t *)dev->private;
    return block_flush(part->parent);
}

/*
 * Create a partition device
 */
static partition_dev_t *partition_create(block_device_t *parent, int num,
                                         uint64_t start, uint64_t count,
                                         uint8_t type, bool is_gpt,
                                         const uint8_t *type_guid) {
    partition_dev_t *part = kmem_cache_alloc(partition_cache);
    if (!part) {
        ERROR("partition: failed to allocate partition_dev_t");
        return NULL;
    }

    memset(part, 0, sizeof(partition_dev_t));

    part->parent = parent;
    part->start_sector = start;
    part->sector_count = count;
    part->partition_number = num;
    part->type = type;
    part->is_gpt = is_gpt;

    if (type_guid) {
        memcpy(part->type_guid, type_guid, 16);
    }

    /* Set up wrapper block device */
    snprintf(part->dev.name, BLOCK_NAME_MAX, "%s%d", parent->name, num);
    part->dev.index = parent->index * 16 + num;  /* Unique index */
    part->dev.sector_count = count;
    part->dev.sector_size = parent->sector_size;
    part->dev.capacity = count * parent->sector_size;
    part->dev.read_only = parent->read_only;
    part->dev.removable = parent->removable;
    part->dev.async_capable = false;    /* Use sync I/O for partitions */
    part->dev.ops = &partition_ops;
    part->dev.private = part;

    /* Register the partition device */
    if (block_register(&part->dev) != 0) {
        ERROR("partition: failed to register %s", part->dev.name);
        kmem_cache_free(partition_cache, part);
        return NULL;
    }

    INFO("partition: %s: start=%llu sectors=%llu (%llu MB) type=0x%02x",
         part->dev.name, start, count,
         (count * parent->sector_size) / (1024 * 1024), type);

    return part;
}

/*
 * Scan MBR partition table
 */
static int mbr_scan(block_device_t *dev, mbr_t *mbr) {
    int count = 0;

    for (int i = 0; i < 4; i++) {
        mbr_partition_t *entry = &mbr->partitions[i];

        /* Skip empty entries */
        if (entry->type == MBR_TYPE_EMPTY) {
            continue;
        }

        /* Skip extended partitions (we don't parse them yet) */
        if (entry->type == MBR_TYPE_EXTENDED ||
            entry->type == MBR_TYPE_EXTENDED_LBA) {
            DEBUG("partition: %s: skipping extended partition %d",
                  dev->name, i + 1);
            continue;
        }

        /* Skip protective MBR (GPT disk) */
        if (entry->type == MBR_TYPE_GPT_PROTECTIVE) {
            DEBUG("partition: %s: GPT protective MBR detected", dev->name);
            return -1;  /* Signal to try GPT */
        }

        /* Validate partition */
        if (entry->start_lba == 0 || entry->sector_count == 0) {
            WARN("partition: %s%d: invalid start/count", dev->name, i + 1);
            continue;
        }

        /* Check bounds */
        uint64_t end = (uint64_t)entry->start_lba + entry->sector_count;
        if (end > dev->sector_count) {
            WARN("partition: %s%d: extends beyond disk", dev->name, i + 1);
            continue;
        }

        /* Create partition device */
        partition_dev_t *part = partition_create(dev, i + 1,
                                                 entry->start_lba,
                                                 entry->sector_count,
                                                 entry->type,
                                                 false, NULL);
        if (part) {
            count++;
        }
    }

    return count;
}

/*
 * Scan GPT partition table
 */
static int gpt_scan(block_device_t *dev) {
    uint8_t *sector = kmalloc(dev->sector_size);
    if (!sector) {
        return -1;
    }

    /* Read GPT header at LBA 1 */
    if (block_read(dev, 1, 1, sector) != 0) {
        kfree(sector);
        return -1;
    }

    gpt_header_t *header = (gpt_header_t *)sector;

    /* Verify GPT signature */
    if (memcmp(header->signature, GPT_SIGNATURE, 8) != 0) {
        DEBUG("partition: %s: no GPT signature", dev->name);
        kfree(sector);
        return -1;
    }

    /* Validate header */
    if (header->header_size < sizeof(gpt_header_t) ||
        header->partition_entry_size < sizeof(gpt_partition_t)) {
        WARN("partition: %s: invalid GPT header sizes", dev->name);
        kfree(sector);
        return -1;
    }

    uint32_t num_entries = header->num_partition_entries;
    uint32_t entry_size = header->partition_entry_size;
    uint64_t table_lba = header->partition_table_lba;

    /* Limit entries to prevent huge allocations */
    if (num_entries > PARTITION_MAX) {
        num_entries = PARTITION_MAX;
    }

    INFO("partition: %s: GPT with %u partition entries", dev->name, num_entries);

    /* Calculate how many entries per sector */
    uint32_t entries_per_sector = dev->sector_size / entry_size;
    if (entries_per_sector == 0) {
        kfree(sector);
        return -1;
    }

    int count = 0;
    int part_num = 1;

    /* Read partition entries */
    for (uint32_t i = 0; i < num_entries; i++) {
        /* Read sector if needed */
        if (i % entries_per_sector == 0) {
            uint64_t lba = table_lba + (i / entries_per_sector);
            if (block_read(dev, lba, 1, sector) != 0) {
                break;
            }
        }

        uint32_t offset = (i % entries_per_sector) * entry_size;
        gpt_partition_t *entry = (gpt_partition_t *)(sector + offset);

        /* Skip unused entries */
        if (guid_equal(entry->type_guid, GPT_TYPE_UNUSED)) {
            continue;
        }

        /* Validate partition */
        if (entry->first_lba == 0 || entry->last_lba < entry->first_lba) {
            continue;
        }

        uint64_t sector_count = entry->last_lba - entry->first_lba + 1;

        /* Determine MBR-compatible type code */
        uint8_t type = 0x83;  /* Default: Linux filesystem */
        if (guid_equal(entry->type_guid, GPT_TYPE_EFI_SYSTEM)) {
            type = 0xEF;  /* EFI System Partition */
        } else if (guid_equal(entry->type_guid, GPT_TYPE_BASIC_DATA)) {
            type = 0x0C;  /* FAT32 LBA */
        }

        /* Create partition device */
        partition_dev_t *part = partition_create(dev, part_num,
                                                 entry->first_lba,
                                                 sector_count,
                                                 type,
                                                 true,
                                                 entry->type_guid);
        if (part) {
            count++;
            part_num++;
        }
    }

    kfree(sector);
    return count;
}

/*
 * Scan a block device for partitions
 */
int partition_scan(block_device_t *dev) {
    if (!dev) {
        return -1;
    }

    INFO("partition: scanning %s (%llu sectors)", dev->name, dev->sector_count);

    /* Allocate buffer for reading sectors */
    uint8_t *sector = kmalloc(dev->sector_size);
    if (!sector) {
        ERROR("partition: failed to allocate sector buffer");
        return -1;
    }

    /* Read sector 0 (MBR) */
    if (block_read(dev, 0, 1, sector) != 0) {
        ERROR("partition: failed to read sector 0");
        kfree(sector);
        return -1;
    }

    mbr_t *mbr = (mbr_t *)sector;

    /* Check MBR signature */
    if (mbr->signature != MBR_SIGNATURE) {
        DEBUG("partition: %s: no MBR signature (got 0x%04x)",
              dev->name, mbr->signature);
        kfree(sector);
        return 0;  /* Not an error, just no partitions */
    }

    /* Try to parse as MBR first */
    int count = mbr_scan(dev, mbr);
    kfree(sector);

    /* If MBR scan returned -1, it's a GPT disk */
    if (count < 0) {
        count = gpt_scan(dev);
        if (count < 0) {
            WARN("partition: %s: GPT scan failed", dev->name);
            return 0;
        }
    }

    INFO("partition: %s: found %d partition(s)", dev->name, count);
    return count;
}

/*
 * Get partition type name
 */
const char *partition_type_name(uint8_t type) {
    switch (type) {
        case MBR_TYPE_EMPTY:        return "Empty";
        case MBR_TYPE_FAT12:        return "FAT12";
        case MBR_TYPE_FAT16_SMALL:  return "FAT16 (<32M)";
        case MBR_TYPE_EXTENDED:     return "Extended";
        case MBR_TYPE_FAT16:        return "FAT16";
        case MBR_TYPE_NTFS:         return "NTFS/exFAT";
        case MBR_TYPE_FAT32:        return "FAT32";
        case MBR_TYPE_FAT32_LBA:    return "FAT32 LBA";
        case MBR_TYPE_FAT16_LBA:    return "FAT16 LBA";
        case MBR_TYPE_EXTENDED_LBA: return "Extended LBA";
        case MBR_TYPE_LINUX_SWAP:   return "Linux swap";
        case MBR_TYPE_LINUX:        return "Linux";
        case MBR_TYPE_LINUX_LVM:    return "Linux LVM";
        case MBR_TYPE_GPT_PROTECTIVE: return "GPT Protective";
        case 0xEF:                  return "EFI System";
        default:                    return "Unknown";
    }
}

/*
 * Initialize partition subsystem
 */
void partition_init(void) {
    partition_cache = kmem_cache_create("partition_dev",
                                        sizeof(partition_dev_t),
                                        8,   /* align */
                                        0);  /* flags */
    if (!partition_cache) {
        ERROR("partition: failed to create slab cache");
        return;
    }

    INFO("partition: subsystem initialized");
}
