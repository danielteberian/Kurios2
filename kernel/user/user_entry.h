/* user_entry.h - User Mode Entry */
#ifndef _USER_ENTRY_H
#define _USER_ENTRY_H

#include <stdint.h>
#include "../mm/as.h"

/*
 * User mode entry point structure
 * Contains all information needed to enter user mode
 */
typedef struct {
    uint64_t rip;           /* User code entry point */
    uint64_t rsp;           /* User stack pointer */
    uint64_t rflags;        /* Initial RFLAGS */
    address_space_t *as;    /* Address space to use */
    uint64_t kernel_stack;  /* Kernel stack top (for TSS.RSP0) */
} user_entry_t;

/*
 * Default RFLAGS for user mode:
 * - IF (bit 9): Enable interrupts
 * - Reserved bit 1 always set
 */
#define USER_RFLAGS_DEFAULT     (0x202)

/*
 * Enter user mode via IRETQ
 * This function does not return (jumps to user space)
 *
 * @param entry User entry point structure
 */
void user_enter(user_entry_t *entry) __attribute__((noreturn));

/*
 * Assembly routine to perform the actual IRETQ
 * Called by user_enter after setting up TSS
 *
 * @param rip    User RIP
 * @param cs     User code segment selector
 * @param rflags User RFLAGS
 * @param rsp    User RSP
 * @param ss     User data segment selector
 */
extern void ring3_enter(uint64_t rip, uint64_t cs, uint64_t rflags,
                        uint64_t rsp, uint64_t ss) __attribute__((noreturn));

/*
 * Create a simple test user program
 * Allocates pages and sets up a minimal user program that makes a syscall
 *
 * @param as    Address space to use
 * @param entry Output: filled with entry point info
 * @return 0 on success, -1 on failure
 */
int user_create_test_program(address_space_t *as, user_entry_t *entry);

#ifdef DEBUG_TESTS
/*
 * Test user mode entry
 * Creates a test program and enters user mode
 */
void user_entry_run_tests(void);
#endif

#endif /* _USER_ENTRY_H */
