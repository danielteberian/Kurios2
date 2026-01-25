/* stack_protector.h - Stack smashing protector support */
#ifndef _KERNEL_STACK_PROTECTOR_H
#define _KERNEL_STACK_PROTECTOR_H

#include <stdint.h>

/* The stack canary value (do not access directly) */
extern uintptr_t __stack_chk_guard;

/* Initialize stack protector with TSC-based randomness */
void stack_protector_init(void);

#endif /* _KERNEL_STACK_PROTECTOR_H */
