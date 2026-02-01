/* acpi.c - ACPI Table Parsing Implementation */

#include "acpi.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "boot_info.h"

/* Static parsed ACPI information */
static acpi_info_t acpi_info;

/*
 * Read a 16-bit value from a physical address
 * Uses inline asm to avoid compiler array bounds warnings
 */
static inline uint16_t read_phys16(uint64_t addr)
{
    uint16_t value;
    __asm__ volatile("movw (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return value;
}

/*
 * Validate checksum of an ACPI structure
 * All bytes must sum to 0
 */
static bool validate_checksum(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += bytes[i];
    }

    return sum == 0;
}

/*
 * Find RSDP in a memory range
 * RSDP must be 16-byte aligned
 */
static acpi_rsdp_t *find_rsdp_in_range(uint64_t start, uint64_t end)
{
    /* RSDP must be 16-byte aligned */
    start = ALIGN_UP(start, 16);

    for (uint64_t addr = start; addr < end; addr += 16) {
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)addr;

        /* Check signature */
        if (memcmp(rsdp->signature, ACPI_SIG_RSDP, 8) != 0) {
            continue;
        }

        /* Validate ACPI 1.0 checksum (first 20 bytes) */
        if (!validate_checksum(rsdp, sizeof(acpi_rsdp_t))) {
            WARN("ACPI: RSDP at 0x%llx has invalid checksum", addr);
            continue;
        }

        /* If ACPI 2.0+, also validate extended checksum */
        if (rsdp->revision >= 2) {
            acpi_rsdp_ext_t *rsdp_ext = (acpi_rsdp_ext_t *)rsdp;
            if (!validate_checksum(rsdp_ext, rsdp_ext->length)) {
                WARN("ACPI: RSDP 2.0 at 0x%llx has invalid extended checksum", addr);
                continue;
            }
        }

        return rsdp;
    }

    return NULL;
}

/*
 * Find RSDP by searching standard locations
 */
static acpi_rsdp_t *find_rsdp(uint64_t hint)
{
    acpi_rsdp_t *rsdp = NULL;

    /* Try the boot-provided hint first */
    if (hint != 0) {
        rsdp = (acpi_rsdp_t *)hint;
        if (memcmp(rsdp->signature, ACPI_SIG_RSDP, 8) == 0 &&
            validate_checksum(rsdp, sizeof(acpi_rsdp_t))) {
            DEBUG("ACPI: Found RSDP at boot-provided address 0x%llx", hint);
            return rsdp;
        }
    }

    /* Search Extended BIOS Data Area (EBDA) */
    /* EBDA pointer is at 0x40E (segment) */
    uint64_t ebda_addr = (uint64_t)read_phys16(0x40E) << 4;
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        rsdp = find_rsdp_in_range(ebda_addr, ebda_addr + 1024);
        if (rsdp) {
            DEBUG("ACPI: Found RSDP in EBDA at 0x%llx", (uint64_t)rsdp);
            return rsdp;
        }
    }

    /* Search BIOS ROM area (0xE0000 - 0xFFFFF) */
    rsdp = find_rsdp_in_range(0xE0000, 0x100000);
    if (rsdp) {
        DEBUG("ACPI: Found RSDP in BIOS ROM at 0x%llx", (uint64_t)rsdp);
        return rsdp;
    }

    return NULL;
}

/*
 * Validate an SDT table header
 */
static bool validate_sdt(const acpi_sdt_header_t *header)
{
    if (!header || header->length < sizeof(acpi_sdt_header_t)) {
        return false;
    }

    return validate_checksum(header, header->length);
}

/*
 * Find a table with given signature in RSDT/XSDT
 */
static acpi_sdt_header_t *find_table(acpi_rsdp_t *rsdp, const char *signature)
{
    uint32_t entry_count;
    bool use_xsdt = false;

    /* Determine whether to use RSDT or XSDT */
    if (rsdp->revision >= 2) {
        acpi_rsdp_ext_t *rsdp_ext = (acpi_rsdp_ext_t *)rsdp;
        if (rsdp_ext->xsdt_address != 0) {
            use_xsdt = true;
        }
    }

    if (use_xsdt) {
        acpi_rsdp_ext_t *rsdp_ext = (acpi_rsdp_ext_t *)rsdp;
        acpi_xsdt_t *xsdt = (acpi_xsdt_t *)rsdp_ext->xsdt_address;

        if (!validate_sdt(&xsdt->header)) {
            ERROR("ACPI: Invalid XSDT checksum");
            return NULL;
        }

        entry_count = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);

        for (uint32_t i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)xsdt->entries[i];
            if (memcmp(header->signature, signature, 4) == 0) {
                if (validate_sdt(header)) {
                    return header;
                } else {
                    WARN("ACPI: Table %.4s has invalid checksum", signature);
                }
            }
        }
    } else {
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(uintptr_t)rsdp->rsdt_address;

        if (!validate_sdt(&rsdt->header)) {
            ERROR("ACPI: Invalid RSDT checksum");
            return NULL;
        }

        entry_count = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);

        for (uint32_t i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)(uintptr_t)rsdt->entries[i];
            if (memcmp(header->signature, signature, 4) == 0) {
                if (validate_sdt(header)) {
                    return header;
                } else {
                    WARN("ACPI: Table %.4s has invalid checksum", signature);
                }
            }
        }
    }

    return NULL;
}

/*
 * Parse MADT (Multiple APIC Description Table)
 */
static void parse_madt(acpi_madt_t *madt)
{
    acpi_info.local_apic_addr = madt->local_apic_address;
    acpi_info.has_legacy_pic = (madt->flags & MADT_FLAG_PCAT_COMPAT) != 0;

    /* Calculate end of table */
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    uint8_t *ptr = (uint8_t *)(madt + 1);  /* After fixed header */

    while (ptr < end) {
        madt_entry_header_t *entry = (madt_entry_header_t *)ptr;

        if (entry->length == 0) {
            WARN("ACPI: MADT entry with zero length");
            break;
        }

        switch (entry->type) {
        case MADT_TYPE_LOCAL_APIC: {
            madt_local_apic_t *lapic = (madt_local_apic_t *)entry;

            if (acpi_info.cpu_count < ACPI_MAX_CPUS) {
                acpi_cpu_info_t *cpu = &acpi_info.cpus[acpi_info.cpu_count];
                cpu->acpi_id = lapic->acpi_processor_id;
                cpu->apic_id = lapic->apic_id;
                cpu->enabled = (lapic->flags & MADT_LAPIC_FLAG_ENABLED) != 0;
                cpu->online_capable = (lapic->flags & MADT_LAPIC_FLAG_ONLINE_CAP) != 0;
                acpi_info.cpu_count++;

                DEBUG("ACPI: CPU %u: APIC ID %u, %s",
                      cpu->acpi_id, cpu->apic_id,
                      cpu->enabled ? "enabled" : "disabled");
            }
            break;
        }

        case MADT_TYPE_IO_APIC: {
            madt_io_apic_t *ioapic = (madt_io_apic_t *)entry;

            if (acpi_info.ioapic_count < ACPI_MAX_IOAPICS) {
                acpi_ioapic_info_t *io = &acpi_info.ioapics[acpi_info.ioapic_count];
                io->id = ioapic->io_apic_id;
                io->address = ioapic->io_apic_address;
                io->gsi_base = ioapic->gsi_base;
                acpi_info.ioapic_count++;

                DEBUG("ACPI: I/O APIC %u at 0x%x, GSI base %u",
                      io->id, io->address, io->gsi_base);
            }
            break;
        }

        case MADT_TYPE_INT_OVERRIDE: {
            madt_int_override_t *override = (madt_int_override_t *)entry;

            if (acpi_info.override_count < ACPI_MAX_OVERRIDES) {
                acpi_int_override_t *ov = &acpi_info.overrides[acpi_info.override_count];
                ov->source = override->source;
                ov->gsi = override->gsi;
                ov->flags = override->flags;
                acpi_info.override_count++;

                DEBUG("ACPI: IRQ %u -> GSI %u (flags 0x%x)",
                      ov->source, ov->gsi, ov->flags);
            }
            break;
        }

        case MADT_TYPE_LOCAL_APIC_OVERRIDE: {
            /* Local APIC address override (64-bit) */
            uint64_t addr = *(uint64_t *)(ptr + 4);
            acpi_info.local_apic_addr = addr;
            DEBUG("ACPI: Local APIC address override: 0x%llx", addr);
            break;
        }

        default:
            DEBUG("ACPI: Unknown MADT entry type %u", entry->type);
            break;
        }

        ptr += entry->length;
    }
}

/*
 * Parse FADT (Fixed ACPI Description Table)
 */
static void parse_fadt(acpi_fadt_t *fadt)
{
    acpi_info.has_fadt = true;
    acpi_info.pm_timer_port = fadt->pm_tmr_blk;
    acpi_info.sci_interrupt = fadt->sci_int;

    DEBUG("ACPI: FADT: PM timer at 0x%x, SCI IRQ %u",
          acpi_info.pm_timer_port, acpi_info.sci_interrupt);
}

/*
 * Parse HPET (High Precision Event Timer Table)
 */
static void parse_hpet(acpi_hpet_t *hpet)
{
    acpi_info.has_hpet = true;
    acpi_info.hpet_address = hpet->address;

    DEBUG("ACPI: HPET at 0x%llx", acpi_info.hpet_address);
}

/*
 * Initialize ACPI subsystem
 */
int acpi_init(void *boot_info_ptr)
{
    BootInfo *boot_info = (BootInfo *)boot_info_ptr;

    /* Clear parsed info */
    memset(&acpi_info, 0, sizeof(acpi_info));

    /* Find RSDP */
    uint64_t rsdp_hint = 0;
    if (boot_info && (boot_info->flags & BOOT_FLAG_ACPI)) {
        rsdp_hint = boot_info->acpi_rsdp;
    }

    acpi_rsdp_t *rsdp = find_rsdp(rsdp_hint);
    if (!rsdp) {
        ERROR("ACPI: RSDP not found");
        return -1;
    }

    acpi_info.revision = rsdp->revision;

    INFO("ACPI: Found RSDP revision %u (%.6s)",
         rsdp->revision, rsdp->oem_id);

    /* Find and parse MADT */
    acpi_madt_t *madt = (acpi_madt_t *)find_table(rsdp, ACPI_SIG_MADT);
    if (madt) {
        parse_madt(madt);
    } else {
        WARN("ACPI: MADT not found - no APIC information available");
    }

    /* Find and parse FADT */
    acpi_fadt_t *fadt = (acpi_fadt_t *)find_table(rsdp, ACPI_SIG_FADT);
    if (fadt) {
        parse_fadt(fadt);
    }

    /* Find and parse HPET */
    acpi_hpet_t *hpet = (acpi_hpet_t *)find_table(rsdp, ACPI_SIG_HPET);
    if (hpet) {
        parse_hpet(hpet);
    }

    acpi_info.valid = true;

    INFO("ACPI: Local APIC at 0x%llx, %u CPUs, %u I/O APICs",
         acpi_info.local_apic_addr,
         acpi_info.cpu_count,
         acpi_info.ioapic_count);

    return 0;
}

/*
 * Get parsed ACPI information
 */
const acpi_info_t *acpi_get_info(void)
{
    return &acpi_info;
}

/*
 * Get Local APIC physical address
 */
uint64_t acpi_get_local_apic_addr(void)
{
    return acpi_info.local_apic_addr;
}

/*
 * Get CPU count
 */
uint32_t acpi_get_cpu_count(void)
{
    return acpi_info.cpu_count;
}

/*
 * Get I/O APIC count
 */
uint32_t acpi_get_ioapic_count(void)
{
    return acpi_info.ioapic_count;
}

/*
 * Check if legacy 8259 PICs are present
 */
bool acpi_has_legacy_pic(void)
{
    return acpi_info.has_legacy_pic;
}

/*
 * Check if HPET is available
 */
bool acpi_has_hpet(void)
{
    return acpi_info.has_hpet;
}

/*
 * Get HPET base address
 */
uint64_t acpi_get_hpet_addr(void)
{
    return acpi_info.hpet_address;
}

/*
 * Map ISA IRQ to Global System Interrupt
 * Returns the GSI, applying any interrupt overrides from MADT
 */
uint32_t acpi_isa_irq_to_gsi(uint8_t irq)
{
    /* Check for override */
    for (uint32_t i = 0; i < acpi_info.override_count; i++) {
        if (acpi_info.overrides[i].source == irq) {
            return acpi_info.overrides[i].gsi;
        }
    }

    /* No override - ISA IRQs 0-15 map directly to GSI 0-15 */
    return irq;
}

/*
 * Get interrupt flags for an ISA IRQ
 * Returns 0 if no override exists
 */
uint16_t acpi_get_irq_flags(uint8_t irq)
{
    for (uint32_t i = 0; i < acpi_info.override_count; i++) {
        if (acpi_info.overrides[i].source == irq) {
            return acpi_info.overrides[i].flags;
        }
    }

    return 0;
}

#ifdef DEBUG_TESTS
/*
 * Run ACPI tests
 */
void acpi_run_tests(void)
{
    kprintf("\n=== ACPI Tests ===\n");

    /* Test 1: ACPI valid */
    kprintf("  Test 1 - ACPI valid: %s\n",
            acpi_info.valid ? "OK" : "FAIL");

    /* Test 2: Local APIC address found */
    kprintf("  Test 2 - Local APIC addr: 0x%llx %s\n",
            acpi_info.local_apic_addr,
            acpi_info.local_apic_addr != 0 ? "OK" : "FAIL");

    /* Test 3: At least one CPU found */
    kprintf("  Test 3 - CPU count: %u %s\n",
            acpi_info.cpu_count,
            acpi_info.cpu_count > 0 ? "OK" : "FAIL");

    /* Test 4: At least one I/O APIC found */
    kprintf("  Test 4 - I/O APIC count: %u %s\n",
            acpi_info.ioapic_count,
            acpi_info.ioapic_count > 0 ? "OK" : "FAIL");

    /* Test 5: I/O APIC has valid address */
    bool ioapic_addr_ok = false;
    if (acpi_info.ioapic_count > 0) {
        ioapic_addr_ok = acpi_info.ioapics[0].address != 0;
    }
    kprintf("  Test 5 - I/O APIC addr: 0x%x %s\n",
            acpi_info.ioapic_count > 0 ? acpi_info.ioapics[0].address : 0,
            ioapic_addr_ok ? "OK" : "FAIL");

    /* Test 6: IRQ-to-GSI mapping */
    uint32_t timer_gsi = acpi_isa_irq_to_gsi(0);   /* Timer */
    uint32_t kbd_gsi = acpi_isa_irq_to_gsi(1);     /* Keyboard */
    uint32_t com1_gsi = acpi_isa_irq_to_gsi(4);    /* COM1 */
    kprintf("  Test 6 - IRQ mapping:\n");
    kprintf("    IRQ 0 (Timer) -> GSI %u\n", timer_gsi);
    kprintf("    IRQ 1 (KBD)   -> GSI %u\n", kbd_gsi);
    kprintf("    IRQ 4 (COM1)  -> GSI %u\n", com1_gsi);

    /* Test 7: HPET detection */
    kprintf("  Test 7 - HPET: %s",
            acpi_info.has_hpet ? "present" : "not present");
    if (acpi_info.has_hpet) {
        kprintf(" at 0x%llx", acpi_info.hpet_address);
    }
    kprintf("\n");

    /* Test 8: Dump CPU info */
    kprintf("\n  CPU Information:\n");
    for (uint32_t i = 0; i < acpi_info.cpu_count && i < 8; i++) {
        acpi_cpu_info_t *cpu = &acpi_info.cpus[i];
        kprintf("    CPU %u: ACPI ID %u, APIC ID %u, %s%s\n",
                i, cpu->acpi_id, cpu->apic_id,
                cpu->enabled ? "enabled" : "disabled",
                cpu->online_capable ? " (online capable)" : "");
    }
    if (acpi_info.cpu_count > 8) {
        kprintf("    ... and %u more\n", acpi_info.cpu_count - 8);
    }

    /* Test 9: Dump I/O APIC info */
    kprintf("\n  I/O APIC Information:\n");
    for (uint32_t i = 0; i < acpi_info.ioapic_count; i++) {
        acpi_ioapic_info_t *io = &acpi_info.ioapics[i];
        kprintf("    I/O APIC %u: addr 0x%08x, GSI base %u\n",
                io->id, io->address, io->gsi_base);
    }

    /* Test 10: Dump interrupt overrides */
    if (acpi_info.override_count > 0) {
        kprintf("\n  Interrupt Overrides:\n");
        for (uint32_t i = 0; i < acpi_info.override_count; i++) {
            acpi_int_override_t *ov = &acpi_info.overrides[i];
            const char *polarity = "default";
            const char *trigger = "default";

            switch (ov->flags & MADT_INT_POLARITY_MASK) {
                case MADT_INT_POLARITY_HIGH: polarity = "high"; break;
                case MADT_INT_POLARITY_LOW:  polarity = "low"; break;
            }
            switch (ov->flags & MADT_INT_TRIGGER_MASK) {
                case MADT_INT_TRIGGER_EDGE:  trigger = "edge"; break;
                case MADT_INT_TRIGGER_LEVEL: trigger = "level"; break;
            }

            kprintf("    IRQ %u -> GSI %u (%s, %s)\n",
                    ov->source, ov->gsi, polarity, trigger);
        }
    }

    /* Summary */
    kprintf("\n  Summary:\n");
    kprintf("    ACPI revision:   %u.0\n", acpi_info.revision == 0 ? 1 : acpi_info.revision);
    kprintf("    Local APIC:      0x%llx\n", acpi_info.local_apic_addr);
    kprintf("    Legacy 8259:     %s\n", acpi_info.has_legacy_pic ? "yes" : "no");
    kprintf("    CPUs:            %u\n", acpi_info.cpu_count);
    kprintf("    I/O APICs:       %u\n", acpi_info.ioapic_count);
    kprintf("    IRQ overrides:   %u\n", acpi_info.override_count);
    kprintf("    HPET:            %s\n", acpi_info.has_hpet ? "yes" : "no");
    kprintf("    FADT:            %s\n", acpi_info.has_fadt ? "yes" : "no");
    if (acpi_info.has_fadt) {
        kprintf("    PM Timer:        0x%x\n", acpi_info.pm_timer_port);
        kprintf("    SCI IRQ:         %u\n", acpi_info.sci_interrupt);
    }

    kprintf("\n");
}
#endif /* DEBUG_TESTS */
