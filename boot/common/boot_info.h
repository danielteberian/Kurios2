/* boot_info.h - Boot information structure (C header) */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

/* Magic number and version */
#define KURIOS_BOOT_MAGIC     0x4B55524953ULL  /* "KURIS" */
#define BOOT_PROTOCOL_VERSION 0x0001

/* Boot flags */
#define BOOT_FLAG_BIOS        (1 << 0)
#define BOOT_FLAG_UEFI        (1 << 1)
#define BOOT_FLAG_FRAMEBUFFER (1 << 2)
#define BOOT_FLAG_ACPI        (1 << 3)
#define BOOT_FLAG_INITRD      (1 << 4)

/* Memory map types (E820 compatible) */
#define MMAP_TYPE_USABLE       1
#define MMAP_TYPE_RESERVED     2
#define MMAP_TYPE_ACPI_RECLAIM 3
#define MMAP_TYPE_ACPI_NVS     4
#define MMAP_TYPE_BAD          5

/* Memory map entry */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attrs;
} __attribute__((packed)) MemoryMapEntry;

/* Framebuffer information */
typedef struct {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
} __attribute__((packed)) FramebufferInfo;

/* Boot information structure */
typedef struct {
    uint64_t magic;          /* Must be KURIOS_BOOT_MAGIC */
    uint64_t version;        /* Protocol version */
    uint64_t flags;          /* Boot flags */
    uint64_t memory_map;     /* Pointer to memory map entries */
    uint64_t memory_count;   /* Number of memory map entries */
    uint64_t framebuffer;    /* Pointer to framebuffer info (or 0) */
    uint64_t acpi_rsdp;      /* ACPI RSDP address (or 0) */
    uint64_t kernel_phys;    /* Kernel physical load address */
    uint64_t kernel_size;    /* Kernel size in bytes */
    uint64_t cmdline;        /* Command line string (or 0) */
    uint64_t boot_drive;     /* Boot drive number */
    uint64_t initrd_start;   /* Initial ramdisk physical address (or 0) */
    uint64_t initrd_size;    /* Initial ramdisk size in bytes (or 0) */
} __attribute__((packed)) BootInfo;

#endif /* BOOT_INFO_H */
