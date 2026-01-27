// src/kernel/drivers/keyboard.c - PS/2 Keyboard Driver - x86_64 VERSION
#include "keyboard.h"
#include "io.h"
#include "idt.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_COMMAND_PORT 0x64

// Keyboard state
static uint8_t keyboard_buffer[256];
static volatile uint8_t kb_read_pos = 0;
static volatile uint8_t kb_write_pos = 0;
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;
static uint8_t caps_lock = 0;

// Scancode to ASCII mapping (US keyboard layout)
static const char scancode_to_ascii[] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_to_ascii_shift[] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

// Special key codes
#define KEY_LEFT_ARROW  0x4B
#define KEY_RIGHT_ARROW 0x4D
#define KEY_UP_ARROW    0x48
#define KEY_DOWN_ARROW  0x50
#define KEY_HOME        0x47
#define KEY_END         0x4F
#define KEY_DELETE      0x53

// Keyboard interrupt handler
void keyboard_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Check if key release (high bit set)
    if (scancode & 0x80) {
        scancode &= 0x7F;
        
        // Handle modifier key releases
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 0;
        } else if (scancode == 0x1D) {
            ctrl_pressed = 0;
        } else if (scancode == 0x38) {
            alt_pressed = 0;
        }
        return;
    }
    
    // Handle modifier key presses
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    } else if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    } else if (scancode == 0x38) {
        alt_pressed = 1;
        return;
    } else if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }
    
    // Handle special keys (extended scancodes)
    if (scancode == 0xE0) {
        // Next byte is extended scancode
        return;
    }
    
    // Add to buffer
    uint8_t next_pos = (kb_write_pos + 1) % 256;
    if (next_pos != kb_read_pos) {
        keyboard_buffer[kb_write_pos] = scancode;
        kb_write_pos = next_pos;
    }
}

// Initialize keyboard
void keyboard_init(void) {
    // Install keyboard IRQ handler (IRQ1 = interrupt 33)
    // The IRQ handler is defined in irq_asm.asm
    extern void irq1_handler(void);
    
    // CHANGED: Cast to uint64_t for 64-bit
    // IDT_GATE_INTERRUPT is 0x8E, IDT_FLAG_DPL0 is 0x00
    idt_set_gate(33, (uint64_t)irq1_handler, 0x08, 0x8E);
    
    // Enable keyboard IRQ (unmask IRQ1 on PIC)
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 1);  // Clear bit 1 (IRQ1)
    outb(0x21, mask);
    
    // Clear keyboard buffer
    kb_read_pos = 0;
    kb_write_pos = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
}

// Check if key is available
int keyboard_available(void) {
    return kb_read_pos != kb_write_pos;
}

// Get next key (blocking)
uint8_t keyboard_getkey(void) {
    while (!keyboard_available()) {
        __asm__ volatile("hlt");  // Wait for interrupt
    }
    
    uint8_t scancode = keyboard_buffer[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % 256;
    return scancode;
}

// Get ASCII character (blocking)
char keyboard_getchar(void) {
    uint8_t scancode = keyboard_getkey();
    
    // Check for special keys
    if (scancode >= sizeof(scancode_to_ascii)) {
        return 0;  // Unknown key
    }
    
    // Convert to ASCII
    char c;
    if (shift_pressed || caps_lock) {
        c = scancode_to_ascii_shift[scancode];
        if (caps_lock && c >= 'A' && c <= 'Z' && !shift_pressed) {
            c = scancode_to_ascii[scancode];  // Caps only affects letters
        }
    } else {
        c = scancode_to_ascii[scancode];
    }
    
    return c;
}

// Read line with editing support
int keyboard_readline(char* buffer, int max_len) {
    int pos = 0;
    buffer[0] = '\0';
    
    // Disable interrupts during keyboard reading to avoid conflicts
    __asm__ volatile("cli");
    
    while (1) {
        // Poll keyboard status port to see if data is available
        while (!(inb(0x64) & 0x01)) {
            __asm__ volatile("pause");  // Hint to CPU we're spinning
        }
        
        // Read scancode directly from port
        uint8_t scancode = inb(0x60);
        
        // Handle special keys
        if (scancode == 0x0E) {  // Backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                extern void terminal_putchar(char);
                terminal_putchar('\b');
            }
            continue;
        }
        
        if (scancode == 0x1C) {  // Enter
            buffer[pos] = '\0';
            extern void terminal_putchar(char);
            terminal_putchar('\n');
            
            // Re-enable interrupts before returning
            __asm__ volatile("sti");
            return pos;
        }
        
        // Check if it's a key release (high bit set)
        if (scancode & 0x80) {
            scancode &= 0x7F;
            // Handle modifier releases
            if (scancode == 0x2A || scancode == 0x36) {
                shift_pressed = 0;
            } else if (scancode == 0x1D) {
                ctrl_pressed = 0;
            }
            continue;
        }
        
        // Handle modifier presses
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        } else if (scancode == 0x1D) {
            ctrl_pressed = 1;
            continue;
        } else if (scancode == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }
        
        // Convert to ASCII
        char c = 0;
        if (scancode < sizeof(scancode_to_ascii)) {
            if (shift_pressed || caps_lock) {
                c = scancode_to_ascii_shift[scancode];
                if (caps_lock && c >= 'A' && c <= 'Z' && !shift_pressed) {
                    c = scancode_to_ascii[scancode];
                }
            } else {
                c = scancode_to_ascii[scancode];
            }
        }
        
        // Add to buffer if valid and space available
        if (c != 0 && pos < max_len - 1) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
            
            // Echo to terminal
            extern void terminal_putchar(char);
            terminal_putchar(c);
        }
    }
}

// Get modifier states
int keyboard_shift_pressed(void) { return shift_pressed; }
int keyboard_ctrl_pressed(void) { return ctrl_pressed; }
int keyboard_alt_pressed(void) { return alt_pressed; }
int keyboard_caps_lock(void) { return caps_lock; }
