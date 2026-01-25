/* Kurios2 Kernel Linker Script - Higher Half */

ENTRY(_start)

/*
 * Higher-half kernel layout:
 * - Kernel virtual base: 0xFFFFFFFF80000000 (last 2GB)
 * - Kernel physical base: 0x200000 (2MB) - must be 2MB aligned for large pages
 * - This means virtual 0xFFFFFFFF80000000 maps to physical 0x200000
 */

KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;
KERNEL_PHYS_BASE = 0x200000;

/* Calculate offset for address translation */
KERNEL_OFFSET = KERNEL_VIRT_BASE - KERNEL_PHYS_BASE;

SECTIONS
{
    /* Start at virtual address, but load at physical address */
    . = KERNEL_VIRT_BASE;

    /* Export symbols for kernel use */
    _kernel_start = .;
    _kernel_phys_start = . - KERNEL_OFFSET;

    /*
     * Boot section - executed before paging is fully set up
     * This must be position-independent or identity-mapped
     */
    .boot ALIGN(4K) : AT(ADDR(.boot) - KERNEL_OFFSET)
    {
        _boot_start = .;
        *(.boot)
        *(.boot.*)
        _boot_end = .;
    }

    /* Code section */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_OFFSET)
    {
        _text_start = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    }

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_OFFSET)
    {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    }

    /* Global constructors */
    .init_array ALIGN(8) : AT(ADDR(.init_array) - KERNEL_OFFSET)
    {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        __init_array_end = .;
    }

    /* Global destructors */
    .fini_array ALIGN(8) : AT(ADDR(.fini_array) - KERNEL_OFFSET)
    {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        __fini_array_end = .;
    }

    /* Initialized data */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_OFFSET)
    {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    }

    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_OFFSET)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    }

    /* End of kernel */
    . = ALIGN(4K);
    _kernel_end = .;
    _kernel_phys_end = . - KERNEL_OFFSET;

    /* Calculate kernel size */
    _kernel_size = _kernel_end - _kernel_start;
    _kernel_phys_size = _kernel_phys_end - _kernel_phys_start;

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
    }
}

/* Export useful symbols */
PROVIDE(_kernel_virt_base = KERNEL_VIRT_BASE);
PROVIDE(_kernel_phys_base = KERNEL_PHYS_BASE);
PROVIDE(_kernel_offset = KERNEL_OFFSET);
