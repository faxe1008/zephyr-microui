/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <microui/zmu.h>
#include <microui/font.h>
#include <microui/image.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(microui_zmu, LOG_LEVEL_INF);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923f
#endif

#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962f
#endif

#define DISPLAY_NODE            DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH           DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT          DT_PROP(DISPLAY_NODE, height)
#define DISPLAY_BYTES_PER_PIXEL (CONFIG_MICROUI_BITS_PER_PIXEL / 8)
#define DISPLAY_STRIDE          (ROUND_UP(DISPLAY_WIDTH, 8) * DISPLAY_BYTES_PER_PIXEL)

#define DISPLAY_BUFFER_SIZE                                                                        \
	(CONFIG_MICROUI_BITS_PER_PIXEL * ROUND_UP(DISPLAY_WIDTH, 8) * DISPLAY_HEIGHT) / 8

static const struct device *display_dev = DEVICE_DT_GET(DISPLAY_NODE);
static uint8_t display_buffer[DISPLAY_BUFFER_SIZE] __aligned(4);
static struct display_capabilities display_caps;

/* Microui Context */
static mu_Context mu_ctx;
static mu_Color bg_color = {90, 95, 100, 255};

/* Clipping rectangle */
static mu_Rect clip_rect = {0, 0, 0, 0};

/* Text width cache */
#ifdef CONFIG_MICROUI_TEXT_WIDTH_CACHE
struct text_width_cache_entry {
	const void *font;
	uint32_t hash;
	int len;
	int width;
};

static struct text_width_cache_entry text_width_cache[CONFIG_MICROUI_TEXT_WIDTH_CACHE_SIZE];

static inline uint32_t fnv1a_hash(const char *text, int len)
{
	uint32_t hash = 2166136261u;

	for (int i = 0; i < len && text[i]; i++) {
		hash ^= (uint8_t)text[i];
		hash *= 16777619u;
	}
	return hash;
}
#endif /* CONFIG_MICROUI_TEXT_WIDTH_CACHE */

/* Work queue and work queue thread */
#ifdef CONFIG_MICROUI_EVENT_LOOP
static struct k_work_q mu_work_queue;
static K_KERNEL_STACK_DEFINE(mu_work_stack, CONFIG_MICROUI_EVENT_LOOP_STACK_SIZE);
#endif /* CONFIG_MICROUI_EVENT_LOOP */
static volatile mu_process_frame_cb frame_cb;

static __always_inline const struct mu_FontGlyph *find_glyph(const struct mu_FontDescriptor *font,
							     uint32_t codepoint)
{
	const struct mu_FontGlyph *glyphs = font->glyphs;
	uint32_t len = font->glyph_count;

	if (unlikely(len == 0)) {
		return NULL;
	}

	/* Branchless binary search */
	const struct mu_FontGlyph *base = glyphs;

	while (len > 1) {
		uint32_t half = len / 2;
		base = (base[half].codepoint <= codepoint) ? &base[half] : base;
		len -= half;
	}

	return (base->codepoint == codepoint) ? base : NULL;
}

static __always_inline int next_utf8_codepoint(const char *str, uint32_t *codepoint)
{
#ifdef CONFIG_MICROUI_TEXT_UTF8
	const uint8_t *bytes = (const uint8_t *)str;

	if (!bytes[0]) {
		*codepoint = 0;
		return 0;
	}

	if (likely(bytes[0] < 0x80)) {
		// ASCII character (0xxxxxxx)
		*codepoint = bytes[0];
		return 1;
	} else if ((bytes[0] & 0xE0) == 0xC0) {
		// 2-byte sequence (110xxxxx 10xxxxxx)
		if (!bytes[1] || (bytes[1] & 0xC0) != 0x80) {
			*codepoint = 0xFFFD; // replacement character
			return 1;
		}
		*codepoint = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
		return 2;
	} else if ((bytes[0] & 0xF0) == 0xE0) {
		// 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
		if (!bytes[1] || !bytes[2] || (bytes[1] & 0xC0) != 0x80 ||
		    (bytes[2] & 0xC0) != 0x80) {
			*codepoint = 0xFFFD; // replacement character
			return 1;
		}
		*codepoint =
			((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
		return 3;
	} else if ((bytes[0] & 0xF8) == 0xF0) {
		// 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
		if (!bytes[1] || !bytes[2] || !bytes[3] || (bytes[1] & 0xC0) != 0x80 ||
		    (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80) {
			*codepoint = 0xFFFD; // replacement character
			return 1;
		}
		*codepoint = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) |
			     ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
		return 4;
	} else {
		// Invalid UTF-8 start byte
		*codepoint = 0xFFFD; // replacement character
		return 1;
	}
#else
	// ASCII fallback
	*codepoint = (uint8_t)*str;
	return *str ? 1 : 0;
#endif
}

static __always_inline uint8_t luminance(mu_Color color)
{
	return (299 * color.r + 587 * color.g + 114 * color.b) / 1000;
}

static __always_inline mu_Rect intersect_rects(mu_Rect r1, mu_Rect r2)
{
	int x1 = mu_max(r1.x, r2.x);
	int y1 = mu_max(r1.y, r2.y);
	int x2 = mu_min(r1.x + r1.w, r2.x + r2.w);
	int y2 = mu_min(r1.y + r1.h, r2.y + r2.h);

	if (x2 < x1) {
		x2 = x1;
	}
	if (y2 < y1) {
		y2 = y1;
	}
	return mu_rect(x1, y1, x2 - x1, y2 - y1);
}

static __always_inline int arc_atan_ratio(int x, int y)
{
#ifdef CONFIG_MICROUI_ARC_ATAN_APPROXIMATION
	/* Fast integer approximation of atan(x/y) scaled to 0-32 range */
	int val = x * 255 / y; /* 0 - 255 */
	return val * (770195 - (val - 255) * (val + 941)) / 6137491; /* 0 - 32 */
#else
	/* Use math library atan2f for accurate calculation */
	return (int)((M_PI_2 - atan2f((float)y, (float)x)) * 32.0f / M_PI_4);
#endif
}

#define ENABLED_CF_COUNT                                                                           \
	IS_ENABLED(CONFIG_MICROUI_RENDER_RGB_888) + IS_ENABLED(CONFIG_MICROUI_RENDER_ARGB_8888) +  \
		IS_ENABLED(CONFIG_MICROUI_RENDER_RGB_565) +                                        \
		IS_ENABLED(CONFIG_MICROUI_RENDER_BGR_565) +                                        \
		IS_ENABLED(CONFIG_MICROUI_RENDER_MONO) + IS_ENABLED(CONFIG_MICROUI_RENDER_L_8) +   \
		IS_ENABLED(CONFIG_MICROUI_RENDER_AL_88)

#ifdef CONFIG_MICROUI_RENDER_RGB_888
static __always_inline uint32_t color_to_pixel_rgb888(mu_Color color)
{
	return ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

static __always_inline void set_pixel_rgb888(int x, int y, uint32_t pixel)
{
	int index = (y * DISPLAY_WIDTH + x) * 3;
	display_buffer[index + 0] = (pixel >> 16) & 0xFF; // R
	display_buffer[index + 1] = (pixel >> 8) & 0xFF;  // G
	display_buffer[index + 2] = pixel & 0xFF;         // B
}
#endif

#ifdef CONFIG_MICROUI_RENDER_ARGB_8888
static __always_inline uint32_t color_to_pixel_argb8888(mu_Color color)
{
	return ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) |
	       (uint32_t)color.b;
}

static __always_inline void set_pixel_argb8888(int x, int y, uint32_t pixel)
{
	int index = (y * DISPLAY_WIDTH + x) * 4;
	uint32_t *p = (uint32_t *)(display_buffer + index);
#ifdef CONFIG_MICROUI_ALPHA_BLENDING
	uint8_t src_a = (pixel >> 24) & 0xFF;

	if (src_a == 255) {
		*p = pixel;
	} else if (src_a > 0) {
		uint32_t dst = *p;
		uint8_t dst_a = (dst >> 24) & 0xFF;
		uint8_t dst_r = (dst >> 16) & 0xFF;
		uint8_t dst_g = (dst >> 8) & 0xFF;
		uint8_t dst_b = dst & 0xFF;

		uint8_t src_r = (pixel >> 16) & 0xFF;
		uint8_t src_g = (pixel >> 8) & 0xFF;
		uint8_t src_b = pixel & 0xFF;

		uint16_t inv_a = 255 - src_a;
		uint16_t out_a = src_a + (dst_a * inv_a) / 255;

		if (out_a > 0) {
			uint8_t out_r = (src_r * src_a + dst_r * dst_a * inv_a / 255) / out_a;
			uint8_t out_g = (src_g * src_a + dst_g * dst_a * inv_a / 255) / out_a;
			uint8_t out_b = (src_b * src_a + dst_b * dst_a * inv_a / 255) / out_a;
			*p = ((uint32_t)out_a << 24) | ((uint32_t)out_r << 16) |
			     ((uint32_t)out_g << 8) | out_b;
		}
	}
#else
	*p = pixel;
#endif
}
#endif

#ifdef CONFIG_MICROUI_RENDER_RGB_565
static __always_inline uint32_t color_to_pixel_rgb565(mu_Color color)
{
	uint16_t rgb565 = ((uint32_t)(color.r & 0xF8) << 8) | ((uint32_t)(color.g & 0xFC) << 3) |
			  (uint32_t)(color.b >> 3);
	return sys_cpu_to_be16(rgb565);
}

static __always_inline void set_pixel_rgb565(int x, int y, uint32_t pixel)
{
	int index = (y * DISPLAY_WIDTH + x) * 2;
	uint16_t *p = (uint16_t *)(display_buffer + index);
	*p = (uint16_t)pixel;
}
#endif

#ifdef CONFIG_MICROUI_RENDER_BGR_565
static __always_inline uint32_t color_to_pixel_bgr565(mu_Color color)
{
	return ((uint32_t)(color.b & 0xF8) << 8) | ((uint32_t)(color.g & 0xFC) << 3) |
	       (uint32_t)(color.r >> 3);
}

static __always_inline void set_pixel_bgr565(int x, int y, uint32_t pixel)
{
	int index = (y * DISPLAY_WIDTH + x) * 2;
	uint16_t *p = (uint16_t *)(display_buffer + index);
	*p = (uint16_t)pixel;
}
#endif

#if IS_ENABLED(CONFIG_MICROUI_RENDER_MONO)
static __always_inline uint32_t color_to_pixel_mono(mu_Color color)
{
	uint8_t luma = luminance(color);
	return (luma > 127) ? 0xFF : 0;
}

static __always_inline void set_pixel_mono(int x, int y, uint32_t pixel)
{
	uint8_t *buf;
	uint8_t bit;

	if (display_caps.screen_info & SCREEN_INFO_MONO_VTILED) {
		buf = display_buffer + x + (y >> 3) * DISPLAY_WIDTH;
		bit = (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) ? (7 - (y & 7))
									      : (y & 7);
	} else {
		buf = display_buffer + (x >> 3) + y * (DISPLAY_WIDTH >> 3);
		bit = (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) ? (7 - (x & 7))
									      : (x & 7);
	}

	if (pixel) {
		*buf |= BIT(bit);
	} else {
		*buf &= ~BIT(bit);
	}
}
#endif

#if IS_ENABLED(CONFIG_MICROUI_RENDER_L_8)
static __always_inline uint32_t color_to_pixel_l8(mu_Color color)
{
	return luminance(color);
}

static __always_inline void set_pixel_l8(int x, int y, uint32_t pixel)
{
	display_buffer[y * DISPLAY_WIDTH + x] = pixel;
}
#endif

#if IS_ENABLED(CONFIG_MICROUI_RENDER_AL_88)
static __always_inline uint32_t color_to_pixel_al88(mu_Color color)
{
	return ((uint32_t)color.a << 8) | luminance(color);
}

static __always_inline void set_pixel_al88(int x, int y, uint32_t pixel)
{
	int index = (y * DISPLAY_WIDTH + x) * 2;
	uint16_t *p = (uint16_t *)(display_buffer + index);
#ifdef CONFIG_MICROUI_ALPHA_BLENDING
	uint8_t src_a = (pixel >> 8) & 0xFF;

	if (src_a == 255) {
		*p = (uint16_t)pixel;
	} else if (src_a > 0) {
		uint16_t dst = *p;
		uint8_t dst_a = (dst >> 8) & 0xFF;
		uint8_t dst_l = dst & 0xFF;
		uint8_t src_l = pixel & 0xFF;

		uint16_t inv_a = 255 - src_a;
		uint16_t out_a = src_a + (dst_a * inv_a) / 255;

		if (out_a > 0) {
			uint8_t out_l = (src_l * src_a + dst_l * dst_a * inv_a / 255) / out_a;
			*p = ((uint16_t)out_a << 8) | out_l;
		}
	}
#else
	*p = (uint16_t)pixel;
#endif
}
#endif

static __always_inline uint32_t color_to_pixel(mu_Color color)
{
#if ENABLED_CF_COUNT == 1
#if defined(CONFIG_MICROUI_RENDER_RGB_888)
	return color_to_pixel_rgb888(color);
#elif defined(CONFIG_MICROUI_RENDER_ARGB_8888)
	return color_to_pixel_argb8888(color);
#elif defined(CONFIG_MICROUI_RENDER_RGB_565)
	return color_to_pixel_rgb565(color);
#elif defined(CONFIG_MICROUI_RENDER_BGR_565)
	return color_to_pixel_bgr565(color);
#elif defined(CONFIG_MICROUI_RENDER_MONO)
	return color_to_pixel_mono(color);
#elif defined(CONFIG_MICROUI_RENDER_L_8)
	return color_to_pixel_l8(color);
#elif defined(CONFIG_MICROUI_RENDER_AL_88)
	return color_to_pixel_al88(color);
#endif
#else
#ifdef CONFIG_MICROUI_RENDER_RGB_888
	if (display_caps.current_pixel_format == PIXEL_FORMAT_RGB_888) {
		return color_to_pixel_rgb888(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_ARGB_8888
	if (display_caps.current_pixel_format == PIXEL_FORMAT_ARGB_8888) {
		return color_to_pixel_argb8888(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_RGB_565
	if (display_caps.current_pixel_format == PIXEL_FORMAT_RGB_565) {
		return color_to_pixel_rgb565(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_BGR_565
	if (display_caps.current_pixel_format == PIXEL_FORMAT_BGR_565) {
		return color_to_pixel_bgr565(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_MONO
	if (display_caps.current_pixel_format == PIXEL_FORMAT_MONO01 ||
	    display_caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
		return color_to_pixel_mono(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_L_8
	if (display_caps.current_pixel_format == PIXEL_FORMAT_L_8) {
		return color_to_pixel_l8(color);
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_AL_88
	if (display_caps.current_pixel_format == PIXEL_FORMAT_AL_88) {
		return color_to_pixel_al88(color);
	}
#endif
	return 0;
#endif
}

static __always_inline void set_pixel_unchecked(int x, int y, uint32_t pixel)
{
#if ENABLED_CF_COUNT == 1
#if defined(CONFIG_MICROUI_RENDER_RGB_888)
	set_pixel_rgb888(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_ARGB_8888)
	set_pixel_argb8888(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_RGB_565)
	set_pixel_rgb565(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_BGR_565)
	set_pixel_bgr565(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_MONO)
	set_pixel_mono(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_L_8)
	set_pixel_l8(x, y, pixel);
#elif defined(CONFIG_MICROUI_RENDER_AL_88)
	set_pixel_al88(x, y, pixel);
#endif
#else
#ifdef CONFIG_MICROUI_RENDER_RGB_888
	if (display_caps.current_pixel_format == PIXEL_FORMAT_RGB_888) {
		set_pixel_rgb888(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_ARGB_8888
	if (display_caps.current_pixel_format == PIXEL_FORMAT_ARGB_8888) {
		set_pixel_argb8888(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_RGB_565
	if (display_caps.current_pixel_format == PIXEL_FORMAT_RGB_565) {
		set_pixel_rgb565(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_BGR_565
	if (display_caps.current_pixel_format == PIXEL_FORMAT_BGR_565) {
		set_pixel_bgr565(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_MONO
	if (display_caps.current_pixel_format == PIXEL_FORMAT_MONO01 ||
	    display_caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
		set_pixel_mono(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_L_8
	if (display_caps.current_pixel_format == PIXEL_FORMAT_L_8) {
		set_pixel_l8(x, y, pixel);
		return;
	}
#endif
#ifdef CONFIG_MICROUI_RENDER_AL_88
	if (display_caps.current_pixel_format == PIXEL_FORMAT_AL_88) {
		set_pixel_al88(x, y, pixel);
		return;
	}
#endif
#endif
}

static __always_inline void set_pixel(int x, int y, uint32_t pixel)
{
	if (x < clip_rect.x || y < clip_rect.y || x >= clip_rect.x + clip_rect.w ||
	    y >= clip_rect.y + clip_rect.h) {
		return;
	}

	set_pixel_unchecked(x, y, pixel);
}

static void renderer_draw_line(mu_Vec2 p0, mu_Vec2 p1, uint8_t thickness, mu_Color color)
{
	int dx = abs(p1.x - p0.x);
	int dy = abs(p1.y - p0.y);
	int sx = (p0.x < p1.x) ? 1 : -1;
	int sy = (p0.y < p1.y) ? 1 : -1;
	int err = dx - dy;
	uint32_t pixel = color_to_pixel(color);

	while (true) {
		for (int ty = -thickness / 2; ty <= thickness / 2; ty++) {
			for (int tx = -thickness / 2; tx <= thickness / 2; tx++) {
				set_pixel(p0.x + tx, p0.y + ty, pixel);
			}
		}

		if (p0.x == p1.x && p0.y == p1.y) {
			break;
		}
		int err2 = err * 2;
		if (err2 > -dy) {
			err -= dy;
			p0.x += sx;
		}
		if (err2 < dx) {
			err += dx;
			p0.y += sy;
		}
	}
}

static __always_inline void draw_glyph(const struct mu_FontGlyph *glyph, int x, int y,
				       const struct mu_FontDescriptor *font, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	/* Compute visible bounds by intersecting glyph rect with display and clip rect */
	mu_Rect glyph_rect = mu_rect(x, y, glyph->width, font->height);
	mu_Rect display_rect = mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	mu_Rect visible = intersect_rects(glyph_rect, display_rect);

	visible = intersect_rects(visible, clip_rect);

	/* Early exit if completely clipped */
	if (visible.w == 0 || visible.h == 0) {
		return;
	}

	/* Convert visible bounds to glyph-local coordinates */
	int start_col = visible.x - x;
	int end_col = visible.x + visible.w - x;
	int start_row = visible.y - y;
	int end_row = visible.y + visible.h - y;

	for (int row = start_row; row < end_row; row++) {
		int screen_y = y + row;
		if (font->bitmap_width <= 8) {
			uint8_t row_data = glyph->bitmap[row];
			/* Mask out bits outside the visible range */
			row_data &= (0xFF >> start_col);
			row_data &= (0xFF << (8 - end_col));
			while (row_data) {
				int col = __builtin_clz((uint32_t)row_data) - 24;
				set_pixel_unchecked(x + col, screen_y, pixel);
				row_data &= ~(0x80 >> col);
			}
		} else if (font->bitmap_width <= 16) {
			uint16_t row_data = sys_get_be16(&glyph->bitmap[row * 2]);
			/* Mask out bits outside the visible range */
			row_data &= (0xFFFF >> start_col);
			row_data &= (0xFFFF << (16 - end_col));
			while (row_data) {
				int col = __builtin_clz((uint32_t)row_data) - 16;
				set_pixel_unchecked(x + col, screen_y, pixel);
				row_data &= ~(0x8000 >> col);
			}
		} else if (font->bitmap_width <= 32) {
			uint32_t row_data = sys_get_be32(&glyph->bitmap[row * 4]);
			/* Mask out bits outside the visible range */
			row_data &= (0xFFFFFFFFu >> start_col);
			row_data &= (0xFFFFFFFFu << (32 - end_col));
			while (row_data) {
				int col = __builtin_clz(row_data);
				set_pixel_unchecked(x + col, screen_y, pixel);
				row_data &= ~(0x80000000u >> col);
			}
		} else {
			/* bitmap_width <= 64 */
			uint64_t row_data = sys_get_be64(&glyph->bitmap[row * 8]);
			/* Mask out bits outside the visible range */
			row_data &= (0xFFFFFFFFFFFFFFFFull >> start_col);
			row_data &= (0xFFFFFFFFFFFFFFFFull << (64 - end_col));
			while (row_data) {
				int col = __builtin_clzll(row_data);
				set_pixel_unchecked(x + col, screen_y, pixel);
				row_data &= ~(0x8000000000000000ull >> col);
			}
		}
	}
}

static void renderer_init(void)
{
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device is not ready");
		return;
	}

	display_get_capabilities(display_dev, &display_caps);
	display_blanking_off(display_dev);

	clip_rect.x = 0;
	clip_rect.y = 0;
	clip_rect.w = DISPLAY_WIDTH;
	clip_rect.h = DISPLAY_HEIGHT;

	memset(display_buffer, 0, DISPLAY_BUFFER_SIZE);

	LOG_INF("MicroUI renderer initialized for %dx%d display", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static void renderer_draw_rect(mu_Rect rect, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	/* Clamp to display bounds (microui already handled clip rect intersection) */
	mu_Rect display_rect = mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	rect = intersect_rects(rect, display_rect);

	if (rect.w == 0 || rect.h == 0) {
		return;
	}

#ifdef CONFIG_MICROUI_RENDER_MONO
	if (display_caps.current_pixel_format == PIXEL_FORMAT_MONO01 ||
	    display_caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
		for (int y = rect.y; y < rect.y + rect.h; y++) {
			for (int x = rect.x; x < rect.x + rect.w; x++) {
				set_pixel_unchecked(x, y, pixel);
			}
		}
		return;
	}
#endif /* CONFIG_MICROUI_RENDER_MONO */

#ifdef CONFIG_MICROUI_ALPHA_BLENDING
	/* When alpha blending with non-opaque color, must blend each pixel individually */
	if (color.a < 255) {
		for (int y = rect.y; y < rect.y + rect.h; y++) {
			for (int x = rect.x; x < rect.x + rect.w; x++) {
				set_pixel_unchecked(x, y, pixel);
			}
		}
		return;
	}
#endif /* CONFIG_MICROUI_ALPHA_BLENDING */

	for (int x = rect.x; x < rect.x + rect.w; x++) {
		set_pixel_unchecked(x, rect.y, pixel);
	}
	if (rect.h == 1) {
		return;
	}

	/* Calculate source and destination pointers for memcpy */
	uint8_t *src_row =
		display_buffer + (rect.y * DISPLAY_STRIDE) + (rect.x * DISPLAY_BYTES_PER_PIXEL);
	int row_bytes = rect.w * DISPLAY_BYTES_PER_PIXEL;

	/* Copy first row to subsequent rows */
	for (int y = 1; y < rect.h; y++) {
		uint8_t *dst_row = src_row + (y * DISPLAY_STRIDE);
		memcpy(dst_row, src_row, row_bytes);
	}
}

static void renderer_draw_text(mu_Font f, const char *text, mu_Vec2 pos, mu_Color color)
{
	int x = pos.x;
	const struct mu_FontDescriptor *font = (struct mu_FontDescriptor *)f;

	if (!font) {
		LOG_WRN_ONCE("Font is NULL, cannot draw text");
		return;
	}

	const char *current = text;
	while (*current) {
		uint32_t codepoint;
		int bytes_consumed = next_utf8_codepoint(current, &codepoint);

		if (unlikely(bytes_consumed == 0)) {
			break;
		}

		const struct mu_FontGlyph *glyph = find_glyph(font, codepoint);
		if (likely(glyph)) {
			draw_glyph(glyph, x, pos.y, font, color);
			x += glyph->width + font->char_spacing;
		} else {
			x += font->default_width + font->char_spacing;
		}

		current += bytes_consumed;
	}
}

static void renderer_draw_icon(int id, mu_Rect rect, mu_Color color)
{
	switch (id) {
	case MU_ICON_CLOSE:
		renderer_draw_line(
			(mu_Vec2){rect.x + rect.w / 4, rect.y + rect.h / 4},
			(mu_Vec2){rect.x + rect.w - rect.w / 4, rect.y + rect.h - rect.h / 4}, 1,
			color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w - rect.w / 4, rect.y + rect.h / 4},
				   (mu_Vec2){rect.x + rect.w / 4, rect.y + rect.h - rect.h / 4}, 1,
				   color);
		break;
	case MU_ICON_COLLAPSED:
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h / 3},
				   (mu_Vec2){rect.x + rect.w - rect.w / 3, rect.y + rect.h / 2}, 1,
				   color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w - rect.w / 3, rect.y + rect.h / 2},
				   (mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h - rect.h / 3}, 1,
				   color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h - rect.h / 3},
				   (mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h / 3}, 1, color);
		break;
	case MU_ICON_EXPANDED:
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h / 3},
				   (mu_Vec2){rect.x + rect.w - rect.w / 3, rect.y + rect.h / 3}, 1,
				   color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w - rect.w / 3, rect.y + rect.h / 3},
				   (mu_Vec2){rect.x + rect.w / 2, rect.y + rect.h - rect.h / 3}, 1,
				   color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 2, rect.y + rect.h - rect.h / 3},
				   (mu_Vec2){rect.x + rect.w / 3, rect.y + rect.h / 3}, 1, color);
		break;
	case MU_ICON_CHECK:
		// Draw a check mark with some padding
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 4, rect.y + rect.h / 2},
				   (mu_Vec2){rect.x + rect.w / 2, rect.y + rect.h - rect.h / 4}, 1,
				   color);
		renderer_draw_line((mu_Vec2){rect.x + rect.w / 2, rect.y + rect.h - rect.h / 4},
				   (mu_Vec2){rect.x + rect.w - rect.w / 5, rect.y + rect.h / 5}, 1,
				   color);
		break;
	}
}

static int renderer_get_text_width(mu_Font f, const char *text, int len)
{
	int width = 0;
	int char_count = 0;
	int byte_count = 0;
	const struct mu_FontDescriptor *font = (const struct mu_FontDescriptor *)f;

	if (!font) {
		LOG_WRN_ONCE("Font is NULL, returning width 0");
		return 0;
	}

	if (len == -1) {
		len = strlen(text);
	}

#ifdef CONFIG_MICROUI_TEXT_WIDTH_CACHE
	uint32_t hash = fnv1a_hash(text, len);
	uint32_t cache_idx = hash % CONFIG_MICROUI_TEXT_WIDTH_CACHE_SIZE;
	struct text_width_cache_entry *entry = &text_width_cache[cache_idx];

	if (entry->font == font && entry->hash == hash && entry->len == len) {
		return entry->width;
	}
#endif /* CONFIG_MICROUI_TEXT_WIDTH_CACHE */

	const char *current = text;
	while (byte_count < len && *current) {
		uint32_t codepoint;
		int bytes_consumed = next_utf8_codepoint(current, &codepoint);

		if (bytes_consumed == 0 || byte_count + bytes_consumed > len) {
			break;
		}

		const struct mu_FontGlyph *glyph = find_glyph(font, codepoint);
		if (likely(glyph)) {
			width += glyph->width;
		} else {
			width += font->default_width;
		}

		char_count++;
		byte_count += bytes_consumed;
		current += bytes_consumed;
	}

	if (char_count > 0) {
		width += (char_count - 1) * font->char_spacing;
	}

#ifdef CONFIG_MICROUI_TEXT_WIDTH_CACHE
	entry->font = font;
	entry->hash = hash;
	entry->len = len;
	entry->width = width;
#endif /* CONFIG_MICROUI_TEXT_WIDTH_CACHE */

	return width;
}

static int renderer_get_text_height(mu_Font f)
{
	const struct mu_FontDescriptor *font = (const struct mu_FontDescriptor *)f;
	if (!font) {
		LOG_WRN_ONCE("Font is NULL, returning height 0");
		return 0;
	}
	return font->height;
}

static void renderer_set_clip_rect(mu_Rect rect)
{
	static mu_Rect screen_rect = {0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT};
	clip_rect = intersect_rects(rect, screen_rect);
}

#ifdef CONFIG_MICROUI_RENDER_CLEAR_BEFORE_DRAW
static void renderer_clear(mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);
	for (int x = 0; x < DISPLAY_WIDTH; x++) {
		set_pixel_unchecked(x, 0, pixel);
	}
	uint8_t *src_row = display_buffer;
	int row_bytes = DISPLAY_WIDTH * DISPLAY_BYTES_PER_PIXEL;
	for (int y = 1; y < DISPLAY_HEIGHT; y++) {
		uint8_t *dst_row = src_row + (y * DISPLAY_STRIDE);
		memcpy(dst_row, src_row, row_bytes);
	}
}
#endif /* CONFIG_MICROUI_RENDER_CLEAR_BEFORE_DRAW */

static void renderer_present(void)
{
	static struct display_buffer_descriptor desc = {
		.buf_size = DISPLAY_BUFFER_SIZE,
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
		.frame_incomplete = false,
	};
	display_write(display_dev, 0, 0, &desc, display_buffer);
}

#ifdef CONFIG_MICROUI_DRAW_EXTENSIONS

static __always_inline mu_Color pixel_to_color(const uint8_t *src, int offset,
					       enum display_pixel_format format)
{
	mu_Color color = {0, 0, 0, 255};

	switch (format) {
	case PIXEL_FORMAT_RGB_888:
		color.r = src[offset * 3 + 0];
		color.g = src[offset * 3 + 1];
		color.b = src[offset * 3 + 2];
		break;
	case PIXEL_FORMAT_ARGB_8888:
		color.b = src[offset * 4 + 0];
		color.g = src[offset * 4 + 1];
		color.r = src[offset * 4 + 2];
		color.a = src[offset * 4 + 3];
		break;
	case PIXEL_FORMAT_RGB_565: {
		uint16_t rgb565 = (src[offset * 2] << 8) | src[offset * 2 + 1];
		color.r = ((rgb565 >> 11) & 0x1F) << 3;
		color.g = ((rgb565 >> 5) & 0x3F) << 2;
		color.b = (rgb565 & 0x1F) << 3;
		break;
	}
	case PIXEL_FORMAT_BGR_565: {
		uint16_t bgr565 = src[offset * 2] | (src[offset * 2 + 1] << 8);
		color.b = ((bgr565 >> 11) & 0x1F) << 3;
		color.g = ((bgr565 >> 5) & 0x3F) << 2;
		color.r = (bgr565 & 0x1F) << 3;
		break;
	}
	case PIXEL_FORMAT_L_8:
		color.r = color.g = color.b = src[offset];
		break;
	case PIXEL_FORMAT_AL_88:
		color.a = src[offset * 2 + 0];
		color.r = color.g = color.b = src[offset * 2 + 1];
		break;
	default:
		break;
	}

	return color;
}

static void renderer_draw_arc(mu_Vec2 center, int radius, int thickness, mu_Real start_angle,
			      mu_Real end_angle, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);
	int cx = center.x;
	int cy = center.y;

	/* Convert angles from degrees to 0-255 range (256 = 360 degrees)
	 * 0° is at 3 o'clock, angles increase clockwise.
	 */
	int a_start = (int)(start_angle * 256 / 360) & 0xFF;
	int a_end = (int)(end_angle * 256 / 360) & 0xFF;

	/* Calculate inner and outer radius from center radius and thickness */
	int inner_radius = radius - thickness / 2;
	int outer_radius = radius + (thickness - 1) / 2;

	if (inner_radius < 0) {
		inner_radius = 0;
	}

	/* Manage angle inputs */
	bool inverted = (a_start > a_end);
	bool full = (a_start == a_end);

	if (inverted) {
		int tmp = a_start;
		a_start = a_end;
		a_end = tmp;
	}

	/* Trace each arc radius with the Andres circle algorithm */
	for (int r = inner_radius; r <= outer_radius; r++) {
		int x = 0;
		int y = r;
		int d = r - 1;

		/* Process each pixel of a 1/8th circle of radius r */
		while (y >= x) {
			/* Get the ratio (0-32) representing angle as fraction of 1/8th circle */
			int ratio = arc_atan_ratio(x, y);

			/* Fill the pixels of the 8 sections of the circle,
			 * but only on the arc defined by the angles (start and end).
			 * Pixel positions are arranged so 0° is at 3 o'clock and
			 * angles increase clockwise.
			 */
			/* Octant 1: 0° - 45° (3 o'clock going down-right) */
			if (full || ((ratio >= a_start && ratio < a_end) ^ inverted)) {
				set_pixel(cx + y, cy + x, pixel);
			}
			/* Octant 2: 45° - 90° */
			if (full || ((ratio > (63 - a_end) && ratio <= (63 - a_start)) ^ inverted)) {
				set_pixel(cx + x, cy + y, pixel);
			}
			/* Octant 3: 90° - 135° */
			if (full || ((ratio >= (a_start - 64) && ratio < (a_end - 64)) ^ inverted)) {
				set_pixel(cx - x, cy + y, pixel);
			}
			/* Octant 4: 135° - 180° */
			if (full || ((ratio > (127 - a_end) && ratio <= (127 - a_start)) ^ inverted)) {
				set_pixel(cx - y, cy + x, pixel);
			}
			/* Octant 5: 180° - 225° */
			if (full || ((ratio >= (a_start - 128) && ratio < (a_end - 128)) ^ inverted)) {
				set_pixel(cx - y, cy - x, pixel);
			}
			/* Octant 6: 225° - 270° */
			if (full || ((ratio > (191 - a_end) && ratio <= (191 - a_start)) ^ inverted)) {
				set_pixel(cx - x, cy - y, pixel);
			}
			/* Octant 7: 270° - 315° */
			if (full || ((ratio >= (a_start - 192) && ratio < (a_end - 192)) ^ inverted)) {
				set_pixel(cx + x, cy - y, pixel);
			}
			/* Octant 8: 315° - 360° */
			if (full || ((ratio > (255 - a_end) && ratio <= (255 - a_start)) ^ inverted)) {
				set_pixel(cx + y, cy - x, pixel);
			}

			/* Run Andres circle algorithm to get to the next pixel */
			if (d >= 2 * x) {
				d = d - 2 * x - 1;
				x = x + 1;
			} else if (d < 2 * (r - y)) {
				d = d + 2 * y - 1;
				y = y - 1;
			} else {
				d = d + 2 * (y - x - 1);
				y = y - 1;
				x = x + 1;
			}
		}
	}
}

static void renderer_draw_circle(mu_Vec2 center, int radius, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);
	int cx = center.x;
	int cy = center.y;

	int x = radius;
	int y = 0;
	int err = 1 - x;

	while (x >= y) {
		// For each "row band" of y, draw horizontal spans across the circle
		for (int i = cx - x; i <= cx + x; i++) {
			set_pixel(i, cy + y, pixel);
			set_pixel(i, cy - y, pixel);
		}
		for (int i = cx - y; i <= cx + y; i++) {
			set_pixel(i, cy + x, pixel);
			set_pixel(i, cy - x, pixel);
		}

		y++;
		if (err < 0) {
			err += 2 * y + 1;
		} else {
			x--;
			err += 2 * (y - x + 1);
		}
	}
}

static void renderer_draw_image(mu_Vec2 pos, mu_Image image)
{
	if (image == NULL) {
		return;
	}

	const struct mu_ImageDescriptor *img_desc = (const struct mu_ImageDescriptor *)image;

	/* Validate image descriptor */
	if (img_desc->data == NULL || img_desc->width == 0 || img_desc->height == 0) {
		return;
	}

	/* Calculate visible region by intersecting image rect with display bounds */
	mu_Rect img_rect = mu_rect(pos.x, pos.y, img_desc->width, img_desc->height);
	mu_Rect display_rect = mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	mu_Rect visible = intersect_rects(img_rect, display_rect);

	visible = intersect_rects(visible, clip_rect);

	/* Early exit if completely clipped */
	if (visible.w == 0 || visible.h == 0) {
		return;
	}

	/* Calculate source offsets (image-local coordinates) */
	int src_x_start = visible.x - pos.x;
	int src_y_start = visible.y - pos.y;

	/* Handle monochrome formats separately (need to use set_pixel) */
	if (img_desc->pixel_format == PIXEL_FORMAT_MONO01 ||
	    img_desc->pixel_format == PIXEL_FORMAT_MONO10) {
		bool invert = (img_desc->pixel_format == PIXEL_FORMAT_MONO10);

		for (int row = 0; row < visible.h; row++) {
			int src_y = src_y_start + row;
			int dst_y = visible.y + row;

			for (int col = 0; col < visible.w; col++) {
				int src_x = src_x_start + col;
				int dst_x = visible.x + col;

				/* Calculate bit position in source data */
				int byte_idx = src_y * img_desc->stride + (src_x / 8);
				int bit_idx = 7 - (src_x % 8);

				/* Extract bit value */
				uint8_t bit_val = (img_desc->data[byte_idx] >> bit_idx) & 0x01;

				/* Apply inversion if MONO10 */
				if (invert) {
					bit_val = !bit_val;
				}

				/* Convert to pixel value (white or black) with full alpha */
				uint32_t pixel = bit_val ? 0xFFFFFFFF : 0xFF000000;
				set_pixel_unchecked(dst_x, dst_y, pixel);
			}
		}
		return;
	}

	/* For non-mono formats, check if pixel formats match for fast path */
	bool format_matches = (img_desc->pixel_format == display_caps.current_pixel_format);

#ifdef CONFIG_MICROUI_ALPHA_BLENDING
	/* When alpha blending is enabled, formats with alpha channel cannot use fast path */
	bool has_alpha = (img_desc->pixel_format == PIXEL_FORMAT_ARGB_8888 ||
			  img_desc->pixel_format == PIXEL_FORMAT_AL_88);
	if (has_alpha) {
		format_matches = false;
	}
#endif

	if (format_matches) {
		/* Fast path: direct memcpy when formats match */
		for (int row = 0; row < visible.h; row++) {
			int src_y = src_y_start + row;
			int dst_y = visible.y + row;

			/* Calculate source and destination offsets */
			const uint8_t *src = img_desc->data +
					     (src_y * img_desc->stride) +
					     (src_x_start * DISPLAY_BYTES_PER_PIXEL);
			uint8_t *dst = display_buffer +
				       (dst_y * DISPLAY_STRIDE) +
				       (visible.x * DISPLAY_BYTES_PER_PIXEL);

			/* Copy row data */
			int bytes_to_copy = visible.w * DISPLAY_BYTES_PER_PIXEL;
			memcpy(dst, src, bytes_to_copy);
		}
	} else {
		/* Slow path: format conversion needed - use set_pixel for each pixel */
		for (int row = 0; row < visible.h; row++) {
			int src_y = src_y_start + row;
			int dst_y = visible.y + row;
			const uint8_t *src_row = img_desc->data + (src_y * img_desc->stride);

			for (int col = 0; col < visible.w; col++) {
				int src_x = src_x_start + col;
				int dst_x = visible.x + col;

				mu_Color color = pixel_to_color(src_row, src_x, img_desc->pixel_format);
				uint32_t pixel = color_to_pixel(color);
				set_pixel_unchecked(dst_x, dst_y, pixel);
			}
		}
	}
}

static void renderer_draw_triangle(mu_Vec2 p0, mu_Vec2 p1, mu_Vec2 p2, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	/* Sort vertices by y-coordinate (p0.y <= p1.y <= p2.y) */
	if (p0.y > p1.y) {
		mu_Vec2 tmp = p0;
		p0 = p1;
		p1 = tmp;
	}
	if (p1.y > p2.y) {
		mu_Vec2 tmp = p1;
		p1 = p2;
		p2 = tmp;
	}
	if (p0.y > p1.y) {
		mu_Vec2 tmp = p0;
		p0 = p1;
		p1 = tmp;
	}

	/* Calculate clipping bounds */
	int clip_x_min, clip_x_max, clip_y_min, clip_y_max;

	clip_x_min = clip_rect.x;
	clip_x_max = clip_rect.x + clip_rect.w - 1;
	clip_y_min = clip_rect.y;
	clip_y_max = clip_rect.y + clip_rect.h - 1;

	/* Early exit if triangle is completely outside clip bounds */
	int tri_y_min = p0.y;
	int tri_y_max = p2.y;
	int tri_x_min = mu_min(p0.x, mu_min(p1.x, p2.x));
	int tri_x_max = mu_max(p0.x, mu_max(p1.x, p2.x));

	if (tri_y_max < clip_y_min || tri_y_min > clip_y_max ||
	    tri_x_max < clip_x_min || tri_x_min > clip_x_max) {
		return;
	}

	/* Handle degenerate triangle (all points on same horizontal line) */
	if (p0.y == p2.y) {
		if (p0.y < clip_y_min || p0.y > clip_y_max) {
			return;
		}
		int min_x = mu_max(tri_x_min, clip_x_min);
		int max_x = mu_min(tri_x_max, clip_x_max);
		for (int x = min_x; x <= max_x; x++) {
			set_pixel_unchecked(x, p0.y, pixel);
		}
		return;
	}

	/* Clamp y-range to clip bounds */
	int y_start = mu_max(p0.y, clip_y_min);
	int y_end = mu_min(p2.y, clip_y_max);

	/* Rasterize using scanline algorithm */
	int total_height = p2.y - p0.y;

	for (int y = y_start; y <= y_end; y++) {
		bool second_half = (y > p1.y) || (p1.y == p0.y);
		int segment_height = second_half ? (p2.y - p1.y) : (p1.y - p0.y);

		if (segment_height == 0) {
			segment_height = 1;
		}

		/* Calculate interpolation factors using fixed-point arithmetic */
		int alpha = (y - p0.y) * 256 / total_height;
		int beta = second_half ? ((y - p1.y) * 256 / segment_height)
				       : ((y - p0.y) * 256 / segment_height);

		/* Calculate x coordinates on edge p0-p2 and on edge p0-p1 or p1-p2 */
		int x_a = p0.x + ((p2.x - p0.x) * alpha) / 256;
		int x_b = second_half ? (p1.x + ((p2.x - p1.x) * beta) / 256)
				      : (p0.x + ((p1.x - p0.x) * beta) / 256);

		/* Ensure x_a <= x_b */
		if (x_a > x_b) {
			int tmp = x_a;
			x_a = x_b;
			x_b = tmp;
		}

		/* Clamp x-range to clip bounds */
		x_a = mu_max(x_a, clip_x_min);
		x_b = mu_min(x_b, clip_x_max);

		/* Draw horizontal line from x_a to x_b */
		for (int x = x_a; x <= x_b; x++) {
			set_pixel_unchecked(x, y, pixel);
		}
	}
}

#endif

void mu_set_bg_color(mu_Color color)
{
	bg_color = color;
}

mu_Context *mu_get_context(void)
{
	return &mu_ctx;
}

void mu_render(void)
{
#ifdef CONFIG_MICROUI_RENDER_CLEAR_BEFORE_DRAW
	renderer_clear(bg_color);
#endif /* CONFIG_MICROUI_RENDER_CLEAR_BEFORE_DRAW */
	mu_Command *cmd = NULL;
	while (mu_next_command(&mu_ctx, &cmd)) {
		switch (cmd->type) {
		case MU_COMMAND_TEXT:
			renderer_draw_text(cmd->text.font, cmd->text.str, cmd->text.pos,
					   cmd->text.color);
			break;
		case MU_COMMAND_RECT:
			renderer_draw_rect(cmd->rect.rect, cmd->rect.color);
			break;
		case MU_COMMAND_ICON:
			renderer_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
			break;
		case MU_COMMAND_CLIP:
			renderer_set_clip_rect(cmd->clip.rect);
			break;
#ifdef CONFIG_MICROUI_DRAW_EXTENSIONS
		case MU_COMMAND_ARC:
			renderer_draw_arc(cmd->arc.center, cmd->arc.radius, cmd->arc.thickness,
					  cmd->arc.start_angle, cmd->arc.end_angle, cmd->arc.color);
			break;
		case MU_COMMAND_CIRCLE:
			renderer_draw_circle(cmd->circle.center, cmd->circle.radius,
					     cmd->circle.color);
			break;
		case MU_COMMAND_LINE:
			renderer_draw_line(cmd->line.p0, cmd->line.p1, cmd->line.thickness,
					   cmd->line.color);
			break;
		case MU_COMMAND_IMAGE:
			renderer_draw_image(cmd->image.pos, cmd->image.image);
			break;
		case MU_COMMAND_TRIANGLE:
			renderer_draw_triangle(cmd->triangle.p0, cmd->triangle.p1,
					       cmd->triangle.p2, cmd->triangle.color);
			break;
#endif
		}
	}
	renderer_present();
}

bool mu_needs_redraw(void)
{
	static mu_Id previous_command_hash;
	mu_Id current_command_hash =
		mu_get_id(&mu_ctx, &mu_ctx.command_list.items, mu_ctx.command_list.idx);

	if (current_command_hash == previous_command_hash) {
		return false;
	}
	previous_command_hash = current_command_hash;

	return true;
}

bool mu_handle_tick(void)
{
#ifdef CONFIG_MICROUI_INPUT
	mu_handle_input_events();
#endif /* CONFIG_MICROUI_INPUT */

	if (frame_cb) {
		frame_cb(&mu_ctx);
	}

#ifdef CONFIG_MICROUI_LAZY_REDRAW
	if (!mu_needs_redraw()) {
		return false;
	}
#endif /* CONFIG_MICROUI_LAZY_REDRAW */

	mu_render();

	return true;
}

#ifdef CONFIG_MICROUI_EVENT_LOOP

static void microui_loop_work(struct k_work *work)
{
	int64_t current_time = k_uptime_get();

	mu_handle_tick();

	int64_t render_time = k_uptime_get() - current_time;
	int64_t wait_time = CONFIG_MICROUI_DISPLAY_REFRESH_PERIOD - render_time;

	if (wait_time < 0) {
		wait_time = 0;
	}

	k_work_schedule_for_queue(&mu_work_queue, k_work_delayable_from_work(work),
				  K_MSEC(wait_time));
}

static K_WORK_DELAYABLE_DEFINE(mu_loop_work, microui_loop_work);

int mu_event_loop_start(void)
{
	__ASSERT(frame_cb, "Process frame callback not set!");
	k_work_queue_start(&mu_work_queue, mu_work_stack, K_KERNEL_STACK_SIZEOF(mu_work_stack),
			   CONFIG_MICROUI_EVENT_LOOP_THREAD_PRIORITY, NULL);
	k_work_submit_to_queue(&mu_work_queue, &mu_loop_work.work);
	return 0;
}

int mu_event_loop_stop(void)
{
	return k_work_queue_stop(&mu_work_queue, K_FOREVER);
}

#endif /* CONFIG_MICROUI_EVENT_LOOP */

int mu_setup(mu_process_frame_cb cb)
{
	if (!cb) {
		return -EINVAL;
	}

	frame_cb = cb;
	renderer_init();

	mu_init(&mu_ctx);
	mu_ctx.text_width = renderer_get_text_width;
	mu_ctx.text_height = renderer_get_text_height;
	mu_ctx.img_dimensions = mu_get_img_dimensions;

#ifdef CONFIG_MICROUI_EVENT_LOOP
	k_work_queue_init(&mu_work_queue);
#endif /* CONFIG_MICROUI_EVENT_LOOP */

	return 0;
}
