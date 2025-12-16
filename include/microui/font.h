/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_MODULES_MICROUI_FONT_H_
#define ZEPHYR_MODULES_MICROUI_FONT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <microui/microui.h>
#include <stdint.h>

struct mu_FontGlyph {
	uint32_t codepoint;
	uint8_t width;
	uint8_t height;
	const uint8_t *bitmap;
};

struct mu_FontDescriptor {
	uint32_t height;
	uint32_t bitmap_width;
	uint32_t bytes_per_row;
	uint32_t default_width;
	uint32_t char_spacing;
	uint32_t glyph_count;
	const struct mu_FontGlyph *glyphs;
};

static inline void mu_set_font(mu_Context *ctx, const struct mu_FontDescriptor *font)
{
	if (ctx) {
		ctx->_style.font = (void *)font;
	}
}

#define MU_FONT_DECLARE(font_name) extern const struct mu_FontDescriptor font_name;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_FONT_H_ */
