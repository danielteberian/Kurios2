/* initrd.c - Initial ramdisk (CPIO newc format) parser */

#include "initrd.h"
#include "debug/debug.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "fs/vfs.h"

/* Initrd state */
static struct {
    bool valid;
    uint64_t phys_base;     /* Physical address */
    uint64_t virt_base;     /* Virtual address (mapped) */
    uint64_t size;          /* Total size */
    uint32_t entry_count;   /* Number of entries */
} initrd_info;

/* Parse a hexadecimal ASCII field from CPIO header */
static uint32_t parse_hex(const char *str, int len)
{
    uint32_t value = 0;
    for (int i = 0; i < len; i++) {
        char c = str[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value |= c - 'A' + 10;
        }
    }
    return value;
}

/* Align pointer to 4-byte boundary */
static inline const uint8_t *align4(const uint8_t *ptr, const uint8_t *base)
{
    uintptr_t offset = (uintptr_t)(ptr - base);
    offset = (offset + 3) & ~3;
    return base + offset;
}

int initrd_init(const BootInfo *boot_info)
{
    initrd_info.valid = false;
    initrd_info.entry_count = 0;

    /* Check if initrd was loaded */
    if (!(boot_info->flags & BOOT_FLAG_INITRD)) {
        DEBUG("initrd: Not present (no BOOT_FLAG_INITRD)");
        return -1;
    }

    if (boot_info->initrd_start == 0 || boot_info->initrd_size == 0) {
        DEBUG("initrd: Not present (start=0x%llx, size=%llu)",
              boot_info->initrd_start, boot_info->initrd_size);
        return -1;
    }

    initrd_info.phys_base = boot_info->initrd_start;
    initrd_info.size = boot_info->initrd_size;

    /* Map initrd into virtual memory using the heap
     * Physical memory is NOT identity mapped in this higher-half kernel.
     * We allocate from the kernel heap and map the physical pages there. */
    uint64_t num_pages = (initrd_info.size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Use vmm_map_pages to a region after the kernel heap starts
     * The slab allocator uses 0xFFFFFFFF88000000+, we'll use 0xFFFFFFFF90200000+ */
    uint64_t initrd_virt = 0xFFFFFFFF90200000UL;

    if (vmm_map_pages(initrd_virt, initrd_info.phys_base, num_pages,
                      PTE_PRESENT | PTE_WRITABLE) != 0) {
        ERROR("initrd: Failed to map %llu pages", num_pages);
        return -1;
    }
    initrd_info.virt_base = initrd_virt;

    /* Validate CPIO magic */
    const cpio_header_t *hdr = (const cpio_header_t *)initrd_info.virt_base;
    if (memcmp(hdr->magic, CPIO_MAGIC, 6) != 0 &&
        memcmp(hdr->magic, CPIO_MAGIC_CRC, 6) != 0) {
        ERROR("initrd: Invalid CPIO magic: %c%c%c%c%c%c",
              hdr->magic[0], hdr->magic[1], hdr->magic[2],
              hdr->magic[3], hdr->magic[4], hdr->magic[5]);
        return -1;
    }

    /* Count entries */
    const uint8_t *ptr = (const uint8_t *)initrd_info.virt_base;
    const uint8_t *end = ptr + initrd_info.size;

    while (ptr + sizeof(cpio_header_t) <= end) {
        hdr = (const cpio_header_t *)ptr;

        if (memcmp(hdr->magic, CPIO_MAGIC, 6) != 0 &&
            memcmp(hdr->magic, CPIO_MAGIC_CRC, 6) != 0) {
            break;
        }

        uint32_t namesize = parse_hex(hdr->namesize, 8);
        uint32_t filesize = parse_hex(hdr->filesize, 8);

        const char *name = (const char *)(ptr + sizeof(cpio_header_t));

        /* Check for trailer */
        if (namesize == 11 && strcmp(name, CPIO_TRAILER) == 0) {
            break;
        }

        initrd_info.entry_count++;

        /* Move to next entry */
        /* Header + name aligned to 4 bytes, then data aligned to 4 bytes */
        ptr += sizeof(cpio_header_t) + namesize;
        ptr = align4(ptr, (const uint8_t *)initrd_info.virt_base);
        ptr += filesize;
        ptr = align4(ptr, (const uint8_t *)initrd_info.virt_base);
    }

    initrd_info.valid = true;

    INFO("initrd: Loaded at 0x%llx, %llu bytes, %u entries",
         initrd_info.phys_base, initrd_info.size, initrd_info.entry_count);

    return 0;
}

bool initrd_available(void)
{
    return initrd_info.valid;
}

uint64_t initrd_get_base(void)
{
    return initrd_info.phys_base;
}

uint64_t initrd_get_size(void)
{
    return initrd_info.size;
}

int initrd_iterate(int (*callback)(const initrd_entry_t *entry, void *ctx), void *ctx)
{
    if (!initrd_info.valid) {
        return -1;
    }

    const uint8_t *base = (const uint8_t *)initrd_info.virt_base;
    const uint8_t *ptr = base;
    const uint8_t *end = ptr + initrd_info.size;

    while (ptr + sizeof(cpio_header_t) <= end) {
        const cpio_header_t *hdr = (const cpio_header_t *)ptr;

        if (memcmp(hdr->magic, CPIO_MAGIC, 6) != 0 &&
            memcmp(hdr->magic, CPIO_MAGIC_CRC, 6) != 0) {
            ERROR("initrd: Bad magic during iteration");
            return -1;
        }

        uint32_t namesize = parse_hex(hdr->namesize, 8);
        uint32_t filesize = parse_hex(hdr->filesize, 8);
        uint32_t mode = parse_hex(hdr->mode, 8);

        const char *name = (const char *)(ptr + sizeof(cpio_header_t));

        /* Check for trailer */
        if (namesize == 11 && strcmp(name, CPIO_TRAILER) == 0) {
            break;
        }

        /* Skip "." entry */
        if (namesize == 2 && name[0] == '.' && name[1] == '\0') {
            ptr += sizeof(cpio_header_t) + namesize;
            ptr = align4(ptr, base);
            ptr += filesize;
            ptr = align4(ptr, base);
            continue;
        }

        /* Get data pointer */
        const uint8_t *header_end = ptr + sizeof(cpio_header_t) + namesize;
        const uint8_t *data = align4(header_end, base);

        initrd_entry_t entry = {
            .name = name,
            .data = data,
            .size = filesize,
            .mode = mode,
            .is_dir = (mode & CPIO_TYPE_MASK) == CPIO_TYPE_DIR
        };

        int ret = callback(&entry, ctx);
        if (ret != 0) {
            return ret;
        }

        /* Move to next entry */
        ptr = data + filesize;
        ptr = align4(ptr, base);
    }

    return 0;
}

const uint8_t *initrd_find(const char *path, uint32_t *size)
{
    if (!initrd_info.valid || path == NULL) {
        return NULL;
    }

    /* Skip leading slash */
    if (path[0] == '/') {
        path++;
    }

    const uint8_t *base = (const uint8_t *)initrd_info.virt_base;
    const uint8_t *ptr = base;
    const uint8_t *end = ptr + initrd_info.size;

    while (ptr + sizeof(cpio_header_t) <= end) {
        const cpio_header_t *hdr = (const cpio_header_t *)ptr;

        if (memcmp(hdr->magic, CPIO_MAGIC, 6) != 0 &&
            memcmp(hdr->magic, CPIO_MAGIC_CRC, 6) != 0) {
            break;
        }

        uint32_t namesize = parse_hex(hdr->namesize, 8);
        uint32_t filesize = parse_hex(hdr->filesize, 8);

        const char *name = (const char *)(ptr + sizeof(cpio_header_t));

        /* Check for trailer */
        if (namesize == 11 && strcmp(name, CPIO_TRAILER) == 0) {
            break;
        }

        /* Check if this is the file we're looking for */
        if (strcmp(name, path) == 0) {
            const uint8_t *header_end = ptr + sizeof(cpio_header_t) + namesize;
            const uint8_t *data = align4(header_end, base);

            if (size != NULL) {
                *size = filesize;
            }
            return data;
        }

        /* Move to next entry */
        ptr += sizeof(cpio_header_t) + namesize;
        ptr = align4(ptr, base);
        ptr += filesize;
        ptr = align4(ptr, base);
    }

    return NULL;
}

/* Helper for mount callback */
static int mount_entry(const initrd_entry_t *entry, void *ctx)
{
    (void)ctx;

    /* Skip entries without leading path component */
    const char *name = entry->name;

    /* Build full path with leading slash */
    char path[256];
    path[0] = '/';
    strncpy(path + 1, name, sizeof(path) - 2);
    path[sizeof(path) - 1] = '\0';

    if (entry->is_dir) {
        /* Create directory */
        int ret = vfs_mkdir(path);
        if (ret < 0 && ret != VFS_EEXIST) {
            WARN("initrd: Failed to create dir %s: %d", path, ret);
        }
    } else {
        /* Create and write file */
        int fd = vfs_open(path, O_CREAT | O_WRONLY);
        if (fd < 0) {
            WARN("initrd: Failed to create file %s: %d", path, fd);
            return 0;  /* Continue with other files */
        }

        if (entry->size > 0) {
            ssize_t written = vfs_write(fd, entry->data, entry->size);
            if (written != (ssize_t)entry->size) {
                WARN("initrd: Partial write to %s: %lld/%u",
                     path, (long long)written, entry->size);
            }
        }

        vfs_close(fd);
        DEBUG("initrd: Created %s (%u bytes)", path, entry->size);
    }

    return 0;
}

int initrd_mount(void)
{
    if (!initrd_info.valid) {
        DEBUG("initrd: Cannot mount - not available");
        return -1;
    }

    INFO("initrd: Mounting %u entries to ramfs...", initrd_info.entry_count);

    int ret = initrd_iterate(mount_entry, NULL);
    if (ret < 0) {
        ERROR("initrd: Mount failed");
        return -1;
    }

    INFO("initrd: Mount complete");
    return 0;
}

#ifdef DEBUG_TESTS
static int print_entry(const initrd_entry_t *entry, void *ctx)
{
    uint32_t *count = (uint32_t *)ctx;
    (*count)++;

    kprintf("    %c %s (%u bytes)\n",
            entry->is_dir ? 'D' : 'F',
            entry->name,
            entry->size);

    return 0;
}

void initrd_run_tests(void)
{
    kprintf("\n=== Initrd Tests ===\n");

    if (!initrd_info.valid) {
        kprintf("  No initrd loaded - skipping tests\n");
        return;
    }

    kprintf("  Test 1 - Initrd available: %s\n",
            initrd_available() ? "OK" : "FAIL");

    kprintf("  Test 2 - Base address: 0x%llx\n", initrd_get_base());
    kprintf("  Test 3 - Size: %llu bytes\n", initrd_get_size());

    kprintf("  Test 4 - List entries:\n");
    uint32_t count = 0;
    int ret = initrd_iterate(print_entry, &count);
    kprintf("    Listed %u entries (ret=%d)\n", count, ret);

    kprintf("\n  Initrd tests complete.\n");
}
#endif /* DEBUG_TESTS */
