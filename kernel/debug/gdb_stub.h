/* gdb_stub.h - GDB Remote Serial Protocol stub for kernel debugging */
#ifndef _KERNEL_GDB_STUB_H
#define _KERNEL_GDB_STUB_H

#include <stdint.h>
#include <stdbool.h>
#include "../arch/x86_64/cpu.h"

/* GDB communication port (COM2) */
#define GDB_SERIAL_PORT     0x2F8

/* Signal numbers (Unix-style, for GDB) */
#define GDB_SIGNAL_INT      2   /* SIGINT - interrupt */
#define GDB_SIGNAL_TRAP     5   /* SIGTRAP - breakpoint/trace trap */
#define GDB_SIGNAL_SEGV     11  /* SIGSEGV - segmentation fault */

/* Maximum breakpoints supported */
#define GDB_MAX_BREAKPOINTS 32

/* RFLAGS bits */
#define RFLAGS_TF           (1 << 8)    /* Trap Flag for single-stepping */
#define RFLAGS_IF           (1 << 9)    /* Interrupt Flag */

/* Initialize the GDB stub - registers exception handlers */
void gdb_init(void);

/* Enter the debugger manually (equivalent to __builtin_trap) */
void gdb_breakpoint(void);

/* Check if GDB stub is active/connected */
bool gdb_is_active(void);

#endif /* _KERNEL_GDB_STUB_H */
