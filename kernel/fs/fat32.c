/* fat32.c - FAT32 filesystem implementation */

#include "fat32.h"
#include "vfs.h"
#include "../drivers/block.h"
#include "../lib/string.h"
#include "../mm/slab.h"
#include "../debug/debug.h"

/* Slab caches */
static kmem_cache_t *fat32_fs_cache;
static kmem_cache_t *fat32_inode_info_cache;

/* Forward declarations */
static node_ops_t fat32_file_ops;
static node_ops_t fat32_dir_ops;
static vfs_node_t *fat32_mount(const char *source, uint32_t flags);
static int fat32_unmount(vfs_mount_t *mount);

/* Filesystem operations */
static fs_ops_t fat32_fs_ops = {
    .name = "fat32",
    .mount = fat32_mount,
    .unmount = fat32_unmount,
};

/* ============================================================================
 * Low-level I/O
 * ========================================================================== */

/*
 * Read sectors from the underlying device
 */
static int fat32_read_sectors(fat32_fs_t *fs, uint64_t sector, uint32_t count, void *buf) {
    return block_read(fs->dev, sector, count, buf);
}

/*
 * Write sectors to the underlying device
 */
static int fat32_write_sectors(fat32_fs_t *fs, uint64_t sector, uint32_t count, const void *buf) {
    if (fs->read_only) {
        return -1;
    }
    return block_write(fs->dev, sector, count, buf);
}

/* ============================================================================
 * Cluster Operations
 * ========================================================================== */

/*
 * Convert cluster number to sector number
 */
static inline uint32_t cluster_to_sector(fat32_fs_t *fs, uint32_t cluster) {
    return fs->data_start_sector + (cluster - 2) * fs->sectors_per_cluster;
}

/*
 * Read a cluster into buffer
 */
static int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buf) {
    if (!FAT32_IS_VALID(cluster)) {
        return -1;
    }
    uint32_t sector = cluster_to_sector(fs, cluster);
    return fat32_read_sectors(fs, sector, fs->sectors_per_cluster, buf);
}

/*
 * Write a cluster from buffer
 */
static int fat32_write_cluster(fat32_fs_t *fs, uint32_t cluster, const void *buf) {
    if (!FAT32_IS_VALID(cluster)) {
        return -1;
    }
    uint32_t sector = cluster_to_sector(fs, cluster);
    return fat32_write_sectors(fs, sector, fs->sectors_per_cluster, buf);
}

/* ============================================================================
 * FAT Cache Operations
 * ========================================================================== */

/*
 * Flush FAT cache to disk
 */
static int fat32_flush_fat(fat32_fs_t *fs) {
    if (!fs->fat_cache_dirty || fs->read_only) {
        return 0;
    }

    /* Write to all FATs */
    for (int i = 0; i < fs->bpb.num_fats; i++) {
        uint32_t sector = fs->fat_start_sector + fs->fat_cache_sector +
                          (i * fs->fat_sectors);
        if (fat32_write_sectors(fs, sector, 1, fs->fat_cache) != 0) {
            ERROR("fat32: failed to write FAT sector %u", sector);
            return -1;
        }
    }

    fs->fat_cache_dirty = false;
    return 0;
}

/*
 * Load a FAT sector into cache
 */
static int fat32_cache_fat_sector(fat32_fs_t *fs, uint32_t sector) {
    if (fs->fat_cache_sector == sector) {
        return 0;  /* Already cached */
    }

    /* Flush current cache if dirty */
    if (fat32_flush_fat(fs) != 0) {
        return -1;
    }

    /* Read new sector */
    uint32_t abs_sector = fs->fat_start_sector + sector;
    if (fat32_read_sectors(fs, abs_sector, 1, fs->fat_cache) != 0) {
        ERROR("fat32: failed to read FAT sector %u", abs_sector);
        return -1;
    }

    fs->fat_cache_sector = sector;
    return 0;
}

/*
 * Get the next cluster in a chain
 */
static uint32_t fat32_get_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (!FAT32_IS_VALID(cluster)) {
        return 0;
    }

    /* Calculate which FAT sector contains this cluster's entry */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bpb.bytes_per_sector;
    uint32_t entry_offset = (fat_offset % fs->bpb.bytes_per_sector) / 4;

    if (fat32_cache_fat_sector(fs, fat_sector) != 0) {
        return 0;
    }

    return fs->fat_cache[entry_offset] & FAT32_MASK;
}

/*
 * Set a cluster's FAT entry
 */
static int fat32_set_cluster(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (fs->read_only) {
        return -1;
    }

    /* Calculate which FAT sector contains this cluster's entry */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_offset / fs->bpb.bytes_per_sector;
    uint32_t entry_offset = (fat_offset % fs->bpb.bytes_per_sector) / 4;

    if (fat32_cache_fat_sector(fs, fat_sector) != 0) {
        return -1;
    }

    /* Preserve high 4 bits (reserved) */
    fs->fat_cache[entry_offset] = (fs->fat_cache[entry_offset] & 0xF0000000) |
                                  (value & FAT32_MASK);
    fs->fat_cache_dirty = true;
    fs->dirty = true;

    return 0;
}

/*
 * Allocate a free cluster
 */
static uint32_t fat32_alloc_cluster(fat32_fs_t *fs) {
    if (fs->read_only || fs->free_clusters == 0) {
        return 0;
    }

    /* Start search from next_free hint */
    uint32_t start = (fs->next_free >= 2) ? fs->next_free : 2;
    uint32_t cluster = start;

    do {
        uint32_t next = fat32_get_next_cluster(fs, cluster);
        if (next == 0) {
            /* Check if actually free (not an error) */
            uint32_t fat_offset = cluster * 4;
            uint32_t fat_sector = fat_offset / fs->bpb.bytes_per_sector;
            uint32_t entry_offset = (fat_offset % fs->bpb.bytes_per_sector) / 4;

            if (fat32_cache_fat_sector(fs, fat_sector) == 0) {
                if ((fs->fat_cache[entry_offset] & FAT32_MASK) == FAT32_FREE) {
                    /* Found free cluster - mark as end of chain */
                    if (fat32_set_cluster(fs, cluster, FAT32_EOC) == 0) {
                        fs->next_free = cluster + 1;
                        if (fs->free_clusters != 0xFFFFFFFF) {
                            fs->free_clusters--;
                        }
                        return cluster;
                    }
                }
            }
        }

        cluster++;
        if (cluster >= fs->total_clusters + 2) {
            cluster = 2;  /* Wrap around */
        }
    } while (cluster != start);

    return 0;  /* No free clusters */
}

/*
 * Free a cluster chain starting at given cluster
 */
static int fat32_free_chain(fat32_fs_t *fs, uint32_t cluster) {
    if (fs->read_only) {
        return -1;
    }

    while (FAT32_IS_VALID(cluster)) {
        uint32_t next = fat32_get_next_cluster(fs, cluster);
        if (fat32_set_cluster(fs, cluster, FAT32_FREE) != 0) {
            return -1;
        }
        if (fs->free_clusters != 0xFFFFFFFF) {
            fs->free_clusters++;
        }
        cluster = next;
    }

    return 0;
}

/*
 * Extend a cluster chain
 * If chain is 0, starts a new chain
 * Returns the newly allocated cluster
 */
static uint32_t fat32_extend_chain(fat32_fs_t *fs, uint32_t chain) {
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (new_cluster == 0) {
        return 0;
    }

    if (chain != 0) {
        /* Find end of existing chain */
        uint32_t last = chain;
        uint32_t next;
        while ((next = fat32_get_next_cluster(fs, last)) != 0 &&
               FAT32_IS_VALID(next)) {
            last = next;
        }

        /* Link new cluster */
        if (fat32_set_cluster(fs, last, new_cluster) != 0) {
            fat32_set_cluster(fs, new_cluster, FAT32_FREE);
            return 0;
        }
    }

    return new_cluster;
}

/* ============================================================================
 * Short Name Utilities
 * ========================================================================== */

/*
 * Calculate LFN checksum from short name
 */
static uint8_t fat32_lfn_checksum(const char *shortname) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)shortname[i];
    }
    return sum;
}

/*
 * Convert 8.3 name to normal string
 */
static void fat32_short_to_name(const fat_dirent_t *de, char *name) {
    int i, j = 0;

    /* Handle deleted entry marker */
    if ((uint8_t)de->name[0] == FAT_DIRENT_KANJI) {
        name[j++] = (char)0xE5;
        i = 1;
    } else {
        i = 0;
    }

    /* Copy name part */
    for (; i < 8 && de->name[i] != ' '; i++) {
        name[j++] = de->name[i];
    }

    /* Add extension if present */
    if (de->ext[0] != ' ') {
        name[j++] = '.';
        for (i = 0; i < 3 && de->ext[i] != ' '; i++) {
            name[j++] = de->ext[i];
        }
    }

    name[j] = '\0';

    /* Convert to lowercase for display */
    for (i = 0; name[i]; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            name[i] += 32;
        }
    }
}

/*
 * Generate 8.3 short name from long name
 * Returns true if name was truncated/modified
 */
static bool fat32_generate_short_name(const char *longname, char *shortname,
                                      int *suffix_num) {
    char basis[13];
    int bi = 0;
    bool lossy = false;
    bool has_ext = false;
    int ext_start = -1;

    /* Find last dot for extension */
    int len = strlen(longname);
    for (int i = len - 1; i >= 0; i--) {
        if (longname[i] == '.') {
            ext_start = i;
            has_ext = true;
            break;
        }
    }

    /* Build basis name (up to 8 chars) */
    int name_end = has_ext ? ext_start : len;
    for (int i = 0; i < name_end && bi < 8; i++) {
        char c = longname[i];

        /* Skip leading/embedded spaces and dots */
        if (c == ' ' || c == '.') {
            lossy = true;
            continue;
        }

        /* Convert to uppercase */
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        }

        /* Replace invalid characters */
        if (c < 0x20 || strchr("+,;=[]", c)) {
            c = '_';
            lossy = true;
        }

        basis[bi++] = c;
    }

    if (bi == 0) {
        /* Empty name */
        basis[0] = '_';
        bi = 1;
        lossy = true;
    }

    if (name_end > 8) {
        lossy = true;
    }

    /* Pad with spaces */
    while (bi < 8) {
        basis[bi++] = ' ';
    }

    /* Build extension (up to 3 chars) */
    if (has_ext && ext_start + 1 < len) {
        int ei = 0;
        for (int i = ext_start + 1; i < len && ei < 3; i++) {
            char c = longname[i];

            if (c >= 'a' && c <= 'z') {
                c -= 32;
            }

            if (c < 0x20 || c == ' ' || strchr("+,;=[].", c)) {
                c = '_';
                lossy = true;
            }

            basis[8 + ei++] = c;
        }

        if (len - ext_start - 1 > 3) {
            lossy = true;
        }

        while (ei < 3) {
            basis[8 + ei++] = ' ';
        }
    } else {
        memset(basis + 8, ' ', 3);
    }

    basis[11] = '\0';

    /* If numeric suffix needed */
    if (*suffix_num > 0 && *suffix_num < 1000000) {
        char suffix[8];
        snprintf(suffix, sizeof(suffix), "~%d", *suffix_num);
        int slen = strlen(suffix);
        int pos = 8 - slen;
        if (pos < 1) pos = 1;

        /* Truncate basis name and add suffix */
        memcpy(basis + pos, suffix, slen);
        lossy = true;
    }

    memcpy(shortname, basis, 11);
    return lossy;
}

/* ============================================================================
 * Long Filename Support
 * ========================================================================== */

/*
 * Extract characters from an LFN entry
 */
static void fat32_lfn_get_chars(const fat_lfn_entry_t *lfn, uint16_t *chars) {
    memcpy(chars, lfn->name1, 5 * 2);
    memcpy(chars + 5, lfn->name2, 6 * 2);
    memcpy(chars + 11, lfn->name3, 2 * 2);
}

/*
 * Set characters in an LFN entry
 */
static void fat32_lfn_set_chars(fat_lfn_entry_t *lfn, const uint16_t *chars) {
    memcpy(lfn->name1, chars, 5 * 2);
    memcpy(lfn->name2, chars + 5, 6 * 2);
    memcpy(lfn->name3, chars + 11, 2 * 2);
}

/*
 * Parse LFN entries and build long filename
 * Returns number of LFN entries consumed
 * Note: Currently unused as LFN parsing is done in callbacks, but kept for future use
 */
__attribute__((unused))
static int fat32_parse_lfn(fat_dirent_t *entries, int count, char *name) {
    uint16_t lfn_buf[FAT_LFN_MAX_LEN + 1];
    int lfn_len = 0;
    int lfn_entries = 0;

    /* Check if first entry is an LFN entry */
    fat_lfn_entry_t *first = (fat_lfn_entry_t *)&entries[0];
    if (first->attr != FAT_ATTR_LFN) {
        return 0;  /* No LFN */
    }

    /* Count LFN entries and get expected checksum */
    int expected_order = first->order & ~FAT_LFN_LAST;
    uint8_t checksum = first->checksum;

    for (int i = 0; i < count && i < expected_order; i++) {
        fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&entries[i];

        if (lfn->attr != FAT_ATTR_LFN) {
            break;
        }

        int order = lfn->order & ~FAT_LFN_LAST;
        if (order != expected_order - i) {
            break;  /* Out of sequence */
        }

        if (lfn->checksum != checksum) {
            break;  /* Checksum mismatch */
        }

        lfn_entries++;

        /* Extract characters into correct position */
        uint16_t chars[13];
        fat32_lfn_get_chars(lfn, chars);

        int pos = (order - 1) * FAT_LFN_CHARS;
        for (int j = 0; j < FAT_LFN_CHARS && pos + j < FAT_LFN_MAX_LEN; j++) {
            lfn_buf[pos + j] = chars[j];
            if (chars[j] == 0) {
                lfn_len = pos + j;
                goto done_reading;
            }
            if (pos + j >= lfn_len) {
                lfn_len = pos + j + 1;
            }
        }
    }

done_reading:
    if (lfn_entries == 0) {
        return 0;
    }

    /* Verify checksum against short name entry */
    if (lfn_entries < count) {
        fat_dirent_t *short_entry = &entries[lfn_entries];
        char shortname[11];
        memcpy(shortname, short_entry->name, 8);
        memcpy(shortname + 8, short_entry->ext, 3);

        if (fat32_lfn_checksum(shortname) != checksum) {
            return 0;  /* Checksum mismatch */
        }
    }

    /* Convert UTF-16LE to ASCII (simple conversion) */
    int j = 0;
    for (int i = 0; i < lfn_len && j < VFS_NAME_MAX; i++) {
        uint16_t c = lfn_buf[i];
        if (c == 0 || c == 0xFFFF) {
            break;
        }
        if (c < 128) {
            name[j++] = (char)c;
        } else {
            name[j++] = '?';  /* Non-ASCII placeholder */
        }
    }
    name[j] = '\0';

    return lfn_entries;
}

/*
 * Create LFN entries for a long filename
 * Returns number of LFN entries created
 */
static int fat32_create_lfn(const char *name, uint8_t checksum,
                            fat_lfn_entry_t *entries, int max_entries) {
    int name_len = strlen(name);
    int num_entries = (name_len + FAT_LFN_CHARS - 1) / FAT_LFN_CHARS;

    if (num_entries > max_entries || num_entries > FAT_LFN_MAX_ENTRIES) {
        return 0;
    }

    /* Convert ASCII to UTF-16LE (simple conversion) */
    uint16_t lfn_buf[FAT_LFN_MAX_LEN + 1];
    for (int i = 0; i < name_len; i++) {
        lfn_buf[i] = (uint8_t)name[i];
    }
    lfn_buf[name_len] = 0;

    /* Pad with 0xFFFF */
    for (int i = name_len + 1; i < (num_entries * FAT_LFN_CHARS); i++) {
        lfn_buf[i] = 0xFFFF;
    }

    /* Create entries in reverse order */
    for (int i = 0; i < num_entries; i++) {
        int order = num_entries - i;
        fat_lfn_entry_t *lfn = &entries[i];

        memset(lfn, 0, sizeof(*lfn));
        lfn->order = order;
        if (i == 0) {
            lfn->order |= FAT_LFN_LAST;
        }
        lfn->attr = FAT_ATTR_LFN;
        lfn->type = 0;
        lfn->checksum = checksum;
        lfn->cluster = 0;

        fat32_lfn_set_chars(lfn, &lfn_buf[(order - 1) * FAT_LFN_CHARS]);
    }

    return num_entries;
}

/* ============================================================================
 * Directory Operations
 * ========================================================================== */

/*
 * Read directory entries from a cluster chain
 * Calls callback for each entry (including LFN entries)
 */
typedef int (*dir_entry_callback_t)(fat32_fs_t *fs, uint32_t cluster,
                                    uint32_t index, fat_dirent_t *entry,
                                    void *ctx);

static int fat32_iterate_dir(fat32_fs_t *fs, uint32_t start_cluster,
                             dir_entry_callback_t callback, void *ctx) {
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return VFS_ENOMEM;
    }

    uint32_t cluster = start_cluster;
    uint32_t global_index = 0;

    while (FAT32_IS_VALID(cluster)) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return VFS_EIO;
        }

        fat_dirent_t *entries = (fat_dirent_t *)cluster_buf;
        for (uint32_t i = 0; i < fs->entries_per_cluster; i++) {
            fat_dirent_t *entry = &entries[i];

            /* End of directory */
            if (entry->name[0] == FAT_DIRENT_END) {
                kfree(cluster_buf);
                return VFS_OK;
            }

            int ret = callback(fs, cluster, global_index + i, entry, ctx);
            if (ret != 0) {
                kfree(cluster_buf);
                return ret;
            }
        }

        global_index += fs->entries_per_cluster;
        cluster = fat32_get_next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    return VFS_OK;
}

/* Context for find operation */
typedef struct {
    const char *name;
    fat_dirent_t *result;
    uint32_t result_cluster;
    uint32_t result_index;
    bool found;
    /* LFN state */
    char lfn_name[VFS_NAME_MAX + 1];
    int lfn_index;
} find_ctx_t;

static int fat32_find_callback(fat32_fs_t *fs, uint32_t cluster,
                               uint32_t index, fat_dirent_t *entry, void *ctx) {
    find_ctx_t *fctx = (find_ctx_t *)ctx;
    (void)fs;

    /* Skip free entries */
    if ((uint8_t)entry->name[0] == FAT_DIRENT_FREE) {
        fctx->lfn_index = -1;  /* Reset LFN state */
        return 0;
    }

    /* Handle LFN entries */
    if (entry->attr == FAT_ATTR_LFN) {
        fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)entry;

        if (lfn->order & FAT_LFN_LAST) {
            /* Start of new LFN sequence */
            fctx->lfn_index = index;
            memset(fctx->lfn_name, 0, sizeof(fctx->lfn_name));
        }

        /* Extract characters */
        int order = lfn->order & ~FAT_LFN_LAST;
        int pos = (order - 1) * FAT_LFN_CHARS;

        uint16_t chars[13];
        fat32_lfn_get_chars(lfn, chars);

        for (int i = 0; i < FAT_LFN_CHARS && pos + i < VFS_NAME_MAX; i++) {
            if (chars[i] == 0 || chars[i] == 0xFFFF) break;
            fctx->lfn_name[pos + i] = (chars[i] < 128) ? (char)chars[i] : '?';
        }

        return 0;  /* Continue */
    }

    /* Skip volume label */
    if (entry->attr & FAT_ATTR_VOLUME_ID) {
        fctx->lfn_index = -1;
        return 0;
    }

    /* Regular or directory entry - check name */
    char shortname[13];
    fat32_short_to_name(entry, shortname);

    /* Check long name first, then short name */
    bool match = false;
    if (fctx->lfn_name[0] != '\0') {
        match = (strcasecmp(fctx->lfn_name, fctx->name) == 0);
    }
    if (!match) {
        match = (strcasecmp(shortname, fctx->name) == 0);
    }

    if (match) {
        memcpy(fctx->result, entry, sizeof(fat_dirent_t));
        fctx->result_cluster = cluster;
        fctx->result_index = index;
        fctx->found = true;
        return 1;  /* Stop iteration */
    }

    fctx->lfn_index = -1;
    memset(fctx->lfn_name, 0, sizeof(fctx->lfn_name));
    return 0;
}

/*
 * Find an entry in a directory
 */
static int fat32_find_in_dir(fat32_fs_t *fs, uint32_t dir_cluster,
                             const char *name, fat_dirent_t *result,
                             uint32_t *out_cluster, uint32_t *out_index) {
    find_ctx_t ctx = {
        .name = name,
        .result = result,
        .found = false,
        .lfn_index = -1,
    };

    int ret = fat32_iterate_dir(fs, dir_cluster, fat32_find_callback, &ctx);
    if (ret < 0) {
        return ret;
    }

    if (!ctx.found) {
        return VFS_ENOENT;
    }

    if (out_cluster) *out_cluster = ctx.result_cluster;
    if (out_index) *out_index = ctx.result_index;
    return VFS_OK;
}

/* Context for readdir operation */
typedef struct {
    uint32_t target_index;
    uint32_t current_index;
    dirent_t *dent;
    bool found;
    /* LFN state */
    char lfn_name[VFS_NAME_MAX + 1];
    int lfn_start;
} readdir_ctx_t;

static int fat32_readdir_callback(fat32_fs_t *fs, uint32_t cluster,
                                  uint32_t index, fat_dirent_t *entry, void *ctx) {
    readdir_ctx_t *rctx = (readdir_ctx_t *)ctx;
    (void)fs;
    (void)cluster;
    (void)index;

    /* Skip free entries */
    if ((uint8_t)entry->name[0] == FAT_DIRENT_FREE) {
        rctx->lfn_start = -1;
        return 0;
    }

    /* Handle LFN entries */
    if (entry->attr == FAT_ATTR_LFN) {
        fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)entry;

        if (lfn->order & FAT_LFN_LAST) {
            rctx->lfn_start = index;
            memset(rctx->lfn_name, 0, sizeof(rctx->lfn_name));
        }

        int order = lfn->order & ~FAT_LFN_LAST;
        int pos = (order - 1) * FAT_LFN_CHARS;

        uint16_t chars[13];
        fat32_lfn_get_chars(lfn, chars);

        for (int i = 0; i < FAT_LFN_CHARS && pos + i < VFS_NAME_MAX; i++) {
            if (chars[i] == 0 || chars[i] == 0xFFFF) break;
            rctx->lfn_name[pos + i] = (chars[i] < 128) ? (char)chars[i] : '?';
        }

        return 0;
    }

    /* Skip volume label */
    if (entry->attr & FAT_ATTR_VOLUME_ID) {
        rctx->lfn_start = -1;
        return 0;
    }

    /* This is a real entry - check if it's the target */
    if (rctx->current_index == rctx->target_index) {
        /* Use LFN if available, otherwise short name */
        if (rctx->lfn_name[0] != '\0') {
            strncpy(rctx->dent->name, rctx->lfn_name, VFS_NAME_MAX);
        } else {
            fat32_short_to_name(entry, rctx->dent->name);
        }
        rctx->dent->name[VFS_NAME_MAX] = '\0';

        rctx->dent->type = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIR : VFS_FILE;
        rctx->dent->inode = FAT_GET_CLUSTER(entry);

        rctx->found = true;
        return 1;  /* Stop iteration */
    }

    rctx->current_index++;
    rctx->lfn_start = -1;
    memset(rctx->lfn_name, 0, sizeof(rctx->lfn_name));
    return 0;
}

/* Context for finding free entries */
typedef struct {
    int needed;         /* Entries needed */
    int consecutive;    /* Current consecutive free count */
    uint32_t start_cluster;
    uint32_t start_index;
    bool found;
} free_entry_ctx_t;

static int fat32_find_free_callback(fat32_fs_t *fs, uint32_t cluster,
                                    uint32_t index, fat_dirent_t *entry, void *ctx) {
    free_entry_ctx_t *fctx = (free_entry_ctx_t *)ctx;
    (void)fs;

    if ((uint8_t)entry->name[0] == FAT_DIRENT_FREE ||
        (uint8_t)entry->name[0] == FAT_DIRENT_END) {
        if (fctx->consecutive == 0) {
            fctx->start_cluster = cluster;
            fctx->start_index = index;
        }
        fctx->consecutive++;

        if (fctx->consecutive >= fctx->needed) {
            fctx->found = true;
            return 1;  /* Found enough */
        }

        if ((uint8_t)entry->name[0] == FAT_DIRENT_END) {
            /* End of dir - we have space */
            fctx->found = true;
            return 1;
        }
    } else {
        fctx->consecutive = 0;
    }

    return 0;
}

/*
 * Add an entry to a directory
 */
static int fat32_add_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster,
                               const char *name, uint8_t attr,
                               uint32_t first_cluster, uint32_t file_size,
                               uint32_t *out_cluster, uint32_t *out_index) {
    int name_len = strlen(name);

    /* Generate short name */
    char shortname[11];
    int suffix = 0;
    bool needs_lfn = fat32_generate_short_name(name, shortname, &suffix);

    /* Check for conflicts and regenerate with suffix if needed */
    fat_dirent_t existing;
    while (suffix < 999999) {
        char check_name[13];
        memcpy(check_name, shortname, 8);
        check_name[8] = '\0';
        for (int i = 7; i >= 0 && check_name[i] == ' '; i--) {
            check_name[i] = '\0';
        }
        if (shortname[8] != ' ') {
            strcat(check_name, ".");
            strncat(check_name, shortname + 8, 3);
        }

        if (fat32_find_in_dir(fs, dir_cluster, check_name, &existing, NULL, NULL) != VFS_OK) {
            break;  /* Name is available */
        }

        suffix++;
        fat32_generate_short_name(name, shortname, &suffix);
        needs_lfn = true;
    }

    /* Calculate entries needed */
    int lfn_entries = 0;
    if (needs_lfn || name_len > 11) {
        lfn_entries = (name_len + FAT_LFN_CHARS - 1) / FAT_LFN_CHARS;
    }
    int total_entries = lfn_entries + 1;

    /* Find free entries */
    free_entry_ctx_t fctx = {
        .needed = total_entries,
        .consecutive = 0,
        .found = false,
    };

    fat32_iterate_dir(fs, dir_cluster, fat32_find_free_callback, &fctx);

    if (!fctx.found) {
        /* Need to extend directory */
        uint32_t new_cluster = fat32_extend_chain(fs, dir_cluster);
        if (new_cluster == 0) {
            return VFS_ENOSPC;
        }

        /* Zero out new cluster */
        uint8_t *zeros = kmalloc(fs->cluster_size);
        if (zeros) {
            memset(zeros, 0, fs->cluster_size);
            fat32_write_cluster(fs, new_cluster, zeros);
            kfree(zeros);
        }

        /* Retry finding free space */
        fctx.consecutive = 0;
        fctx.found = false;
        fat32_iterate_dir(fs, dir_cluster, fat32_find_free_callback, &fctx);

        if (!fctx.found) {
            return VFS_ENOSPC;
        }
    }

    /* Build entries */
    fat_dirent_t *entries = kmalloc(total_entries * sizeof(fat_dirent_t));
    if (!entries) {
        return VFS_ENOMEM;
    }

    /* Create LFN entries if needed */
    uint8_t checksum = fat32_lfn_checksum(shortname);
    if (lfn_entries > 0) {
        fat32_create_lfn(name, checksum, (fat_lfn_entry_t *)entries, lfn_entries);
    }

    /* Create short name entry */
    fat_dirent_t *de = &entries[lfn_entries];
    memset(de, 0, sizeof(fat_dirent_t));
    memcpy(de->name, shortname, 8);
    memcpy(de->ext, shortname + 8, 3);
    de->attr = attr;
    FAT_SET_CLUSTER(de, first_cluster);
    de->file_size = file_size;
    /* TODO: Set creation/modification times */

    /* Write entries to disk */
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        kfree(entries);
        return VFS_ENOMEM;
    }

    /* Read cluster containing first entry */
    if (fat32_read_cluster(fs, fctx.start_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        kfree(entries);
        return VFS_EIO;
    }

    /* Write entries (may span clusters) */
    uint32_t current_cluster = fctx.start_cluster;
    uint32_t current_index = fctx.start_index;

    for (int i = 0; i < total_entries; i++) {
        /* Check if we need next cluster */
        if (current_index >= fs->entries_per_cluster) {
            /* Write current cluster */
            if (fat32_write_cluster(fs, current_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                kfree(entries);
                return VFS_EIO;
            }

            /* Move to next cluster */
            current_cluster = fat32_get_next_cluster(fs, current_cluster);
            if (!FAT32_IS_VALID(current_cluster)) {
                kfree(cluster_buf);
                kfree(entries);
                return VFS_ENOSPC;
            }

            if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                kfree(entries);
                return VFS_EIO;
            }

            current_index = 0;
        }

        memcpy(cluster_buf + current_index * sizeof(fat_dirent_t),
               &entries[i], sizeof(fat_dirent_t));
        current_index++;
    }

    /* Write final cluster */
    if (fat32_write_cluster(fs, current_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        kfree(entries);
        return VFS_EIO;
    }

    if (out_cluster) *out_cluster = current_cluster;
    if (out_index) *out_index = current_index - 1;

    kfree(cluster_buf);
    kfree(entries);
    return VFS_OK;
}

/*
 * Remove an entry from a directory
 */
static int fat32_remove_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster,
                                  uint32_t entry_cluster, uint32_t entry_index) {
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return VFS_ENOMEM;
    }

    /* Read the cluster */
    if (fat32_read_cluster(fs, entry_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return VFS_EIO;
    }

    fat_dirent_t *entries = (fat_dirent_t *)cluster_buf;
    uint32_t local_index = entry_index % fs->entries_per_cluster;

    /* Mark short entry as deleted */
    entries[local_index].name[0] = FAT_DIRENT_FREE;

    /* Mark preceding LFN entries as deleted */
    /* We need to search backwards for LFN entries */
    uint32_t cluster = dir_cluster;
    while (FAT32_IS_VALID(cluster)) {
        if (cluster == entry_cluster) {
            /* Same cluster - mark LFN entries */
            for (int i = (int)local_index - 1; i >= 0; i--) {
                if (entries[i].attr == FAT_ATTR_LFN) {
                    entries[i].name[0] = FAT_DIRENT_FREE;
                } else {
                    break;
                }
            }
            break;
        }
        cluster = fat32_get_next_cluster(fs, cluster);
    }

    /* Write back */
    if (fat32_write_cluster(fs, entry_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        return VFS_EIO;
    }

    kfree(cluster_buf);
    return VFS_OK;
}

/* ============================================================================
 * VFS Node Creation
 * ========================================================================== */

/*
 * Create a VFS node from a directory entry
 */
static vfs_node_t *fat32_create_node(fat32_fs_t *fs, const char *name,
                                     fat_dirent_t *de, uint32_t dir_cluster,
                                     uint32_t dir_index) {
    vfs_node_t *node = vfs_node_alloc();
    if (!node) {
        return NULL;
    }

    fat32_inode_info_t *info = kmem_cache_alloc(fat32_inode_info_cache);
    if (!info) {
        vfs_node_free(node);
        return NULL;
    }

    info->fs = fs;
    info->first_cluster = FAT_GET_CLUSTER(de);
    info->dir_cluster = dir_cluster;
    info->dir_index = dir_index;
    memcpy(&info->dirent, de, sizeof(fat_dirent_t));
    info->dirty = false;

    strncpy(node->name, name, VFS_NAME_MAX);
    node->name[VFS_NAME_MAX] = '\0';
    node->type = (de->attr & FAT_ATTR_DIRECTORY) ? VFS_DIR : VFS_FILE;
    node->size = de->file_size;
    node->permissions = (de->attr & FAT_ATTR_READ_ONLY) ? VFS_PERM_READ : (VFS_PERM_READ | VFS_PERM_WRITE);
    if (de->attr & FAT_ATTR_DIRECTORY) {
        node->permissions |= VFS_PERM_EXEC;
    }
    node->private = info;
    node->ops = (de->attr & FAT_ATTR_DIRECTORY) ? &fat32_dir_ops : &fat32_file_ops;

    return node;
}

/* ============================================================================
 * File Operations
 * ========================================================================== */

static int fat32_file_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void fat32_file_close(vfs_node_t *node) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    if (info && info->dirty) {
        /* Writeback directory entry */
        fat32_fs_t *fs = info->fs;

        uint8_t *cluster_buf = kmalloc(fs->cluster_size);
        if (cluster_buf) {
            if (fat32_read_cluster(fs, info->dir_cluster, cluster_buf) == 0) {
                uint32_t local_idx = info->dir_index % fs->entries_per_cluster;
                memcpy(cluster_buf + local_idx * sizeof(fat_dirent_t),
                       &info->dirent, sizeof(fat_dirent_t));
                fat32_write_cluster(fs, info->dir_cluster, cluster_buf);
            }
            kfree(cluster_buf);
        }
        info->dirty = false;
    }
}

static ssize_t fat32_file_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    fat32_fs_t *fs = info->fs;

    if (offset >= node->size) {
        return 0;
    }

    if (offset + size > node->size) {
        size = node->size - offset;
    }

    if (size == 0) {
        return 0;
    }

    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return VFS_ENOMEM;
    }

    /* Find starting cluster */
    uint32_t cluster = info->first_cluster;
    uint32_t cluster_offset = offset / fs->cluster_size;

    for (uint32_t i = 0; i < cluster_offset && FAT32_IS_VALID(cluster); i++) {
        cluster = fat32_get_next_cluster(fs, cluster);
    }

    if (!FAT32_IS_VALID(cluster)) {
        kfree(cluster_buf);
        return 0;
    }

    /* Read data */
    size_t bytes_read = 0;
    uint32_t pos_in_cluster = offset % fs->cluster_size;

    while (bytes_read < size && FAT32_IS_VALID(cluster)) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return (bytes_read > 0) ? (ssize_t)bytes_read : VFS_EIO;
        }

        size_t to_copy = fs->cluster_size - pos_in_cluster;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }

        memcpy((uint8_t *)buf + bytes_read, cluster_buf + pos_in_cluster, to_copy);
        bytes_read += to_copy;
        pos_in_cluster = 0;

        cluster = fat32_get_next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    return (ssize_t)bytes_read;
}

static ssize_t fat32_file_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    if (size == 0) {
        return 0;
    }

    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return VFS_ENOMEM;
    }

    /* Ensure we have at least one cluster */
    if (info->first_cluster == 0) {
        info->first_cluster = fat32_alloc_cluster(fs);
        if (info->first_cluster == 0) {
            kfree(cluster_buf);
            return VFS_ENOSPC;
        }
        FAT_SET_CLUSTER(&info->dirent, info->first_cluster);
        info->dirty = true;

        /* Zero out the new cluster */
        memset(cluster_buf, 0, fs->cluster_size);
        fat32_write_cluster(fs, info->first_cluster, cluster_buf);
    }

    /* Navigate to starting cluster */
    uint32_t cluster = info->first_cluster;
    uint32_t cluster_num = offset / fs->cluster_size;

    for (uint32_t i = 0; i < cluster_num; i++) {
        uint32_t next = fat32_get_next_cluster(fs, cluster);
        if (!FAT32_IS_VALID(next)) {
            /* Extend chain */
            next = fat32_extend_chain(fs, cluster);
            if (next == 0) {
                kfree(cluster_buf);
                return VFS_ENOSPC;
            }
            /* Zero new cluster */
            memset(cluster_buf, 0, fs->cluster_size);
            fat32_write_cluster(fs, next, cluster_buf);
        }
        cluster = next;
    }

    /* Write data */
    size_t bytes_written = 0;
    uint32_t pos_in_cluster = offset % fs->cluster_size;

    while (bytes_written < size) {
        /* Read current cluster contents if partial write */
        if (pos_in_cluster > 0 || size - bytes_written < fs->cluster_size) {
            if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return (bytes_written > 0) ? (ssize_t)bytes_written : VFS_EIO;
            }
        }

        size_t to_copy = fs->cluster_size - pos_in_cluster;
        if (to_copy > size - bytes_written) {
            to_copy = size - bytes_written;
        }

        memcpy(cluster_buf + pos_in_cluster, (const uint8_t *)buf + bytes_written, to_copy);

        if (fat32_write_cluster(fs, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return (bytes_written > 0) ? (ssize_t)bytes_written : VFS_EIO;
        }

        bytes_written += to_copy;
        pos_in_cluster = 0;

        if (bytes_written < size) {
            uint32_t next = fat32_get_next_cluster(fs, cluster);
            if (!FAT32_IS_VALID(next)) {
                next = fat32_extend_chain(fs, cluster);
                if (next == 0) {
                    break;
                }
                memset(cluster_buf, 0, fs->cluster_size);
                fat32_write_cluster(fs, next, cluster_buf);
            }
            cluster = next;
        }
    }

    /* Update file size */
    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
        info->dirent.file_size = node->size;
        info->dirty = true;
    }

    kfree(cluster_buf);
    fs->dirty = true;
    return (ssize_t)bytes_written;
}

static int fat32_file_truncate(vfs_node_t *node, uint64_t size) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    if (size == node->size) {
        return VFS_OK;
    }

    if (size == 0) {
        /* Free entire chain */
        if (info->first_cluster != 0) {
            fat32_free_chain(fs, info->first_cluster);
            info->first_cluster = 0;
            FAT_SET_CLUSTER(&info->dirent, 0);
        }
    } else if (size < node->size) {
        /* Truncate chain */
        uint32_t clusters_needed = (size + fs->cluster_size - 1) / fs->cluster_size;
        uint32_t cluster = info->first_cluster;

        for (uint32_t i = 1; i < clusters_needed && FAT32_IS_VALID(cluster); i++) {
            cluster = fat32_get_next_cluster(fs, cluster);
        }

        if (FAT32_IS_VALID(cluster)) {
            uint32_t next = fat32_get_next_cluster(fs, cluster);
            if (FAT32_IS_VALID(next)) {
                fat32_set_cluster(fs, cluster, FAT32_EOC);
                fat32_free_chain(fs, next);
            }
        }
    }
    /* Growing is handled lazily in write */

    node->size = size;
    info->dirent.file_size = size;
    info->dirty = true;
    fs->dirty = true;

    return VFS_OK;
}

static int fat32_file_stat(vfs_node_t *node, vfs_stat_t *st) {
    st->size = node->size;
    st->type = node->type;
    st->permissions = node->permissions;
    st->uid = 0;
    st->gid = 0;
    st->nlink = 1;
    st->atime = 0;
    st->mtime = 0;
    st->ctime = 0;
    return VFS_OK;
}

/* ============================================================================
 * Directory Operations
 * ========================================================================== */

static int fat32_dir_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static void fat32_dir_close(vfs_node_t *node) {
    (void)node;
}

static int fat32_dir_readdir(vfs_node_t *node, dirent_t *dent, uint32_t index) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    fat32_fs_t *fs = info->fs;

    readdir_ctx_t ctx = {
        .target_index = index,
        .current_index = 0,
        .dent = dent,
        .found = false,
        .lfn_start = -1,
    };

    fat32_iterate_dir(fs, info->first_cluster, fat32_readdir_callback, &ctx);

    return ctx.found ? VFS_OK : VFS_ENOENT;
}

static vfs_node_t *fat32_dir_finddir(vfs_node_t *node, const char *name) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)node->private;
    fat32_fs_t *fs = info->fs;

    /* Handle . and .. */
    if (strcmp(name, ".") == 0) {
        vfs_node_ref(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        if (node->parent) {
            vfs_node_ref(node->parent);
            return node->parent;
        }
        vfs_node_ref(node);
        return node;
    }

    fat_dirent_t de;
    uint32_t de_cluster, de_index;

    if (fat32_find_in_dir(fs, info->first_cluster, name, &de, &de_cluster, &de_index) != VFS_OK) {
        return NULL;
    }

    return fat32_create_node(fs, name, &de, de_cluster, de_index);
}

static int fat32_dir_create(vfs_node_t *parent, const char *name, uint32_t type) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)parent->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    if (type != VFS_FILE) {
        return VFS_EINVAL;  /* Use mkdir for directories */
    }

    /* Check if already exists */
    fat_dirent_t existing;
    if (fat32_find_in_dir(fs, info->first_cluster, name, &existing, NULL, NULL) == VFS_OK) {
        return VFS_EEXIST;
    }

    /* Create entry (no cluster allocated yet - lazy allocation) */
    return fat32_add_dir_entry(fs, info->first_cluster, name, FAT_ATTR_ARCHIVE, 0, 0, NULL, NULL);
}

static int fat32_dir_unlink(vfs_node_t *parent, const char *name) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)parent->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    fat_dirent_t de;
    uint32_t de_cluster, de_index;

    if (fat32_find_in_dir(fs, info->first_cluster, name, &de, &de_cluster, &de_index) != VFS_OK) {
        return VFS_ENOENT;
    }

    /* Can't unlink directories */
    if (de.attr & FAT_ATTR_DIRECTORY) {
        return VFS_EISDIR;
    }

    /* Free cluster chain */
    uint32_t first_cluster = FAT_GET_CLUSTER(&de);
    if (first_cluster != 0) {
        fat32_free_chain(fs, first_cluster);
    }

    /* Remove directory entry */
    return fat32_remove_dir_entry(fs, info->first_cluster, de_cluster, de_index);
}

static int fat32_dir_mkdir(vfs_node_t *parent, const char *name) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)parent->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    /* Check if already exists */
    fat_dirent_t existing;
    if (fat32_find_in_dir(fs, info->first_cluster, name, &existing, NULL, NULL) == VFS_OK) {
        return VFS_EEXIST;
    }

    /* Allocate cluster for new directory */
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (new_cluster == 0) {
        return VFS_ENOSPC;
    }

    /* Initialize directory with . and .. entries */
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        fat32_free_chain(fs, new_cluster);
        return VFS_ENOMEM;
    }

    memset(cluster_buf, 0, fs->cluster_size);
    fat_dirent_t *entries = (fat_dirent_t *)cluster_buf;

    /* . entry */
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT_ATTR_DIRECTORY;
    FAT_SET_CLUSTER(&entries[0], new_cluster);

    /* .. entry */
    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT_ATTR_DIRECTORY;
    FAT_SET_CLUSTER(&entries[1], info->first_cluster);

    if (fat32_write_cluster(fs, new_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        fat32_free_chain(fs, new_cluster);
        return VFS_EIO;
    }

    kfree(cluster_buf);

    /* Add entry to parent directory */
    int ret = fat32_add_dir_entry(fs, info->first_cluster, name,
                                  FAT_ATTR_DIRECTORY, new_cluster, 0, NULL, NULL);
    if (ret != VFS_OK) {
        fat32_free_chain(fs, new_cluster);
        return ret;
    }

    return VFS_OK;
}

static int fat32_dir_rmdir(vfs_node_t *parent, const char *name) {
    fat32_inode_info_t *info = (fat32_inode_info_t *)parent->private;
    fat32_fs_t *fs = info->fs;

    if (fs->read_only) {
        return VFS_EINVAL;
    }

    fat_dirent_t de;
    uint32_t de_cluster, de_index;

    if (fat32_find_in_dir(fs, info->first_cluster, name, &de, &de_cluster, &de_index) != VFS_OK) {
        return VFS_ENOENT;
    }

    /* Must be a directory */
    if (!(de.attr & FAT_ATTR_DIRECTORY)) {
        return VFS_ENOTDIR;
    }

    uint32_t dir_cluster = FAT_GET_CLUSTER(&de);

    /* Check if directory is empty (only . and .. allowed) */
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return VFS_ENOMEM;
    }

    uint32_t cluster = dir_cluster;
    bool empty = true;

    while (FAT32_IS_VALID(cluster) && empty) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return VFS_EIO;
        }

        fat_dirent_t *entries = (fat_dirent_t *)cluster_buf;
        for (uint32_t i = 0; i < fs->entries_per_cluster; i++) {
            if (entries[i].name[0] == FAT_DIRENT_END) {
                break;
            }
            if ((uint8_t)entries[i].name[0] == FAT_DIRENT_FREE) {
                continue;
            }
            if (entries[i].attr == FAT_ATTR_LFN) {
                continue;
            }
            /* Skip . and .. */
            if (entries[i].name[0] == '.' && entries[i].name[1] == ' ') {
                continue;
            }
            if (entries[i].name[0] == '.' && entries[i].name[1] == '.' && entries[i].name[2] == ' ') {
                continue;
            }
            empty = false;
            break;
        }

        cluster = fat32_get_next_cluster(fs, cluster);
    }

    kfree(cluster_buf);

    if (!empty) {
        return VFS_ENOTEMPTY;
    }

    /* Free directory cluster chain */
    fat32_free_chain(fs, dir_cluster);

    /* Remove directory entry */
    return fat32_remove_dir_entry(fs, info->first_cluster, de_cluster, de_index);
}

static int fat32_dir_stat(vfs_node_t *node, vfs_stat_t *st) {
    st->size = 0;
    st->type = VFS_DIR;
    st->permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
    st->uid = 0;
    st->gid = 0;
    st->nlink = 2;
    st->atime = 0;
    st->mtime = 0;
    st->ctime = 0;
    return VFS_OK;
}

/* ============================================================================
 * Node Operations Tables
 * ========================================================================== */

static node_ops_t fat32_file_ops = {
    .open = fat32_file_open,
    .close = fat32_file_close,
    .read = fat32_file_read,
    .write = fat32_file_write,
    .readdir = NULL,
    .finddir = NULL,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .rename = NULL,
    .truncate = fat32_file_truncate,
    .stat = fat32_file_stat,
    .ioctl = NULL,
    .symlink = NULL,
    .readlink = NULL,
};

static node_ops_t fat32_dir_ops = {
    .open = fat32_dir_open,
    .close = fat32_dir_close,
    .read = NULL,
    .write = NULL,
    .readdir = fat32_dir_readdir,
    .finddir = fat32_dir_finddir,
    .create = fat32_dir_create,
    .unlink = fat32_dir_unlink,
    .mkdir = fat32_dir_mkdir,
    .rmdir = fat32_dir_rmdir,
    .rename = NULL,
    .truncate = NULL,
    .stat = fat32_dir_stat,
    .ioctl = NULL,
    .symlink = NULL,
    .readlink = NULL,
};

/* ============================================================================
 * Mount/Unmount
 * ========================================================================== */

static vfs_node_t *fat32_mount(const char *source, uint32_t flags) {
    if (!source) {
        ERROR("fat32: no source device specified");
        return NULL;
    }

    /* Find block device */
    block_device_t *dev = block_find(source);
    if (!dev) {
        ERROR("fat32: device not found: %s", source);
        return NULL;
    }

    /* Allocate filesystem structure */
    fat32_fs_t *fs = kmem_cache_alloc(fat32_fs_cache);
    if (!fs) {
        ERROR("fat32: failed to allocate filesystem structure");
        return NULL;
    }

    memset(fs, 0, sizeof(fat32_fs_t));
    fs->dev = dev;
    fs->read_only = (flags & 1) || dev->read_only;

    /* Read boot sector */
    uint8_t *boot_sector = kmalloc(dev->sector_size);
    if (!boot_sector) {
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    if (block_read(dev, 0, 1, boot_sector) != 0) {
        ERROR("fat32: failed to read boot sector");
        kfree(boot_sector);
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    memcpy(&fs->bpb, boot_sector, sizeof(fat32_bpb_t));
    kfree(boot_sector);

    /* Validate boot sector */
    if (fs->bpb.bytes_per_sector != 512 &&
        fs->bpb.bytes_per_sector != 1024 &&
        fs->bpb.bytes_per_sector != 2048 &&
        fs->bpb.bytes_per_sector != 4096) {
        ERROR("fat32: invalid bytes per sector: %u", fs->bpb.bytes_per_sector);
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    if (fs->bpb.num_fats == 0 || fs->bpb.num_fats > 2) {
        ERROR("fat32: invalid number of FATs: %u", fs->bpb.num_fats);
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    if (fs->bpb.fat_size_32 == 0) {
        ERROR("fat32: not a FAT32 filesystem (fat_size_32 = 0)");
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    /* Calculate derived values */
    fs->fat_start_sector = fs->bpb.reserved_sectors;
    fs->fat_sectors = fs->bpb.fat_size_32;
    fs->data_start_sector = fs->fat_start_sector +
                            (fs->bpb.num_fats * fs->fat_sectors);
    fs->root_cluster = fs->bpb.root_cluster;
    fs->sectors_per_cluster = fs->bpb.sectors_per_cluster;
    fs->cluster_size = fs->sectors_per_cluster * fs->bpb.bytes_per_sector;
    fs->entries_per_cluster = fs->cluster_size / sizeof(fat_dirent_t);

    uint32_t total_sectors = (fs->bpb.total_sectors_16 != 0) ?
                             fs->bpb.total_sectors_16 : fs->bpb.total_sectors_32;
    uint32_t data_sectors = total_sectors - fs->data_start_sector;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    /* Verify this is FAT32 */
    if (fs->total_clusters < FAT32_MIN_CLUSTERS) {
        ERROR("fat32: not FAT32 (only %u clusters)", fs->total_clusters);
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    /* Allocate FAT cache */
    fs->fat_cache = kmalloc(fs->bpb.bytes_per_sector);
    if (!fs->fat_cache) {
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    fs->fat_cache_sector = 0xFFFFFFFF;  /* Invalid, force read on first access */
    fs->fat_cache_dirty = false;

    /* Read FSInfo if available */
    fs->fsinfo_sector = fs->bpb.fsinfo_sector;
    fs->free_clusters = 0xFFFFFFFF;  /* Unknown */
    fs->next_free = 2;

    if (fs->fsinfo_sector != 0 && fs->fsinfo_sector != 0xFFFF) {
        uint8_t *fsinfo_buf = kmalloc(fs->bpb.bytes_per_sector);
        if (fsinfo_buf) {
            if (block_read(dev, fs->fsinfo_sector, 1, fsinfo_buf) == 0) {
                fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)fsinfo_buf;
                if (fsinfo->lead_signature == FAT32_FSINFO_LEAD_SIG &&
                    fsinfo->struct_signature == FAT32_FSINFO_STRUCT_SIG) {
                    fs->free_clusters = fsinfo->free_count;
                    fs->next_free = fsinfo->next_free;
                }
            }
            kfree(fsinfo_buf);
        }
    }

    INFO("fat32: mounted %s: %u clusters, %u bytes/cluster",
         source, fs->total_clusters, fs->cluster_size);

    /* Create root directory node */
    fat_dirent_t root_de;
    memset(&root_de, 0, sizeof(root_de));
    memset(root_de.name, ' ', 8);
    memset(root_de.ext, ' ', 3);
    root_de.attr = FAT_ATTR_DIRECTORY;
    FAT_SET_CLUSTER(&root_de, fs->root_cluster);

    vfs_node_t *root = fat32_create_node(fs, "", &root_de, 0, 0);
    if (!root) {
        kfree(fs->fat_cache);
        kmem_cache_free(fat32_fs_cache, fs);
        return NULL;
    }

    return root;
}

static int fat32_unmount(vfs_mount_t *mount) {
    if (!mount || !mount->root) {
        return VFS_EINVAL;
    }

    fat32_inode_info_t *info = (fat32_inode_info_t *)mount->root->private;
    if (!info) {
        return VFS_EINVAL;
    }

    fat32_fs_t *fs = info->fs;

    /* Flush FAT cache */
    fat32_flush_fat(fs);

    /* Update FSInfo */
    if (!fs->read_only && fs->fsinfo_sector != 0 && fs->fsinfo_sector != 0xFFFF) {
        uint8_t *fsinfo_buf = kmalloc(fs->bpb.bytes_per_sector);
        if (fsinfo_buf) {
            if (block_read(fs->dev, fs->fsinfo_sector, 1, fsinfo_buf) == 0) {
                fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)fsinfo_buf;
                if (fsinfo->lead_signature == FAT32_FSINFO_LEAD_SIG &&
                    fsinfo->struct_signature == FAT32_FSINFO_STRUCT_SIG) {
                    fsinfo->free_count = fs->free_clusters;
                    fsinfo->next_free = fs->next_free;
                    block_write(fs->dev, fs->fsinfo_sector, 1, fsinfo_buf);
                }
            }
            kfree(fsinfo_buf);
        }
    }

    /* Flush block device */
    block_flush(fs->dev);

    /* Free resources */
    kfree(fs->fat_cache);
    kmem_cache_free(fat32_inode_info_cache, info);
    kmem_cache_free(fat32_fs_cache, fs);

    return VFS_OK;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

void fat32_init(void) {
    /* Create slab caches */
    fat32_fs_cache = kmem_cache_create("fat32_fs",
                                       sizeof(fat32_fs_t),
                                       8, 0);
    if (!fat32_fs_cache) {
        ERROR("fat32: failed to create fs cache");
        return;
    }

    fat32_inode_info_cache = kmem_cache_create("fat32_inode",
                                               sizeof(fat32_inode_info_t),
                                               8, 0);
    if (!fat32_inode_info_cache) {
        ERROR("fat32: failed to create inode cache");
        return;
    }

    /* Register filesystem */
    if (vfs_register_fs(&fat32_fs_ops) != VFS_OK) {
        ERROR("fat32: failed to register filesystem");
        return;
    }

    INFO("fat32: filesystem driver initialized");
}
