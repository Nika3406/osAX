// src/include/keyboard.h
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../core/types.h"

// Initialize keyboard driver
void keyboard_init(void);

// Check if key available
int keyboard_available(void);

// Get next scancode (blocking)
uint8_t keyboard_getkey(void);

// Get ASCII character (blocking)
char keyboard_getchar(void);

// Read line with editing (bash-like)
int keyboard_readline(char* buffer, int max_len);

// Modifier key states
int keyboard_shift_pressed(void);
int keyboard_ctrl_pressed(void);
int keyboard_alt_pressed(void);
int keyboard_caps_lock(void);

// Keyboard interrupt handler (called from IRQ1)
void keyboard_handler(void);

#endif // KEYBOARD_H
