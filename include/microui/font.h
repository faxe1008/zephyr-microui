#ifndef MICROUI_FONT_H
#define MICROUI_FONT_H

struct Font {
	uint32_t height;
	uint32_t bitmap_width;
	uint32_t bytes_per_row;
	uint32_t first_char;
	uint32_t last_char;
	uint32_t default_width;
	uint32_t char_spacing;
	struct FontGlyph *glyphs;
};

struct FontGlyph {
	uint8_t width;
	uint8_t height;
	const uint8_t *bitmap;
};

#endif // MICROUI_FONT_H
