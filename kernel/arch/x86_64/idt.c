/* idt.c - Interrupt Descriptor Table */

#include "idt.h"
#include "gdt.h"
#include "cpu.h"
#include "io.h"
#include "../../debug/debug.h"
#include "../../mm/pmm.h"

/* IDT entries */
static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

/* IDT pointer for LIDT */
static idt_pointer_t idt_ptr;

/* Interrupt handler callbacks */
static interrupt_handler_t handlers[IDT_ENTRIES];

/* IST stack size (16KB each) */
#define IST_STACK_SIZE  (16 * 1024)

/* IST stacks */
static uint8_t ist1_stack[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist2_stack[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist3_stack[IST_STACK_SIZE] __attribute__((aligned(16)));

/* External ISR stubs from assembly */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Exception names for debugging */
static const char *exception_names[] = {
    "Divide Error",             /* 0 */
    "Debug",                    /* 1 */
    "NMI",                      /* 2 */
    "Breakpoint",               /* 3 */
    "Overflow",                 /* 4 */
    "Bound Range Exceeded",     /* 5 */
    "Invalid Opcode",           /* 6 */
    "Device Not Available",     /* 7 */
    "Double Fault",             /* 8 */
    "Coprocessor Segment",      /* 9 */
    "Invalid TSS",              /* 10 */
    "Segment Not Present",      /* 11 */
    "Stack-Segment Fault",      /* 12 */
    "General Protection Fault", /* 13 */
    "Page Fault",               /* 14 */
    "Reserved",                 /* 15 */
    "x87 FPU Error",            /* 16 */
    "Alignment Check",          /* 17 */
    "Machine Check",            /* 18 */
    "SIMD Floating-Point",      /* 19 */
    "Virtualization",           /* 20 */
    "Control Protection",       /* 21 */
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"      /* 22-31 */
};

/*
 * Set an IDT entry
 */
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                  uint8_t ist, uint8_t type_attr) {
    idt_entry_t *entry = &idt[vector];

    entry->offset_low = handler & 0xFFFF;
    entry->selector = selector;
    entry->ist = ist & 0x07;
    entry->type_attr = type_attr;
    entry->offset_mid = (handler >> 16) & 0xFFFF;
    entry->offset_high = (handler >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}

/*
 * Register an interrupt handler callback
 */
void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

/*
 * Get exception name
 */
const char* exception_name(uint8_t vector) {
    if (vector < 32) {
        return exception_names[vector];
    } else if (vector >= IRQ_BASE && vector < IRQ_BASE + 16) {
        return "Hardware IRQ";
    }
    return "Unknown";
}

/*
 * Default exception handler - called from assembly
 */
void exception_handler(cpu_state_t *state) {
    uint8_t vector = state->int_no;

    /* Check if a handler is registered */
    if (handlers[vector]) {
        handlers[vector](state);
        return;
    }

    /* No handler - this is a fatal exception */
    cli();

    kprintf("\n");
    kprintf("!!! UNHANDLED EXCEPTION !!!\n");
    kprintf("Exception %llu: %s\n", state->int_no, exception_name(vector));
    kprintf("Error code: 0x%llx\n", state->error_code);
    kprintf("\n");

    /* Print registers */
    dump_registers(state);

    /* Extra info for page faults */
    if (vector == INT_PAGE_FAULT) {
        uint64_t cr2 = read_cr2();
        kprintf("\nPage fault address: 0x%016llx\n", cr2);
        kprintf("Fault type: %s %s %s\n",
                (state->error_code & 1) ? "protection" : "not-present",
                (state->error_code & 2) ? "write" : "read",
                (state->error_code & 4) ? "user" : "supervisor");
    }

    kprintf("\nStack trace:\n");
    stack_trace_from(state->rbp, state->rip);

    kprintf("\nSystem halted.\n");
    while (1) { cli(); hlt(); }
}

/*
 * IRQ handler - called from assembly
 */
void irq_handler(cpu_state_t *state) {
    uint8_t irq = state->int_no - IRQ_BASE;

    /* Call registered handler if any */
    if (handlers[state->int_no]) {
        handlers[state->int_no](state);
    }

    /* Send EOI (End of Interrupt) to PIC
     * For now we use the legacy PIC; later we'll switch to APIC */
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        outb(0xA0, 0x20);
    }
    /* Send EOI to master PIC */
    outb(0x20, 0x20);
}

/*
 * Remap the legacy PIC
 * Remaps IRQ 0-7 to INT 32-39 and IRQ 8-15 to INT 40-47
 */
static void pic_remap(void) {
    /* Start initialization sequence (ICW1) */
    outb(0x20, 0x11);  /* Master PIC */
    outb(0xA0, 0x11);  /* Slave PIC */
    io_wait();

    /* Set vector offsets (ICW2) */
    outb(0x21, IRQ_BASE);      /* Master: IRQ 0-7 -> INT 32-39 */
    outb(0xA1, IRQ_BASE + 8);  /* Slave: IRQ 8-15 -> INT 40-47 */
    io_wait();

    /* Tell Master about Slave at IRQ2 (ICW3) */
    outb(0x21, 0x04);  /* Slave on IRQ2 */
    outb(0xA1, 0x02);  /* Slave cascade identity */
    io_wait();

    /* Set 8086 mode (ICW4) */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();

    /* Restore masks (mask all IRQs for now) */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    TRACE("PIC remapped: IRQs at INT %d-%d", IRQ_BASE, IRQ_BASE + 15);
}

/*
 * Initialize the IDT
 */
void idt_init(void) {
    INFO("Initializing IDT...");

    /* Clear all handlers */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = NULL;
    }

    /* Set up IST stacks in TSS */
    tss_set_ist(IST_DOUBLE_FAULT, (uint64_t)ist1_stack + IST_STACK_SIZE);
    tss_set_ist(IST_NMI, (uint64_t)ist2_stack + IST_STACK_SIZE);
    tss_set_ist(IST_MCE, (uint64_t)ist3_stack + IST_STACK_SIZE);

    /* Set up exception handlers (0-31) */
    idt_set_gate(0,  (uint64_t)isr0,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(1,  (uint64_t)isr1,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(2,  (uint64_t)isr2,  GDT_KERNEL_CODE, IST_NMI,  IDT_INTERRUPT_GATE);
    idt_set_gate(3,  (uint64_t)isr3,  GDT_KERNEL_CODE, IST_NONE, IDT_TRAP_GATE);  /* Breakpoint */
    idt_set_gate(4,  (uint64_t)isr4,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(5,  (uint64_t)isr5,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(6,  (uint64_t)isr6,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(7,  (uint64_t)isr7,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(8,  (uint64_t)isr8,  GDT_KERNEL_CODE, IST_DOUBLE_FAULT, IDT_INTERRUPT_GATE);
    idt_set_gate(9,  (uint64_t)isr9,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, IST_MCE,  IDT_INTERRUPT_GATE);
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(22, (uint64_t)isr22, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(23, (uint64_t)isr23, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(24, (uint64_t)isr24, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(25, (uint64_t)isr25, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(26, (uint64_t)isr26, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(27, (uint64_t)isr27, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(28, (uint64_t)isr28, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(29, (uint64_t)isr29, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(30, (uint64_t)isr30, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(31, (uint64_t)isr31, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);

    /* Remap PIC before setting up IRQ handlers */
    pic_remap();

    /* Set up IRQ handlers (32-47) */
    idt_set_gate(32, (uint64_t)irq0,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (uint64_t)irq1,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(34, (uint64_t)irq2,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (uint64_t)irq3,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (uint64_t)irq4,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (uint64_t)irq5,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (uint64_t)irq6,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (uint64_t)irq7,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (uint64_t)irq8,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (uint64_t)irq9,  GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, IST_NONE, IDT_INTERRUPT_GATE);

    /* Set up IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    /* Load IDT */
    idt_flush(&idt_ptr);

    INFO("IDT loaded: %d entries at 0x%llx", IDT_ENTRIES, (uint64_t)&idt);
    DEBUG("  Exceptions: INT 0-31");
    DEBUG("  IRQs: INT %d-%d", IRQ_BASE, IRQ_BASE + 15);
    DEBUG("  IST stacks configured for DF, NMI, MCE");
}
