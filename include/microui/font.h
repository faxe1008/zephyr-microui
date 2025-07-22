#ifndef ZEPHYR_MODULES_MICROUI_FONT_H_
#define ZEPHYR_MODULES_MICROUI_FONT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Font {
	uint32_t height;
	uint32_t bitmap_width;
	uint32_t bytes_per_row;
	uint32_t first_char;
	uint32_t last_char;
	uint32_t default_width;
	uint32_t char_spacing;
	const struct FontGlyph *glyphs;
};

struct FontGlyph {
	uint8_t width;
	uint8_t height;
	const uint8_t *bitmap;
};

static inline void mu_set_font(mu_Context *ctx, struct Font* font)
{
    if (ctx) {
        ctx->_style.font = (void*)font;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_FONT_H_ */
