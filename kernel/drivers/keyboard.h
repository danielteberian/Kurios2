/* keyboard.h - PS/2 Keyboard Driver */
#ifndef _DRIVERS_KEYBOARD_H
#define _DRIVERS_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize keyboard driver (registers IRQ1 handler) */
void keyboard_init(void);

/* Check if a key is available in the buffer */
bool keyboard_has_key(void);

/* Get next key from buffer (blocking) */
char keyboard_getchar(void);

/* Get next key from buffer (non-blocking, returns 0 if empty) */
char keyboard_getchar_nonblock(void);

/* Get raw scancode (non-blocking, returns 0 if empty) */
uint8_t keyboard_get_scancode(void);

/* Key state queries */
bool keyboard_shift_pressed(void);
bool keyboard_ctrl_pressed(void);
bool keyboard_alt_pressed(void);
bool keyboard_caps_lock(void);

#endif /* _DRIVERS_KEYBOARD_H */
