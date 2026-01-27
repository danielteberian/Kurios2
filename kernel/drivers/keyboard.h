/* keyboard.h - PS/2 Keyboard Driver */
#ifndef _DRIVERS_KEYBOARD_H
#define _DRIVERS_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Special key codes (returned by keyboard_getchar) */
#define KEY_UP            0x80
#define KEY_DOWN          0x81
#define KEY_LEFT          0x82
#define KEY_RIGHT         0x83
#define KEY_DELETE        0x7F
#define KEY_HOME          0x84
#define KEY_END           0x85
#define KEY_PGUP          0x86
#define KEY_PGDN          0x87
#define KEY_INSERT        0x88

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
