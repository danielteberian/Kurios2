/* Kurios2 Kernel Linker Script */

ENTRY(_start)

/* Kernel loaded at 1MB */
KERNEL_PHYS = 0x100000;

SECTIONS
{
    . = KERNEL_PHYS;

    /* Code section */
    .text ALIGN(4K) : AT(ADDR(.text))
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata))
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data ALIGN(4K) : AT(ADDR(.data))
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data */
    .bss ALIGN(4K) : AT(ADDR(.bss))
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* End of kernel */
    _kernel_end = .;

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
