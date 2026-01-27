/* gdt.h - Global Descriptor Table and Task State Segment */
#ifndef _ARCH_GDT_H
#define _ARCH_GDT_H

#include <stdint.h>

/*
 * GDT Segment Selectors
 *
 * In 64-bit mode, most segmentation is disabled, but we still need:
 * - Valid code/data segments for kernel and user mode
 * - TSS for interrupt stack switching
 *
 * Selector format: Index (13 bits) | TI (1 bit) | RPL (2 bits)
 * TI=0 means GDT, RPL is requested privilege level
 */
#define GDT_NULL            0x00    /* Null descriptor (required) */
#define GDT_KERNEL_CODE     0x08    /* Kernel code segment (ring 0) */
#define GDT_KERNEL_DATA     0x10    /* Kernel data segment (ring 0) */
#define GDT_USER_DATA       0x18    /* User data segment (ring 3) */
#define GDT_USER_CODE       0x20    /* User code segment (ring 3) */
#define GDT_TSS             0x28    /* Task State Segment */

/* Selector with RPL for user mode */
#define GDT_USER_DATA_RPL3  (GDT_USER_DATA | 3)
#define GDT_USER_CODE_RPL3  (GDT_USER_CODE | 3)

/* Number of GDT entries (TSS takes 2 slots in 64-bit mode) */
#define GDT_ENTRIES         7

/*
 * GDT Entry (8 bytes for regular segments)
 *
 * In 64-bit long mode, base and limit are mostly ignored for
 * code/data segments (flat memory model), but flags still matter.
 */
typedef struct {
    uint16_t limit_low;         /* Segment limit bits 0-15 */
    uint16_t base_low;          /* Base address bits 0-15 */
    uint8_t  base_mid;          /* Base address bits 16-23 */
    uint8_t  access;            /* Access byte */
    uint8_t  granularity;       /* Limit bits 16-19 + flags */
    uint8_t  base_high;         /* Base address bits 24-31 */
} __attribute__((packed)) gdt_entry_t;

/*
 * TSS Descriptor (16 bytes in 64-bit mode)
 *
 * The TSS descriptor is twice as large in 64-bit mode because
 * base addresses are 64 bits.
 */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;        /* Upper 32 bits of base address */
    uint32_t reserved;
} __attribute__((packed)) tss_descriptor_t;

/*
 * GDT Pointer (for LGDT instruction)
 */
typedef struct {
    uint16_t limit;             /* Size of GDT - 1 */
    uint64_t base;              /* Linear address of GDT */
} __attribute__((packed)) gdt_pointer_t;

/*
 * Task State Segment (64-bit)
 *
 * In 64-bit mode, the TSS is primarily used for:
 * - RSP0-2: Stack pointers for ring transitions
 * - IST1-7: Interrupt Stack Table entries
 * - I/O permission bitmap
 */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;              /* Stack pointer for ring 0 */
    uint64_t rsp1;              /* Stack pointer for ring 1 (unused) */
    uint64_t rsp2;              /* Stack pointer for ring 2 (unused) */
    uint64_t reserved1;
    uint64_t ist1;              /* Interrupt Stack Table entry 1 */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;       /* I/O permission bitmap offset */
} __attribute__((packed)) tss_t;

/*
 * Access byte flags
 */
#define GDT_ACCESS_PRESENT      (1 << 7)    /* Segment present */
#define GDT_ACCESS_DPL0         (0 << 5)    /* Ring 0 */
#define GDT_ACCESS_DPL3         (3 << 5)    /* Ring 3 */
#define GDT_ACCESS_SYSTEM       (0 << 4)    /* System segment (TSS, etc.) */
#define GDT_ACCESS_CODE_DATA    (1 << 4)    /* Code or data segment */
#define GDT_ACCESS_EXEC         (1 << 3)    /* Executable (code segment) */
#define GDT_ACCESS_DC           (1 << 2)    /* Direction/Conforming */
#define GDT_ACCESS_RW           (1 << 1)    /* Readable (code) / Writable (data) */
#define GDT_ACCESS_ACCESSED     (1 << 0)    /* Accessed */

/* TSS access byte (type = 0x9 for available 64-bit TSS) */
#define GDT_ACCESS_TSS          (GDT_ACCESS_PRESENT | 0x09)

/*
 * Granularity byte flags
 */
#define GDT_FLAG_GRANULARITY    (1 << 7)    /* Limit in 4KB blocks */
#define GDT_FLAG_SIZE           (1 << 6)    /* 32-bit protected mode */
#define GDT_FLAG_LONG           (1 << 5)    /* 64-bit long mode */

/*
 * Common segment access bytes
 */
#define GDT_KERNEL_CODE_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | \
                                 GDT_ACCESS_RW)

#define GDT_KERNEL_DATA_ACCESS  (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | \
                                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)

#define GDT_USER_CODE_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_EXEC | \
                                 GDT_ACCESS_RW)

#define GDT_USER_DATA_ACCESS    (GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | \
                                 GDT_ACCESS_CODE_DATA | GDT_ACCESS_RW)

/*
 * Initialize GDT and TSS
 * Must be called early in kernel initialization
 */
void gdt_init(void);

/*
 * Set the kernel stack pointer in the TSS
 * Called when switching to a different kernel stack (e.g., per-CPU)
 */
void tss_set_rsp0(uint64_t rsp0);

/*
 * Set an IST (Interrupt Stack Table) entry
 * ist: IST number (1-7)
 * stack: Stack pointer for this IST
 */
void tss_set_ist(int ist, uint64_t stack);

/*
 * Get the current TSS
 */
tss_t* tss_get(void);

/*
 * Assembly functions (in gdt_flush.asm)
 */
extern void gdt_flush(gdt_pointer_t *gdt_ptr);
extern void tss_flush(uint16_t selector);

#endif /* _ARCH_GDT_H */
