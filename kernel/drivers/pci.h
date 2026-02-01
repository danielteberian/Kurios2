/* pci.h - PCI Bus Access */
#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H

#include <stdint.h>
#include <stdbool.h>

/* PCI configuration space I/O ports */
#define PCI_CONFIG_ADDR     0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI configuration space registers */
#define PCI_VENDOR_ID       0x00    /* 16-bit */
#define PCI_DEVICE_ID       0x02    /* 16-bit */
#define PCI_COMMAND         0x04    /* 16-bit */
#define PCI_STATUS          0x06    /* 16-bit */
#define PCI_REVISION_ID     0x08    /* 8-bit */
#define PCI_PROG_IF         0x09    /* 8-bit */
#define PCI_SUBCLASS        0x0A    /* 8-bit */
#define PCI_CLASS           0x0B    /* 8-bit */
#define PCI_CACHE_LINE      0x0C    /* 8-bit */
#define PCI_LATENCY_TIMER   0x0D    /* 8-bit */
#define PCI_HEADER_TYPE     0x0E    /* 8-bit */
#define PCI_BIST            0x0F    /* 8-bit */
#define PCI_BAR0            0x10    /* 32-bit */
#define PCI_BAR1            0x14    /* 32-bit */
#define PCI_BAR2            0x18    /* 32-bit */
#define PCI_BAR3            0x1C    /* 32-bit */
#define PCI_BAR4            0x20    /* 32-bit */
#define PCI_BAR5            0x24    /* 32-bit */
#define PCI_SUBSYSTEM_VENDOR 0x2C   /* 16-bit */
#define PCI_SUBSYSTEM_ID    0x2E    /* 16-bit */
#define PCI_INTERRUPT_LINE  0x3C    /* 8-bit */
#define PCI_INTERRUPT_PIN   0x3D    /* 8-bit */

/* PCI command register bits */
#define PCI_CMD_IO_SPACE        0x0001
#define PCI_CMD_MEMORY_SPACE    0x0002
#define PCI_CMD_BUS_MASTER      0x0004
#define PCI_CMD_SPECIAL_CYCLES  0x0008
#define PCI_CMD_MWI_ENABLE      0x0010
#define PCI_CMD_VGA_PALETTE     0x0020
#define PCI_CMD_PARITY          0x0040
#define PCI_CMD_SERR            0x0100
#define PCI_CMD_FAST_BTB        0x0200
#define PCI_CMD_INT_DISABLE     0x0400

/* BAR types */
#define PCI_BAR_TYPE_MASK       0x01
#define PCI_BAR_TYPE_IO         0x01
#define PCI_BAR_TYPE_MEM        0x00
#define PCI_BAR_MEM_TYPE_MASK   0x06
#define PCI_BAR_MEM_32          0x00
#define PCI_BAR_MEM_64          0x04
#define PCI_BAR_MEM_PREFETCH    0x08

/* Invalid vendor ID (empty slot) */
#define PCI_VENDOR_NONE         0xFFFF

/* Maximum PCI bus/device/function numbers */
#define PCI_MAX_BUS             256
#define PCI_MAX_DEVICE          32
#define PCI_MAX_FUNCTION        8

/*
 * PCI device structure
 */
typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t irq;

    /* BARs */
    uint32_t bar[6];
    uint32_t bar_size[6];
    bool bar_is_io[6];
    bool bar_is_64[6];
} pci_device_t;

/*
 * PCI device callback for enumeration
 */
typedef void (*pci_enum_callback_t)(pci_device_t *dev, void *ctx);

/*
 * Initialize PCI subsystem
 */
void pci_init(void);

/*
 * Read 8-bit PCI configuration register
 */
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/*
 * Read 16-bit PCI configuration register
 */
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/*
 * Read 32-bit PCI configuration register
 */
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/*
 * Write 8-bit PCI configuration register
 */
void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);

/*
 * Write 16-bit PCI configuration register
 */
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

/*
 * Write 32-bit PCI configuration register
 */
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

/*
 * Get BAR address (decoded)
 *
 * @param dev      PCI device
 * @param bar_num  BAR index (0-5)
 * @return BAR address (physical), or 0 if not valid
 */
uint64_t pci_get_bar_addr(pci_device_t *dev, int bar_num);

/*
 * Get BAR size
 *
 * @param dev      PCI device
 * @param bar_num  BAR index (0-5)
 * @return BAR size in bytes
 */
uint32_t pci_get_bar_size(pci_device_t *dev, int bar_num);

/*
 * Enable bus mastering for a device
 */
void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func);

/*
 * Enable I/O space access for a device
 */
void pci_enable_io_space(uint8_t bus, uint8_t slot, uint8_t func);

/*
 * Enable memory space access for a device
 */
void pci_enable_mem_space(uint8_t bus, uint8_t slot, uint8_t func);

/*
 * Enumerate all PCI devices
 *
 * @param callback  Function to call for each device
 * @param ctx       Context pointer passed to callback
 */
void pci_enumerate(pci_enum_callback_t callback, void *ctx);

/*
 * Find device by vendor/device ID
 *
 * @param vendor   Vendor ID to match
 * @param device   Device ID to match
 * @param out_dev  Output device structure (filled on match)
 * @return true if found, false otherwise
 */
bool pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out_dev);

/*
 * Find device by class
 *
 * @param class_code  Class code to match
 * @param subclass    Subclass to match
 * @param out_dev     Output device structure (filled on match)
 * @return true if found, false otherwise
 */
bool pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out_dev);

/*
 * Read device info into structure
 *
 * @param bus   PCI bus number
 * @param slot  PCI device number
 * @param func  PCI function number
 * @param dev   Output device structure
 */
void pci_read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *dev);

/*
 * Print PCI device info (for debugging)
 */
void pci_dump_device(pci_device_t *dev);

/*
 * List all PCI devices (for debugging)
 */
void pci_list_devices(void);

#endif /* _KERNEL_PCI_H */
