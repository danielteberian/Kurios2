/* acpi.h - ACPI Table Parsing */
#ifndef _KERNEL_ACPI_H
#define _KERNEL_ACPI_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/types.h"

/*
 * ACPI Table Signatures
 */
#define ACPI_SIG_RSDP   "RSD PTR "  /* Root System Description Pointer */
#define ACPI_SIG_RSDT   "RSDT"      /* Root System Description Table */
#define ACPI_SIG_XSDT   "XSDT"      /* Extended System Description Table */
#define ACPI_SIG_MADT   "APIC"      /* Multiple APIC Description Table */
#define ACPI_SIG_FADT   "FACP"      /* Fixed ACPI Description Table */
#define ACPI_SIG_HPET   "HPET"      /* High Precision Event Timer Table */

/*
 * RSDP - Root System Description Pointer (ACPI 1.0)
 */
typedef struct acpi_rsdp {
    char     signature[8];      /* "RSD PTR " */
    uint8_t  checksum;          /* Checksum of bytes 0-19 */
    char     oem_id[6];         /* OEM identification */
    uint8_t  revision;          /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;      /* 32-bit physical address of RSDT */
} PACKED acpi_rsdp_t;

/*
 * RSDP - Extended (ACPI 2.0+)
 */
typedef struct acpi_rsdp_ext {
    acpi_rsdp_t rsdp;           /* ACPI 1.0 fields */
    uint32_t    length;         /* RSDP length including extended */
    uint64_t    xsdt_address;   /* 64-bit physical address of XSDT */
    uint8_t     ext_checksum;   /* Checksum of entire structure */
    uint8_t     reserved[3];
} PACKED acpi_rsdp_ext_t;

/*
 * SDT Header - Common header for all ACPI tables
 */
typedef struct acpi_sdt_header {
    char     signature[4];      /* Table signature */
    uint32_t length;            /* Total table length including header */
    uint8_t  revision;          /* Table revision */
    uint8_t  checksum;          /* Checksum of entire table */
    char     oem_id[6];         /* OEM identification */
    char     oem_table_id[8];   /* OEM table identification */
    uint32_t oem_revision;      /* OEM revision */
    char     creator_id[4];     /* Creator ID */
    uint32_t creator_revision;  /* Creator revision */
} PACKED acpi_sdt_header_t;

/*
 * RSDT - Root System Description Table (32-bit entries)
 */
typedef struct acpi_rsdt {
    acpi_sdt_header_t header;
    uint32_t          entries[];  /* Array of 32-bit physical addresses */
} PACKED acpi_rsdt_t;

/*
 * XSDT - Extended System Description Table (64-bit entries)
 */
typedef struct acpi_xsdt {
    acpi_sdt_header_t header;
    uint64_t          entries[];  /* Array of 64-bit physical addresses */
} PACKED acpi_xsdt_t;

/*
 * MADT Entry Types
 */
#define MADT_TYPE_LOCAL_APIC        0
#define MADT_TYPE_IO_APIC           1
#define MADT_TYPE_INT_OVERRIDE      2
#define MADT_TYPE_NMI_SOURCE        3
#define MADT_TYPE_LOCAL_APIC_NMI    4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE 5
#define MADT_TYPE_IO_SAPIC          6
#define MADT_TYPE_LOCAL_SAPIC       7
#define MADT_TYPE_PLATFORM_INT      8
#define MADT_TYPE_LOCAL_X2APIC      9
#define MADT_TYPE_LOCAL_X2APIC_NMI  10

/*
 * MADT Entry Header
 */
typedef struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} PACKED madt_entry_header_t;

/*
 * MADT Local APIC Entry (Type 0)
 */
typedef struct madt_local_apic {
    madt_entry_header_t header;
    uint8_t  acpi_processor_id;  /* ACPI processor unique ID */
    uint8_t  apic_id;            /* Processor's local APIC ID */
    uint32_t flags;              /* Bit 0: Processor Enabled */
} PACKED madt_local_apic_t;

#define MADT_LAPIC_FLAG_ENABLED     (1 << 0)
#define MADT_LAPIC_FLAG_ONLINE_CAP  (1 << 1)

/*
 * MADT I/O APIC Entry (Type 1)
 */
typedef struct madt_io_apic {
    madt_entry_header_t header;
    uint8_t  io_apic_id;         /* I/O APIC's unique ID */
    uint8_t  reserved;
    uint32_t io_apic_address;    /* 32-bit physical address */
    uint32_t gsi_base;           /* Global System Interrupt base */
} PACKED madt_io_apic_t;

/*
 * MADT Interrupt Source Override Entry (Type 2)
 */
typedef struct madt_int_override {
    madt_entry_header_t header;
    uint8_t  bus;                /* 0 = ISA */
    uint8_t  source;             /* Bus-relative source (ISA IRQ) */
    uint32_t gsi;                /* Global System Interrupt */
    uint16_t flags;              /* MPS INTI flags */
} PACKED madt_int_override_t;

/* Polarity flags (bits 0-1) */
#define MADT_INT_POLARITY_MASK      0x03
#define MADT_INT_POLARITY_DEFAULT   0x00
#define MADT_INT_POLARITY_HIGH      0x01
#define MADT_INT_POLARITY_LOW       0x03

/* Trigger mode flags (bits 2-3) */
#define MADT_INT_TRIGGER_MASK       0x0C
#define MADT_INT_TRIGGER_DEFAULT    0x00
#define MADT_INT_TRIGGER_EDGE       0x04
#define MADT_INT_TRIGGER_LEVEL      0x0C

/*
 * MADT - Multiple APIC Description Table
 */
typedef struct acpi_madt {
    acpi_sdt_header_t header;
    uint32_t local_apic_address; /* Physical address of Local APIC */
    uint32_t flags;              /* Bit 0: Dual 8259 legacy PICs installed */
    /* Variable-length entries follow */
} PACKED acpi_madt_t;

#define MADT_FLAG_PCAT_COMPAT   (1 << 0)  /* Dual 8259 legacy PICs */

/*
 * FADT - Fixed ACPI Description Table (selected fields)
 */
typedef struct acpi_fadt {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;      /* Physical address of FACS */
    uint32_t dsdt;               /* Physical address of DSDT */
    uint8_t  reserved1;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;            /* SCI interrupt */
    uint32_t smi_cmd;            /* SMI command port */
    uint8_t  acpi_enable;        /* Value to write to enable ACPI */
    uint8_t  acpi_disable;       /* Value to write to disable ACPI */
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;       /* PM1a Event Block address */
    uint32_t pm1b_evt_blk;       /* PM1b Event Block address */
    uint32_t pm1a_cnt_blk;       /* PM1a Control Block address */
    uint32_t pm1b_cnt_blk;       /* PM1b Control Block address */
    uint32_t pm2_cnt_blk;        /* PM2 Control Block address */
    uint32_t pm_tmr_blk;         /* PM Timer Block address */
    uint32_t gpe0_blk;           /* GPE0 Block address */
    uint32_t gpe1_blk;           /* GPE1 Block address */
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alarm;
    uint8_t  mon_alarm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved2;
    uint32_t flags;
    /* More fields follow in ACPI 2.0+, but we only need these */
} PACKED acpi_fadt_t;

/*
 * HPET - High Precision Event Timer Table
 */
typedef struct acpi_hpet {
    acpi_sdt_header_t header;
    uint32_t event_timer_block_id;
    uint8_t  address_space_id;   /* 0 = memory, 1 = I/O */
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  reserved;
    uint64_t address;            /* Base address of HPET */
    uint8_t  hpet_number;        /* HPET sequence number */
    uint16_t minimum_tick;       /* Minimum clock tick in periodic mode */
    uint8_t  page_protection;
} PACKED acpi_hpet_t;

/*
 * Parsed CPU Information
 */
typedef struct acpi_cpu_info {
    uint8_t  acpi_id;            /* ACPI processor ID */
    uint8_t  apic_id;            /* Local APIC ID */
    bool     enabled;            /* Processor enabled */
    bool     online_capable;     /* Can be enabled */
} acpi_cpu_info_t;

/*
 * Parsed I/O APIC Information
 */
typedef struct acpi_ioapic_info {
    uint8_t  id;                 /* I/O APIC ID */
    uint32_t address;            /* Physical address */
    uint32_t gsi_base;           /* Global System Interrupt base */
} acpi_ioapic_info_t;

/*
 * Parsed Interrupt Override Information
 */
typedef struct acpi_int_override {
    uint8_t  source;             /* ISA IRQ number */
    uint32_t gsi;                /* Mapped GSI */
    uint16_t flags;              /* Polarity and trigger mode */
} acpi_int_override_t;

/*
 * ACPI Information - Kernel-friendly parsed data
 */
#define ACPI_MAX_CPUS           256
#define ACPI_MAX_IOAPICS        8
#define ACPI_MAX_OVERRIDES      24

typedef struct acpi_info {
    bool     valid;              /* ACPI tables found and parsed */
    uint8_t  revision;           /* ACPI revision (0=1.0, 2=2.0+) */

    /* Local APIC */
    uint64_t local_apic_addr;    /* Local APIC physical address */
    bool     has_legacy_pic;     /* Dual 8259 legacy PICs installed */

    /* CPUs */
    uint32_t cpu_count;          /* Number of CPUs found */
    acpi_cpu_info_t cpus[ACPI_MAX_CPUS];

    /* I/O APICs */
    uint32_t ioapic_count;       /* Number of I/O APICs found */
    acpi_ioapic_info_t ioapics[ACPI_MAX_IOAPICS];

    /* Interrupt overrides */
    uint32_t override_count;     /* Number of interrupt overrides */
    acpi_int_override_t overrides[ACPI_MAX_OVERRIDES];

    /* HPET */
    bool     has_hpet;           /* HPET table found */
    uint64_t hpet_address;       /* HPET base address */

    /* FADT */
    bool     has_fadt;           /* FADT table found */
    uint32_t pm_timer_port;      /* PM timer I/O port */
    uint16_t sci_interrupt;      /* SCI interrupt number */
} acpi_info_t;

/*
 * Public API
 */

/* Initialize ACPI - parse tables from boot info */
int acpi_init(void *boot_info);

/* Get parsed ACPI information */
const acpi_info_t *acpi_get_info(void);

/* Convenience accessors */
uint64_t acpi_get_local_apic_addr(void);
uint32_t acpi_get_cpu_count(void);
uint32_t acpi_get_ioapic_count(void);
bool     acpi_has_legacy_pic(void);
bool     acpi_has_hpet(void);
uint64_t acpi_get_hpet_addr(void);

/* Map ISA IRQ to Global System Interrupt (handles overrides) */
uint32_t acpi_isa_irq_to_gsi(uint8_t irq);

/* Get interrupt override flags for an ISA IRQ (0 if no override) */
uint16_t acpi_get_irq_flags(uint8_t irq);

#ifdef DEBUG_TESTS
/* Run ACPI tests */
void acpi_run_tests(void);
#endif

#endif /* _KERNEL_ACPI_H */
