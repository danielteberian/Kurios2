/* cpu.h - x86_64 CPU operations and registers */
#ifndef _ARCH_CPU_H
#define _ARCH_CPU_H

#include <stdint.h>
#include <stdbool.h>

/* CPU register state (for exception handlers, etc.) */
typedef struct {
    /* Pushed by our code */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Interrupt number and error code */
    uint64_t int_no, error_code;

    /* Pushed by CPU automatically */
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_state_t;

/* Control register operations */
static inline uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(uint64_t value) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(value));
}

static inline uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value));
}

static inline uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static inline void write_cr4(uint64_t value) {
    __asm__ volatile("mov %0, %%cr4" : : "r"(value));
}

/* Flags register */
static inline uint64_t read_rflags(void) {
    uint64_t value;
    __asm__ volatile("pushfq; popq %0" : "=r"(value));
    return value;
}

/* MSR (Model Specific Register) operations */
static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

/* Common MSRs */
#define MSR_EFER            0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR            0xC0000081  /* SYSCALL/SYSRET segments */
#define MSR_LSTAR           0xC0000082  /* SYSCALL RIP (64-bit) */
#define MSR_CSTAR           0xC0000083  /* SYSCALL RIP (compat mode) */
#define MSR_SFMASK          0xC0000084  /* SYSCALL RFLAGS mask */
#define MSR_FS_BASE         0xC0000100  /* FS base address */
#define MSR_GS_BASE         0xC0000101  /* GS base address */
#define MSR_KERNEL_GS_BASE  0xC0000102  /* Kernel GS base (swapped on SWAPGS) */
#define MSR_APIC_BASE       0x0000001B  /* Local APIC base address */

/* EFER bits */
#define EFER_SCE            (1 << 0)    /* SYSCALL Enable */
#define EFER_LME            (1 << 8)    /* Long Mode Enable */
#define EFER_LMA            (1 << 10)   /* Long Mode Active */
#define EFER_NXE            (1 << 11)   /* No-Execute Enable */

/* Interrupt control */
static inline void cli(void) {
    __asm__ volatile("cli");
}

static inline void sti(void) {
    __asm__ volatile("sti");
}

static inline bool interrupts_enabled(void) {
    return (read_rflags() & 0x200) != 0;
}

/* Halt until interrupt */
static inline void hlt(void) {
    __asm__ volatile("hlt");
}

/* Halt forever */
static inline void halt_forever(void) {
    while (1) {
        cli();
        hlt();
    }
}

/* Pause instruction (for spinloops) */
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

/* CPUID */
static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

static inline void cpuid_ext(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

/* TLB flush */
static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void flush_tlb(void) {
    write_cr3(read_cr3());
}

/* Read timestamp counter */
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/* Read timestamp counter with serialization */
static inline uint64_t rdtscp(uint32_t *aux) {
    uint32_t low, high, aux_val;
    __asm__ volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(aux_val));
    if (aux) *aux = aux_val;
    return ((uint64_t)high << 32) | low;
}

/* Get current stack pointer */
static inline uint64_t read_rsp(void) {
    uint64_t value;
    __asm__ volatile("mov %%rsp, %0" : "=r"(value));
    return value;
}

/* Get current instruction pointer (via call/pop trick) */
static inline uint64_t read_rip(void) {
    uint64_t value;
    __asm__ volatile("lea (%%rip), %0" : "=r"(value));
    return value;
}

#endif /* _ARCH_CPU_H */
