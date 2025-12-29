# MicroUI for Zephyr

A lightweight immediate-mode GUI library for Zephyr applications.

## Overview

This module provides the MicroUI library for Zephyr RTOS applications. MicroUI is a tiny immediate-mode UI library designed for resource-constrained embedded systems. This port is based on [rxi/microui](https://github.com/rxi/microui) and extends it with Zephyr-specific integrations and additional features.

## Features

- Lightweight immediate-mode GUI
- Minimal memory footprint, no dynamic memory allocation
- Simple API for creating user interfaces
- Integration with Zephyr's display subsystem
- Support for basic UI elements (buttons, text, windows, etc.)

## Extensions over Base MicroUI

This Zephyr port adds several features not found in the original MicroUI library:

### Zephyr Integration (`zmu.h`)
- **Event loop**: Built-in workqueue-based event loop (`mu_event_loop_start()`, `mu_event_loop_stop()`) that handles frame timing
- **Input handling**: Automatic integration with Zephyr's input subsystem for touch/pointer devices
- **Display rendering**: Direct integration with Zephyr's display driver subsystem
- **Lazy redraw**: Only redraws when UI state changes, reducing power consumption

### Drawing Extensions (`CONFIG_MICROUI_DRAW_EXTENSIONS`)
When enabled, provides additional drawing primitives:
- `mu_draw_circle()` - Filled circles
- `mu_draw_arc()` - Arc segments with configurable thickness and angles
- `mu_draw_line()` - Lines with configurable thickness
- `mu_draw_triangle()` - Filled triangles
- `mu_draw_image()` - Image rendering support

### Animation Support
Built-in animation framework for UI elements:
- Smooth transitions and animations for UI state changes
- Configurable animation timing and easing functions
- Integration with the event loop for frame-based updates

### Flex Layout System
Proportional/weighted layout system using `MU_FLEX()` macro:
```c
// Equal distribution (1:1:1)
mu_layout_row(ctx, 3, (int[]){MU_FLEX(1), MU_FLEX(1), MU_FLEX(1)}, 0);

// Weighted distribution (1:2:1)
mu_layout_row(ctx, 3, (int[]){MU_FLEX(1), MU_FLEX(2), MU_FLEX(1)}, 0);

// Mixed: fixed 60px + flex
mu_layout_row(ctx, 3, (int[]){60, MU_FLEX(1), MU_FLEX(1)}, 0);
```

### Pixel Format Support
Supports multiple display pixel formats:
- RGB 888 (24-bit)
- ARGB 8888 (32-bit with alpha)
- RGB 565 / BGR 565 (16-bit)
- L8 (8-bit grayscale)
- AL 88 (8-bit alpha + luminance)
- Monochrome (1-bit)

### Alpha Blending
Optional alpha blending support for formats with alpha channel (ARGB 8888, AL 88).

### Text Extensions
- **UTF-8 support**: Full UTF-8 text rendering (`CONFIG_MICROUI_TEXT_UTF8`)
- **Text width cache**: Caches text width calculations for improved performance

### Font & Image Generation Scripts
Python scripts for asset generation:
- `scripts/microui_font_gen.py` - Generate bitmap fonts from TTF files
- `scripts/microui_image_gen.py` - Convert images to C arrays for embedding

### Additional Text Alignment Options
Extended alignment options for controls:
- `MU_OPT_ALIGNTOP`
- `MU_OPT_ALIGNBOTTOM`

### Configurable Memory Pools
All internal buffer sizes are configurable via Kconfig:
- Command list size
- Container/layout stack sizes
- Pool sizes for containers and tree nodes

## Getting Started

### Prerequisites

- Zephyr development environment
- Display device supported by Zephyr

### Example Usage

```c
#include <microui/zmu.h>
#include <microui/microui.h>
#include <microui/font.h>

// Your application font, see scripts/microui_font_gen.py to generate new ones
MU_FONT_DECLARE(your_font);

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
    mu_setup(process_frame);

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
