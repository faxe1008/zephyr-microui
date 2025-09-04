# MicroUI for Zephyr

A lightweight immediate-mode GUI library for Zephyr applications.

## Overview

This module provides the MicroUI library for Zephyr RTOS applications. MicroUI is a tiny immediate-mode UI library designed for resource-constrained embedded systems.

## Features

- Lightweight immediate-mode GUI
- Minimal memory footprint, no dynamic memory allocation
- Simple API for creating user interfaces
- Integration with Zephyr's display subsystem
- Support for basic UI elements (buttons, text, windows, etc.)

## Getting Started

### Prerequisites

- Zephyr development environment
- Display device supported by Zephyr

### Example Usage

```c
#include <microui/event_loop.h>
#include <microui/microui.h>
#include <microui/font.h>

// Your application font, see scripts/microui_font_gen.py to generate new ones
extern const struct Font your_font;

// Frame callback function
void process_frame(mu_Context *ctx) 
{
    // Begin the frame
    mu_begin(ctx);

    // Create a window
    if (mu_begin_window(ctx, "Hello Window", mu_rect(40, 40, 300, 200))) {
        // Add a label
        mu_label(ctx, "Hello, MicroUI on Zephyr!");

        // Add a button
        if (mu_button(ctx, "Click Me")) {
            // Handle button click
            printk("Button clicked!\n");
        }

        mu_end_window(ctx);
    }

    // End the frame
    mu_end(ctx);
}

int main(void)
{
    // Initialize the MicroUI event loop
    mu_event_loop_init(process_frame);

    // Get the MicroUI context and set the font
    mu_Context *ctx = mu_get_context();
    mu_set_font(ctx, &your_font);

    // Start the event loop
    mu_event_loop_start();

    return 0;
}
```

## Documentation

For detailed API documentation and examples, refer to the original MicroUI documentation.

## License

This module follows the same licensing as the original MicroUI library.
