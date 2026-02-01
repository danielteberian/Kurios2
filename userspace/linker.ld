/* User-space linker script for Kurios2 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* User-space starts at 4MB (matches kernel's USER_SPACE_START) */
SECTIONS {
    . = 0x400000;

    /* Code section */
    .text : {
        *(.text .text.*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);

    /* Initialized data */
    .data : {
        *(.data .data.*)
    }

    /* Uninitialized data (BSS) */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    /* Discard debug sections to keep binaries small */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.note.GNU-stack)
    }
}
