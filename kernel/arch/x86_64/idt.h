/* idt.h - Interrupt Descriptor Table */
#ifndef _ARCH_IDT_H
#define _ARCH_IDT_H

#include <stdint.h>

/*
 * IDT Entry (Gate Descriptor) - 16 bytes in 64-bit mode
 *
 * Used for interrupt gates, trap gates, and task gates.
 * In 64-bit mode, we use interrupt gates (clears IF) and trap gates (keeps IF).
 */
typedef struct {
    uint16_t offset_low;        /* Offset bits 0-15 */
    uint16_t selector;          /* Code segment selector */
    uint8_t  ist;               /* IST index (bits 0-2), rest reserved */
    uint8_t  type_attr;         /* Type and attributes */
    uint16_t offset_mid;        /* Offset bits 16-31 */
    uint32_t offset_high;       /* Offset bits 32-63 */
    uint32_t reserved;          /* Reserved, must be 0 */
} __attribute__((packed)) idt_entry_t;

/*
 * IDT Pointer (for LIDT instruction)
 */
typedef struct {
    uint16_t limit;             /* Size of IDT - 1 */
    uint64_t base;              /* Linear address of IDT */
} __attribute__((packed)) idt_pointer_t;

/*
 * Gate types
 */
#define IDT_TYPE_INTERRUPT  0x0E    /* 64-bit interrupt gate */
#define IDT_TYPE_TRAP       0x0F    /* 64-bit trap gate */

/*
 * Gate attributes
 */
#define IDT_ATTR_PRESENT    (1 << 7)    /* Present bit */
#define IDT_ATTR_DPL0       (0 << 5)    /* Ring 0 */
#define IDT_ATTR_DPL3       (3 << 5)    /* Ring 3 (for syscalls) */

/*
 * Common gate attributes
 */
#define IDT_INTERRUPT_GATE  (IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT)
#define IDT_TRAP_GATE       (IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_TRAP)
#define IDT_USER_INTERRUPT  (IDT_ATTR_PRESENT | IDT_ATTR_DPL3 | IDT_TYPE_INTERRUPT)

/*
 * Number of IDT entries (256 total: 0-31 exceptions, 32-255 interrupts)
 */
#define IDT_ENTRIES         256

/*
 * CPU Exception numbers (0-31)
 */
#define INT_DIVIDE_ERROR        0   /* #DE - Divide Error */
#define INT_DEBUG               1   /* #DB - Debug Exception */
#define INT_NMI                 2   /* NMI - Non-Maskable Interrupt */
#define INT_BREAKPOINT          3   /* #BP - Breakpoint */
#define INT_OVERFLOW            4   /* #OF - Overflow */
#define INT_BOUND_RANGE         5   /* #BR - Bound Range Exceeded */
#define INT_INVALID_OPCODE      6   /* #UD - Invalid Opcode */
#define INT_DEVICE_NA           7   /* #NM - Device Not Available */
#define INT_DOUBLE_FAULT        8   /* #DF - Double Fault */
#define INT_COPROC_SEG          9   /* Coprocessor Segment Overrun (reserved) */
#define INT_INVALID_TSS         10  /* #TS - Invalid TSS */
#define INT_SEGMENT_NP          11  /* #NP - Segment Not Present */
#define INT_STACK_FAULT         12  /* #SS - Stack-Segment Fault */
#define INT_GPF                 13  /* #GP - General Protection Fault */
#define INT_PAGE_FAULT          14  /* #PF - Page Fault */
#define INT_RESERVED_15         15  /* Reserved */
#define INT_X87_FP              16  /* #MF - x87 FPU Error */
#define INT_ALIGNMENT           17  /* #AC - Alignment Check */
#define INT_MACHINE_CHECK       18  /* #MC - Machine Check */
#define INT_SIMD_FP             19  /* #XM/#XF - SIMD Floating-Point */
#define INT_VIRTUALIZATION      20  /* #VE - Virtualization Exception */
#define INT_CONTROL_PROT        21  /* #CP - Control Protection Exception */
/* 22-31 are reserved */

/*
 * IRQ numbers (remapped to 32-47)
 */
#define IRQ_BASE            32
#define IRQ_TIMER           (IRQ_BASE + 0)
#define IRQ_KEYBOARD        (IRQ_BASE + 1)
#define IRQ_CASCADE         (IRQ_BASE + 2)
#define IRQ_COM2            (IRQ_BASE + 3)
#define IRQ_COM1            (IRQ_BASE + 4)
#define IRQ_LPT2            (IRQ_BASE + 5)
#define IRQ_FLOPPY          (IRQ_BASE + 6)
#define IRQ_LPT1            (IRQ_BASE + 7)
#define IRQ_RTC             (IRQ_BASE + 8)
#define IRQ_MOUSE           (IRQ_BASE + 12)
#define IRQ_FPU             (IRQ_BASE + 13)
#define IRQ_ATA_PRIMARY     (IRQ_BASE + 14)
#define IRQ_ATA_SECONDARY   (IRQ_BASE + 15)

/*
 * IST (Interrupt Stack Table) assignments
 * Using separate stacks for critical exceptions prevents stack corruption
 */
#define IST_NONE            0   /* Use current stack */
#define IST_DOUBLE_FAULT    1   /* Double fault uses IST1 */
#define IST_NMI             2   /* NMI uses IST2 */
#define IST_MCE             3   /* Machine check uses IST3 */

/*
 * Initialize the IDT
 * Must be called after GDT is set up
 */
void idt_init(void);

/*
 * Set an IDT entry
 * vector: interrupt number (0-255)
 * handler: address of the interrupt handler
 * selector: code segment selector (usually kernel CS)
 * ist: IST index (0 = no IST, 1-7 = use IST)
 * type_attr: gate type and attributes
 */
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                  uint8_t ist, uint8_t type_attr);

/*
 * Register an interrupt handler callback
 * vector: interrupt number
 * handler: function to call when interrupt occurs
 */
typedef void (*interrupt_handler_t)(void *cpu_state);
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

/*
 * Assembly function to load the IDT
 */
extern void idt_flush(idt_pointer_t *idt_ptr);

/*
 * Exception names (for debugging)
 */
const char* exception_name(uint8_t vector);

#endif /* _ARCH_IDT_H */
