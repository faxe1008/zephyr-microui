#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <microui/event_loop.h>
#include <microui/font.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(microui_event_loop, LOG_LEVEL_INF);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define DISPLAY_NODE   DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH  DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT DT_PROP(DISPLAY_NODE, height)

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
static bool has_clip_rect = false;

/* Work queue and work queue thread */
#ifdef CONFIG_MICROUI_EVENT_LOOP
static struct k_work_q mu_work_queue;
static K_KERNEL_STACK_DEFINE(mu_work_stack, CONFIG_MICROUI_EVENT_LOOP_STACK_SIZE);
#endif /* CONFIG_MICROUI_EVENT_LOOP */
static volatile mu_process_frame_cb frame_cb;

static __always_inline const struct FontGlyph *find_glyph(const struct Font *font,
							  uint32_t codepoint)
{
	int left = 0;
	int right = font->glyph_count - 1;

	while (left <= right) {
		int mid = left + (right - left) / 2;

		if (font->glyphs[mid].codepoint == codepoint) {
			return &font->glyphs[mid];
		}

		if (font->glyphs[mid].codepoint < codepoint) {
			left = mid + 1;
		} else {
			right = mid - 1;
		}
	}

	return NULL;
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

static __always_inline uint32_t color_to_pixel(mu_Color color)
{
	switch (display_caps.current_pixel_format) {
	case PIXEL_FORMAT_RGB_888:
		return (color.r << 16) | (color.g << 8) | color.b;

	case PIXEL_FORMAT_ARGB_8888:
		return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;

	case PIXEL_FORMAT_RGB_565: {
		uint16_t rgb565 =
			((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
		return sys_cpu_to_be16(rgb565);
	}

	case PIXEL_FORMAT_BGR_565:
		return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);

	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10: {
		uint8_t luma = luminance(color);
		return (luma > 127) ? 0xFF : 0;
	}

	case PIXEL_FORMAT_L_8:
		return luminance(color);
	case PIXEL_FORMAT_AL_88:
		return (color.a << 8) | luminance(color);
	default:
		return 0;
	}
	return 0;
}

static void set_pixel(int x, int y, uint32_t pixel)
{
	if (x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		return;
	}

	if (likely(has_clip_rect)) {
		if (x < clip_rect.x || y < clip_rect.y || x >= clip_rect.x + clip_rect.w ||
		    y >= clip_rect.y + clip_rect.h) {
			return;
		}
	}

	switch (display_caps.current_pixel_format) {
	case PIXEL_FORMAT_RGB_888: {
		int index = (y * DISPLAY_WIDTH + x) * 3;
		display_buffer[index + 0] = (pixel >> 16) & 0xFF; // R
		display_buffer[index + 1] = (pixel >> 8) & 0xFF;  // G
		display_buffer[index + 2] = pixel & 0xFF;         // B
		break;
	}

	case PIXEL_FORMAT_ARGB_8888: {
		int index = (y * DISPLAY_WIDTH + x) * 4;
		uint32_t *p = (uint32_t *)(display_buffer + index);
		*p = pixel;
		break;
	}

	case PIXEL_FORMAT_AL_88:
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565: {
		int index = (y * DISPLAY_WIDTH + x) * 2;
		uint16_t *p = (uint16_t *)(display_buffer + index);
		*p = (uint16_t)pixel;
		break;
	}

	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10: {
		uint8_t *buf;
		uint8_t bit;

		if (display_caps.screen_info & SCREEN_INFO_MONO_VTILED) {
			buf = display_buffer + x + (y >> 3) * DISPLAY_WIDTH;
			bit = (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST)
				      ? (7 - (y & 7))
				      : (y & 7);
		} else {
			buf = display_buffer + (x >> 3) + y * (DISPLAY_WIDTH >> 3);
			bit = (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST)
				      ? (7 - (x & 7))
				      : (x & 7);
		}

		if (pixel) {
			*buf |= BIT(bit);
		} else {
			*buf &= ~BIT(bit);
		}
		break;
	}

	case PIXEL_FORMAT_L_8: {
		display_buffer[y * DISPLAY_WIDTH + x] = pixel;
		break;
	}
	}
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

static __always_inline void draw_glyph(const struct FontGlyph *glyph, int x, int y,
				       const struct Font *font, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	for (int row = 0; row < font->height; row++) {
		if (font->bitmap_width <= 8) {
			uint8_t row_data = glyph->bitmap[row];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x80 >> col)) {
					set_pixel(x + col, y + row, pixel);
				}
			}
		} else if (font->bitmap_width <= 16) {
			uint16_t row_data =
				(glyph->bitmap[row * 2] << 8) | glyph->bitmap[row * 2 + 1];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x8000 >> col)) {
					set_pixel(x + col, y + row, pixel);
				}
			}
		} else {
			uint32_t row_data = (glyph->bitmap[row * 4] << 24) |
					    (glyph->bitmap[row * 4 + 1] << 16) |
					    (glyph->bitmap[row * 4 + 2] << 8) |
					    glyph->bitmap[row * 4 + 3];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x80000000 >> col)) {
					set_pixel(x + col, y + row, pixel);
				}
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
	has_clip_rect = false;

	memset(display_buffer, 0, DISPLAY_BUFFER_SIZE);

	LOG_INF("MicroUI renderer initialized for %dx%d display", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

static void renderer_draw_rect(mu_Rect rect, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);
	for (int y = rect.y; y < rect.y + rect.h; y++) {
		for (int x = rect.x; x < rect.x + rect.w; x++) {
			set_pixel(x, y, pixel);
		}
	}
}

static void renderer_draw_text(mu_Font f, const char *text, mu_Vec2 pos, mu_Color color)
{
	int x = pos.x;
	const struct Font *font = (struct Font *)f;

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

		const struct FontGlyph *glyph = find_glyph(font, codepoint);
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
	const struct Font *font = (const struct Font *)f;

	if (!font) {
		LOG_WRN_ONCE("Font is NULL, returning width 0");
		return 0;
	}

	if (len == -1) {
		len = strlen(text);
	}

	const char *current = text;
	while (byte_count < len && *current) {
		uint32_t codepoint;
		int bytes_consumed = next_utf8_codepoint(current, &codepoint);

		if (bytes_consumed == 0 || byte_count + bytes_consumed > len) {
			break;
		}

		const struct FontGlyph *glyph = find_glyph(font, codepoint);
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

	return width;
}

static int renderer_get_text_height(mu_Font f)
{
	const struct Font *font = (const struct Font *)f;
	if (!font) {
		LOG_WRN_ONCE("Font is NULL, returning height 0");
		return 0;
	}
	return font->height;
}

static void renderer_set_clip_rect(mu_Rect rect)
{
	clip_rect = rect;
	has_clip_rect = true;
}

static void renderer_clear(mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	switch (display_caps.current_pixel_format) {
	case PIXEL_FORMAT_AL_88:
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565: {
		uint16_t pixel16 = (uint16_t)pixel;
		uint16_t *buf16 = (uint16_t *)display_buffer;
		for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
			buf16[i] = pixel16;
		}
		break;
	}
	case PIXEL_FORMAT_L_8:
	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10: {
		memset(display_buffer, (uint8_t)pixel, DISPLAY_BUFFER_SIZE);
		break;
	}
	case PIXEL_FORMAT_ARGB_8888: {
		uint32_t *buf32 = (uint32_t *)display_buffer;
		for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
			buf32[i] = pixel;
		}
		break;
	}
	default:
		for (int y = 0; y < DISPLAY_HEIGHT; y++) {
			for (int x = 0; x < DISPLAY_WIDTH; x++) {
				set_pixel(x, y, pixel);
			}
		}
	}
}

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
static void renderer_draw_arc(mu_Vec2 center, int radius, int thickness, mu_Real start_angle,
			      mu_Real end_angle, mu_Color color)
{
	uint32_t pixel = color_to_pixel(color);

	// Convert degrees to radians
	mu_Real start_rad = start_angle * M_PI / (mu_Real)180.0;
	mu_Real end_rad = end_angle * M_PI / (mu_Real)180.0;

	// Normalize angles to 0-2π range
	while (start_rad < 0) {
		start_rad += 2 * M_PI;
	}
	while (end_rad < 0) {
		end_rad += 2 * M_PI;
	}
	while (start_rad >= 2 * M_PI) {
		start_rad -= 2 * M_PI;
	}
	while (end_rad >= 2 * M_PI) {
		end_rad -= 2 * M_PI;
	}

	// Handle case where arc crosses 0°
	bool crosses_zero = end_rad < start_rad;

	// Draw arc using parametric circle equations
	for (int r = radius - thickness / 2; r <= radius + thickness / 2; r++) {
		if (r <= 0) {
			continue;
		}

		// Calculate step size based on radius for smooth arc
		int steps = 2 * M_PI * r;
		if (steps < 8) {
			steps = 8;
		}

		mu_Real step = 2 * M_PI / steps;

		for (int i = 0; i < steps; i++) {
			mu_Real angle = i * step;

			// Check if angle is within arc range
			bool in_range;
			if (crosses_zero) {
				in_range = (angle >= start_rad) || (angle <= end_rad);
			} else {
				in_range = (angle >= start_rad) && (angle <= end_rad);
			}

			if (in_range) {
				// Calculate pixel position (0° is at 3 o'clock)
				int x = center.x + (int)(r * cos(angle));
				int y = center.y + (int)(r * sin(angle));

				set_pixel(x, y, pixel);
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
#endif

void mu_set_bg_color(mu_Color color)
{
	bg_color = color;
}

mu_Context *mu_get_context(void)
{
	return &mu_ctx;
}

bool mu_handle_tick(void)
{
#ifdef CONFIG_MICROUI_INPUT
	mu_event_loop_handle_input_events();
#endif /* CONFIG_MICROUI_INPUT */

	if (frame_cb) {
		frame_cb(&mu_ctx);
	}

#ifdef CONFIG_MICROUI_LAZY_REDRAW
	static mu_Id previous_command_hash;
	mu_Id current_command_hash =
		mu_get_id(&mu_ctx, &mu_ctx.command_list.items, mu_ctx.command_list.idx);

	if (current_command_hash == previous_command_hash) {
		return false;
	}
	previous_command_hash = current_command_hash;
#endif /* CONFIG_MICROUI_LAZY_REDRAW */

	renderer_clear(bg_color);
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
#endif
		}
	}
	renderer_present();
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

#ifdef CONFIG_MICROUI_EVENT_LOOP
	k_work_queue_init(&mu_work_queue);
#endif /* CONFIG_MICROUI_EVENT_LOOP */

	return 0;
}
