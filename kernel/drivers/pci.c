/* pci.c - PCI Bus Access Implementation */

#include "pci.h"
#include "../debug/debug.h"
#include "../arch/x86_64/cpu.h"

/* I/O port access */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/*
 * Calculate PCI configuration address
 */
static uint32_t pci_config_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (1 << 31) |                  /* Enable bit */
           ((uint32_t)bus << 16) |
           ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) |
           (offset & 0xFC);             /* Align to 32-bit */
}

/*
 * Read 32-bit PCI config register
 */
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_config_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

/*
 * Write 32-bit PCI config register
 */
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_config_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

/*
 * Read 16-bit PCI config register
 */
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, slot, func, offset & ~0x02);
    return (val >> ((offset & 0x02) * 8)) & 0xFFFF;
}

/*
 * Write 16-bit PCI config register
 */
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t tmp = pci_read32(bus, slot, func, offset & ~0x02);
    int shift = (offset & 0x02) * 8;
    tmp = (tmp & ~(0xFFFF << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, slot, func, offset & ~0x02, tmp);
}

/*
 * Read 8-bit PCI config register
 */
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, slot, func, offset & ~0x03);
    return (val >> ((offset & 0x03) * 8)) & 0xFF;
}

/*
 * Write 8-bit PCI config register
 */
void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val) {
    uint32_t tmp = pci_read32(bus, slot, func, offset & ~0x03);
    int shift = (offset & 0x03) * 8;
    tmp = (tmp & ~(0xFF << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, slot, func, offset & ~0x03, tmp);
}

/*
 * Enable bus mastering
 */
void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_read16(bus, slot, func, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_write16(bus, slot, func, PCI_COMMAND, cmd);
}

/*
 * Enable I/O space access
 */
void pci_enable_io_space(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_read16(bus, slot, func, PCI_COMMAND);
    cmd |= PCI_CMD_IO_SPACE;
    pci_write16(bus, slot, func, PCI_COMMAND, cmd);
}

/*
 * Enable memory space access
 */
void pci_enable_mem_space(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_read16(bus, slot, func, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE;
    pci_write16(bus, slot, func, PCI_COMMAND, cmd);
}

/*
 * Read device info into structure
 */
void pci_read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *dev) {
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;

    dev->vendor_id = pci_read16(bus, slot, func, PCI_VENDOR_ID);
    dev->device_id = pci_read16(bus, slot, func, PCI_DEVICE_ID);
    dev->class_code = pci_read8(bus, slot, func, PCI_CLASS);
    dev->subclass = pci_read8(bus, slot, func, PCI_SUBCLASS);
    dev->prog_if = pci_read8(bus, slot, func, PCI_PROG_IF);
    dev->revision = pci_read8(bus, slot, func, PCI_REVISION_ID);
    dev->header_type = pci_read8(bus, slot, func, PCI_HEADER_TYPE);
    dev->irq = pci_read8(bus, slot, func, PCI_INTERRUPT_LINE);

    /* Read BARs (only for header type 0) */
    if ((dev->header_type & 0x7F) == 0) {
        for (int i = 0; i < 6; i++) {
            uint32_t bar = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
            dev->bar[i] = bar;
            dev->bar_is_io[i] = (bar & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_IO;
            dev->bar_is_64[i] = !dev->bar_is_io[i] &&
                                ((bar & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_64);

            /* Get BAR size by writing all 1s and reading back */
            pci_write32(bus, slot, func, PCI_BAR0 + i * 4, 0xFFFFFFFF);
            uint32_t size_mask = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
            pci_write32(bus, slot, func, PCI_BAR0 + i * 4, bar);

            if (dev->bar_is_io[i]) {
                size_mask &= ~0x03;  /* Clear I/O bit and reserved */
            } else {
                size_mask &= ~0x0F;  /* Clear type bits */
            }

            if (size_mask) {
                dev->bar_size[i] = (~size_mask) + 1;
            } else {
                dev->bar_size[i] = 0;
            }

            /* Skip next BAR if this is 64-bit */
            if (dev->bar_is_64[i] && i < 5) {
                i++;
                dev->bar[i] = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
                dev->bar_size[i] = 0;
                dev->bar_is_io[i] = false;
                dev->bar_is_64[i] = false;
            }
        }
    }
}

/*
 * Get BAR address
 */
uint64_t pci_get_bar_addr(pci_device_t *dev, int bar_num) {
    if (bar_num < 0 || bar_num > 5) return 0;

    uint64_t addr = dev->bar[bar_num];
    if (dev->bar_is_io[bar_num]) {
        return addr & ~0x03ULL;
    }

    addr &= ~0x0FULL;  /* Clear type bits */

    /* Handle 64-bit BAR */
    if (dev->bar_is_64[bar_num] && bar_num < 5) {
        addr |= ((uint64_t)dev->bar[bar_num + 1]) << 32;
    }

    return addr;
}

/*
 * Get BAR size
 */
uint32_t pci_get_bar_size(pci_device_t *dev, int bar_num) {
    if (bar_num < 0 || bar_num > 5) return 0;
    return dev->bar_size[bar_num];
}

/*
 * Enumerate all PCI devices
 */
void pci_enumerate(pci_enum_callback_t callback, void *ctx) {
    for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (int slot = 0; slot < PCI_MAX_DEVICE; slot++) {
            uint16_t vendor = pci_read16(bus, slot, 0, PCI_VENDOR_ID);
            if (vendor == PCI_VENDOR_NONE) continue;

            uint8_t header = pci_read8(bus, slot, 0, PCI_HEADER_TYPE);
            int max_func = (header & 0x80) ? PCI_MAX_FUNCTION : 1;

            for (int func = 0; func < max_func; func++) {
                if (func > 0) {
                    vendor = pci_read16(bus, slot, func, PCI_VENDOR_ID);
                    if (vendor == PCI_VENDOR_NONE) continue;
                }

                pci_device_t dev;
                pci_read_device(bus, slot, func, &dev);

                if (callback) {
                    callback(&dev, ctx);
                }
            }
        }
    }
}

/*
 * Find device by vendor/device ID
 */
typedef struct {
    uint16_t vendor;
    uint16_t device;
    pci_device_t *out_dev;
    bool found;
} pci_find_ctx_t;

static void pci_find_callback(pci_device_t *dev, void *ctx) {
    pci_find_ctx_t *find_ctx = ctx;
    if (!find_ctx->found &&
        dev->vendor_id == find_ctx->vendor &&
        dev->device_id == find_ctx->device) {
        *find_ctx->out_dev = *dev;
        find_ctx->found = true;
    }
}

bool pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out_dev) {
    pci_find_ctx_t ctx = {
        .vendor = vendor,
        .device = device,
        .out_dev = out_dev,
        .found = false
    };
    pci_enumerate(pci_find_callback, &ctx);
    return ctx.found;
}

/*
 * Find device by class
 */
typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    pci_device_t *out_dev;
    bool found;
} pci_find_class_ctx_t;

static void pci_find_class_callback(pci_device_t *dev, void *ctx) {
    pci_find_class_ctx_t *find_ctx = ctx;
    if (!find_ctx->found &&
        dev->class_code == find_ctx->class_code &&
        dev->subclass == find_ctx->subclass) {
        *find_ctx->out_dev = *dev;
        find_ctx->found = true;
    }
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out_dev) {
    pci_find_class_ctx_t ctx = {
        .class_code = class_code,
        .subclass = subclass,
        .out_dev = out_dev,
        .found = false
    };
    pci_enumerate(pci_find_class_callback, &ctx);
    return ctx.found;
}

/*
 * Dump device info
 */
void pci_dump_device(pci_device_t *dev) {
    kprintf("PCI %02x:%02x.%x - Vendor: %04x Device: %04x\n",
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id);
    kprintf("  Class: %02x Subclass: %02x ProgIF: %02x Rev: %02x\n",
            dev->class_code, dev->subclass, dev->prog_if, dev->revision);
    kprintf("  IRQ: %d Header: %02x\n", dev->irq, dev->header_type);

    for (int i = 0; i < 6; i++) {
        if (dev->bar_size[i] > 0) {
            kprintf("  BAR%d: %08x (%s, %u bytes)\n",
                    i, dev->bar[i],
                    dev->bar_is_io[i] ? "I/O" : "MEM",
                    dev->bar_size[i]);
        }
    }
}

/*
 * List all PCI devices
 */
static void pci_list_callback(pci_device_t *dev, void *ctx) {
    uint32_t *count = ctx;
    kprintf("  %02x:%02x.%x  %04x:%04x  Class %02x.%02x\n",
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id,
            dev->class_code, dev->subclass);
    (*count)++;
}

void pci_list_devices(void) {
    uint32_t count = 0;
    kprintf("=== PCI Devices ===\n");
    kprintf("  Bus:Dev.Fn  VID:DID   Class\n");
    pci_enumerate(pci_list_callback, &count);
    kprintf("Total: %u devices\n\n", count);
}

/*
 * Initialize PCI subsystem
 */
void pci_init(void) {
    INFO("Initializing PCI subsystem");

    /* Test if PCI is available by reading vendor at 00:00.0 */
    uint16_t test = pci_read16(0, 0, 0, PCI_VENDOR_ID);
    if (test == 0xFFFF || test == 0x0000) {
        WARN("PCI not detected or not accessible");
        return;
    }

    INFO("PCI subsystem initialized");
}
