/* mouse.h - PS/2 Mouse Driver */
#ifndef _DRIVERS_MOUSE_H
#define _DRIVERS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

/* Mouse button masks */
#define MOUSE_LEFT_BTN      0x01
#define MOUSE_RIGHT_BTN     0x02
#define MOUSE_MIDDLE_BTN    0x04

/* Mouse event structure */
typedef struct {
    int16_t delta_x;        /* X movement (positive = right) */
    int16_t delta_y;        /* Y movement (positive = down) */
    uint8_t buttons;        /* Button state (left, right, middle) */
} mouse_event_t;

/*
 * Initialize the PS/2 mouse driver
 * Enables the auxiliary device and IRQ12
 */
void mouse_init(void);

/*
 * Check if a mouse event is available
 * @return true if event available
 */
bool mouse_has_event(void);

/*
 * Get the next mouse event (non-blocking)
 * @param event  Output event structure
 * @return true if event was available, false otherwise
 */
bool mouse_get_event(mouse_event_t *event);

/*
 * Get current button state
 * @return Button mask (MOUSE_LEFT_BTN, MOUSE_RIGHT_BTN, MOUSE_MIDDLE_BTN)
 */
uint8_t mouse_get_buttons(void);

/*
 * IRQ12 handler (called from interrupt context)
 */
void mouse_irq_handler(void);

#endif /* _DRIVERS_MOUSE_H */
