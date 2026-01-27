/* gdt.c - Global Descriptor Table and Task State Segment */

#include "gdt.h"
#include "../../debug/debug.h"
#include "../../include/types.h"

/* GDT entries - we use a union to handle both regular and TSS descriptors */
static struct {
    gdt_entry_t entries[5];         /* Null, KCode, KData, UData, UCode */
    tss_descriptor_t tss_desc;      /* TSS descriptor (16 bytes) */
} __attribute__((packed, aligned(16))) gdt;

/* Task State Segment */
static tss_t tss __attribute__((aligned(16)));

/* GDT pointer for LGDT */
static gdt_pointer_t gdt_ptr;

/*
 * Set a GDT entry
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    gdt_entry_t *entry = &gdt.entries[index];

    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_mid = (base >> 16) & 0xFF;
    entry->access = access;
    entry->granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    entry->base_high = (base >> 24) & 0xFF;
}

/*
 * Set the TSS descriptor in the GDT
 */
static void gdt_set_tss(uint64_t base, uint32_t limit) {
    tss_descriptor_t *desc = &gdt.tss_desc;

    desc->limit_low = limit & 0xFFFF;
    desc->base_low = base & 0xFFFF;
    desc->base_mid = (base >> 16) & 0xFF;
    desc->access = GDT_ACCESS_TSS;
    desc->granularity = (limit >> 16) & 0x0F;  /* No flags for TSS */
    desc->base_high = (base >> 24) & 0xFF;
    desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    desc->reserved = 0;
}

/*
 * Initialize the TSS
 */
static void tss_init(void) {
    /* Zero the entire TSS */
    uint8_t *p = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(tss_t); i++) {
        p[i] = 0;
    }

    /* Set I/O permission bitmap offset to point past the TSS
     * (no I/O bitmap, all ports require ring 0) */
    tss.iopb_offset = sizeof(tss_t);

    /* RSP0 will be set later when we have a proper kernel stack
     * For now, it stays at 0 (interrupts would crash if we tried
     * to switch from ring 3 without setting this) */
}

/*
 * Initialize the GDT and TSS
 */
void gdt_init(void) {
    INFO("Initializing GDT and TSS...");

    /* Initialize TSS first */
    tss_init();

    /*
     * Set up GDT entries
     *
     * In 64-bit long mode:
     * - Base and limit are ignored for code/data segments
     * - The Long flag (L) must be set for 64-bit code segments
     * - The Size flag (D/B) must be clear for 64-bit code segments
     *
     * Note: User data comes before user code for SYSCALL/SYSRET
     * (STAR MSR expects this order)
     */

    /* Entry 0: Null descriptor (required) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: Kernel code segment (selector 0x08) */
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_KERNEL_CODE_ACCESS,
                  GDT_FLAG_GRANULARITY | GDT_FLAG_LONG);

    /* Entry 2: Kernel data segment (selector 0x10) */
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_KERNEL_DATA_ACCESS,
                  GDT_FLAG_GRANULARITY | GDT_FLAG_SIZE);

    /* Entry 3: User data segment (selector 0x18) */
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_USER_DATA_ACCESS,
                  GDT_FLAG_GRANULARITY | GDT_FLAG_SIZE);

    /* Entry 4: User code segment (selector 0x20) */
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_USER_CODE_ACCESS,
                  GDT_FLAG_GRANULARITY | GDT_FLAG_LONG);

    /* TSS descriptor at offset 0x28 (takes 2 GDT slots) */
    gdt_set_tss((uint64_t)&tss, sizeof(tss_t) - 1);

    /* Set up GDT pointer */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    /* Load the GDT */
    gdt_flush(&gdt_ptr);

    /* Load the TSS */
    tss_flush(GDT_TSS);

    INFO("GDT loaded: %d entries at 0x%llx", GDT_ENTRIES, (uint64_t)&gdt);
    DEBUG("  Kernel CS=0x%02x, DS=0x%02x", GDT_KERNEL_CODE, GDT_KERNEL_DATA);
    DEBUG("  User   CS=0x%02x, DS=0x%02x", GDT_USER_CODE, GDT_USER_DATA);
    DEBUG("  TSS at 0x%llx, selector=0x%02x", (uint64_t)&tss, GDT_TSS);
}

/*
 * Set the kernel stack pointer in the TSS
 */
void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
    TRACE("TSS RSP0 set to 0x%llx", rsp0);
}

/*
 * Set an IST entry
 */
void tss_set_ist(int ist, uint64_t stack) {
    if (ist < 1 || ist > 7) {
        ERROR("Invalid IST number: %d (must be 1-7)", ist);
        return;
    }

    /* Set IST entry directly to avoid packed struct pointer issues */
    switch (ist) {
        case 1: tss.ist1 = stack; break;
        case 2: tss.ist2 = stack; break;
        case 3: tss.ist3 = stack; break;
        case 4: tss.ist4 = stack; break;
        case 5: tss.ist5 = stack; break;
        case 6: tss.ist6 = stack; break;
        case 7: tss.ist7 = stack; break;
    }
    TRACE("TSS IST%d set to 0x%llx", ist, stack);
}

/*
 * Get the current TSS
 */
tss_t* tss_get(void) {
    return &tss;
}
