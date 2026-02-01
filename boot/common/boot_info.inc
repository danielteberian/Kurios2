; boot_info.inc - Common boot information structure
; Passed from bootloader to kernel in RDI register

; Boot info structure offsets (64-bit aligned)
BOOT_INFO_MAGIC         equ 0x00    ; Magic number: 0x4B55524953 ("KURIS")
BOOT_INFO_VERSION       equ 0x08    ; Boot protocol version
BOOT_INFO_FLAGS         equ 0x10    ; Feature flags
BOOT_INFO_MEMORY_MAP    equ 0x18    ; Pointer to memory map
BOOT_INFO_MEMORY_COUNT  equ 0x20    ; Number of memory map entries
BOOT_INFO_FRAMEBUFFER   equ 0x28    ; Pointer to framebuffer info
BOOT_INFO_ACPI_RSDP     equ 0x30    ; ACPI RSDP pointer
BOOT_INFO_KERNEL_PHYS   equ 0x38    ; Kernel physical load address
BOOT_INFO_KERNEL_SIZE   equ 0x40    ; Kernel size in bytes
BOOT_INFO_CMDLINE       equ 0x48    ; Pointer to command line string
BOOT_INFO_BOOT_DRIVE    equ 0x50    ; Boot drive number
BOOT_INFO_INITRD_START  equ 0x58    ; Initial ramdisk physical address
BOOT_INFO_INITRD_SIZE   equ 0x60    ; Initial ramdisk size in bytes
BOOT_INFO_SIZE          equ 0x68    ; Total structure size

; Magic number
KURIOS_BOOT_MAGIC       equ 0x4B55524953  ; "KURIS" in ASCII

; Boot protocol version
BOOT_PROTOCOL_VERSION   equ 0x0001

; Boot flags
BOOT_FLAG_BIOS          equ (1 << 0)    ; Booted via BIOS
BOOT_FLAG_UEFI          equ (1 << 1)    ; Booted via UEFI
BOOT_FLAG_FRAMEBUFFER   equ (1 << 2)    ; Framebuffer available
BOOT_FLAG_ACPI          equ (1 << 3)    ; ACPI tables found
BOOT_FLAG_INITRD        equ (1 << 4)    ; Initial ramdisk loaded

; Memory map entry structure
; Compatible with E820 format
MMAP_ENTRY_BASE         equ 0x00    ; Base address (64-bit)
MMAP_ENTRY_LENGTH       equ 0x08    ; Length in bytes (64-bit)
MMAP_ENTRY_TYPE         equ 0x10    ; Type (32-bit)
MMAP_ENTRY_ACPI         equ 0x14    ; ACPI extended attributes (32-bit)
MMAP_ENTRY_SIZE         equ 0x18    ; Size of each entry

; Memory types (E820 compatible)
MMAP_TYPE_USABLE        equ 1       ; Usable RAM
MMAP_TYPE_RESERVED      equ 2       ; Reserved
MMAP_TYPE_ACPI_RECLAIM  equ 3       ; ACPI reclaimable
MMAP_TYPE_ACPI_NVS      equ 4       ; ACPI NVS
MMAP_TYPE_BAD           equ 5       ; Bad memory

; Framebuffer info structure
FB_INFO_ADDRESS         equ 0x00    ; Framebuffer physical address
FB_INFO_WIDTH           equ 0x08    ; Width in pixels
FB_INFO_HEIGHT          equ 0x0C    ; Height in pixels
FB_INFO_PITCH           equ 0x10    ; Bytes per scanline
FB_INFO_BPP             equ 0x14    ; Bits per pixel
FB_INFO_RED_MASK        equ 0x18    ; Red channel mask
FB_INFO_GREEN_MASK      equ 0x1C    ; Green channel mask
FB_INFO_BLUE_MASK       equ 0x20    ; Blue channel mask
FB_INFO_SIZE            equ 0x28    ; Structure size

; Standard locations
STAGE2_LOAD_SEG         equ 0x1000      ; Stage 2 loaded at 0x10000
STAGE2_LOAD_OFF         equ 0x0000
KERNEL_LOAD_ADDR        equ 0x200000    ; Kernel loaded at 2MB (for 2MB page alignment)
BOOT_INFO_ADDR          equ 0x9000      ; Boot info structure at 0x9000
MEMORY_MAP_ADDR         equ 0x9100      ; Memory map follows boot info
FRAMEBUFFER_INFO_ADDR   equ 0x9800      ; Framebuffer info
STACK_TOP               equ 0x7C00      ; Stack grows down from boot sector
