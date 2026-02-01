/* initrd.h - Initial ramdisk (CPIO newc format) support */
#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_info.h"

/* CPIO "newc" format header (ASCII, no alignment padding) */
typedef struct {
    char magic[6];      /* "070701" for newc, "070702" for newc+CRC */
    char ino[8];        /* Inode number */
    char mode[8];       /* File mode and permissions */
    char uid[8];        /* User ID */
    char gid[8];        /* Group ID */
    char nlink[8];      /* Number of hard links */
    char mtime[8];      /* Modification time */
    char filesize[8];   /* File size */
    char devmajor[8];   /* Device major number */
    char devminor[8];   /* Device minor number */
    char rdevmajor[8];  /* Rdev major number */
    char rdevminor[8];  /* Rdev minor number */
    char namesize[8];   /* Length of filename (including NUL) */
    char check[8];      /* Checksum (0 for newc) */
} __attribute__((packed)) cpio_header_t;

#define CPIO_MAGIC      "070701"
#define CPIO_MAGIC_CRC  "070702"
#define CPIO_TRAILER    "TRAILER!!!"

/* File type bits (from mode) */
#define CPIO_TYPE_MASK  0170000
#define CPIO_TYPE_DIR   0040000
#define CPIO_TYPE_FILE  0100000
#define CPIO_TYPE_LINK  0120000

/* Initrd entry (for iteration) */
typedef struct {
    const char *name;       /* Filename (within cpio archive) */
    const uint8_t *data;    /* Pointer to file data */
    uint32_t size;          /* File size */
    uint32_t mode;          /* File mode */
    bool is_dir;            /* Is this a directory? */
} initrd_entry_t;

/* Initialize initrd from boot info
 * Returns 0 on success, -1 if no initrd or parse error */
int initrd_init(const BootInfo *boot_info);

/* Check if initrd is available */
bool initrd_available(void);

/* Get initrd base address and size */
uint64_t initrd_get_base(void);
uint64_t initrd_get_size(void);

/* Iterate over all entries in the initrd
 * callback returns 0 to continue, non-zero to stop
 * Returns 0 on success, -1 on parse error */
int initrd_iterate(int (*callback)(const initrd_entry_t *entry, void *ctx), void *ctx);

/* Find a specific file in the initrd
 * Returns pointer to entry data, or NULL if not found
 * If size is non-NULL, stores the file size */
const uint8_t *initrd_find(const char *path, uint32_t *size);

/* Mount initrd contents to ramfs at root
 * Creates directories and files as needed
 * Returns 0 on success, -1 on error */
int initrd_mount(void);

#ifdef DEBUG_TESTS
void initrd_run_tests(void);
#endif

#endif /* INITRD_H */
