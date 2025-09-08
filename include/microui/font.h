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

#include <stdint.h>

struct FontGlyph {
	uint32_t codepoint;
	uint8_t width;
	uint8_t height;
	const uint8_t *bitmap;
};

struct Font {
	uint32_t height;
	uint32_t bitmap_width;
	uint32_t bytes_per_row;
	uint32_t default_width;
	uint32_t char_spacing;
	uint32_t glyph_count;
	const struct FontGlyph *glyphs;
};

static inline void mu_set_font(mu_Context *ctx, const struct Font *font)
{
	if (ctx) {
		ctx->_style.font = (void *)font;
	}
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_FONT_H_ */
