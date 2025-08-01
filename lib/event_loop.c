#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <string.h>

#include <microui/event_loop.h>
#include <microui/font.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(microui_event_loop, LOG_LEVEL_INF);

#define DISPLAY_NODE   DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH  DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT DT_PROP(DISPLAY_NODE, height)

#define DISPLAY_BUFFER_SIZE (CONFIG_MICROUI_BITS_PER_PIXEL * DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8

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
static struct k_work_q mu_work_queue;
static K_KERNEL_STACK_DEFINE(mu_work_stack, CONFIG_MICROUI_EVENT_LOOP_STACK_SIZE);

static __always_inline const struct FontGlyph *find_glyph(const struct Font *font, uint32_t unicode)
{
	if (unicode < font->first_char || unicode > font->last_char) {
		return NULL;
	}
	return &font->glyphs[unicode - font->first_char];
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

	case PIXEL_FORMAT_RGB_565:
		return ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);

	case PIXEL_FORMAT_BGR_565:
		return ((color.b & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.r >> 3);

	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10: {
		uint8_t luma = luminance(color);
		if (display_caps.current_pixel_format == PIXEL_FORMAT_MONO01) {
			return (luma > 127) ? 1 : 0;
		} else {
			return (luma > 127) ? 0 : 1;
		}
	}

	case PIXEL_FORMAT_L_8:
		return luminance(color);
	default:
		return 0;
	}
	return 0;
}

static void set_pixel(int x, int y, mu_Color color)
{
	if (x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		return;
	}

	if (has_clip_rect) {
		if (x < clip_rect.x || y < clip_rect.y || x >= clip_rect.x + clip_rect.w ||
		    y >= clip_rect.y + clip_rect.h) {
			return;
		}
	}

	uint32_t pixel = color_to_pixel(color);

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
		display_buffer[index + 0] = (pixel >> 24) & 0xFF; // A
		display_buffer[index + 1] = (pixel >> 16) & 0xFF; // R
		display_buffer[index + 2] = (pixel >> 8) & 0xFF;  // G
		display_buffer[index + 3] = pixel & 0xFF;         // B
		break;
	}

	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565: {
		int index = (y * DISPLAY_WIDTH + x) * 2;
		display_buffer[index + 0] = (pixel >> 8) & 0xFF;
		display_buffer[index + 1] = pixel & 0xFF;
		break;
	}

	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10: {
		uint8_t bit;
		uint8_t *buf;
		if (pixel) {
			if (display_caps.screen_info & SCREEN_INFO_MONO_VTILED) {
				buf = display_buffer + x + y / 8 * DISPLAY_WIDTH;

				if (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) {
					bit = 7 - y % 8;
				} else {
					bit = y % 8;
				}
			} else {
				buf = display_buffer + x / 8 + y * DISPLAY_WIDTH / 8;

				if (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) {
					bit = 7 - x % 8;
				} else {
					bit = x % 8;
				}
			}
			if (display_caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
				*buf |= BIT(bit);
			} else {
				*buf &= ~BIT(bit);
			}
		}

		break;
	}

	case PIXEL_FORMAT_L_8: {
		display_buffer[y * DISPLAY_WIDTH + x] = pixel & 0xFF;
		break;
	}
	}
}

static __always_inline void draw_char(char c, int x, int y, const struct Font *font, mu_Color color)
{
	const struct FontGlyph *glyph = find_glyph(font, (uint32_t)c);
	if (!glyph) {
		return;
	}

	for (int row = 0; row < font->height; row++) {
		if (font->bitmap_width <= 8) {
			uint8_t row_data = glyph->bitmap[row];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x80 >> col)) {
					set_pixel(x + col, y + row, color);
				}
			}
		} else if (font->bitmap_width <= 16) {
			uint16_t row_data =
				(glyph->bitmap[row * 2] << 8) | glyph->bitmap[row * 2 + 1];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x8000 >> col)) {
					set_pixel(x + col, y + row, color);
				}
			}
		} else {
			uint32_t row_data = (glyph->bitmap[row * 4] << 24) |
					    (glyph->bitmap[row * 4 + 1] << 16) |
					    (glyph->bitmap[row * 4 + 2] << 8) |
					    glyph->bitmap[row * 4 + 3];
			for (int col = 0; col < glyph->width && col < font->bitmap_width; col++) {
				if (row_data & (0x80000000 >> col)) {
					set_pixel(x + col, y + row, color);
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
	for (int y = rect.y; y < rect.y + rect.h; y++) {
		for (int x = rect.x; x < rect.x + rect.w; x++) {
			set_pixel(x, y, color);
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

	while (*text) {
		const struct FontGlyph *glyph = find_glyph(font, (uint32_t)*text);
		if (glyph) {
			draw_char(*text, x, pos.y, font, color);
			x += glyph->width + font->char_spacing;
		} else {
			x += font->default_width + font->char_spacing;
		}
		text++;
	}
}

static void renderer_draw_icon(int id, mu_Rect rect, mu_Color color)
{
}

static int renderer_get_text_width(mu_Font f, const char *text, int len)
{
	int width = 0;
	int char_count = 0;
	const struct Font *font = (const struct Font *)f;

	if (!font) {
		LOG_WRN_ONCE("Font is NULL, returning width 0");
		return 0;
	}

	if (len == -1) {
		len = strlen(text);
	}

	while (char_count < len) {
		const struct FontGlyph *glyph = find_glyph(font, (uint32_t)*text);
		if (glyph) {
			width += glyph->width;
		} else {
			width += font->default_width;
		}
		char_count++;
		text++;
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

	if (clip_rect.x < 0) {
		clip_rect.w += clip_rect.x;
		clip_rect.x = 0;
	}

	if (clip_rect.y < 0) {
		clip_rect.h += clip_rect.y;
		clip_rect.y = 0;
	}

	if (clip_rect.x + clip_rect.w > DISPLAY_WIDTH) {
		clip_rect.w = DISPLAY_WIDTH - clip_rect.x;
	}

	if (clip_rect.y + clip_rect.h > DISPLAY_HEIGHT) {
		clip_rect.h = DISPLAY_HEIGHT - clip_rect.y;
	}
}

static void renderer_clear(mu_Color color)
{
	for (int y = 0; y < DISPLAY_HEIGHT; y++) {
		for (int x = 0; x < DISPLAY_WIDTH; x++) {
			set_pixel(x, y, color);
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

void mu_set_bg_color(mu_Color color)
{
	bg_color = color;
}

mu_Context *mu_get_context(void)
{
	return &mu_ctx;
}

struct k_work_q *mu_get_work_queue(void)
{
	return &mu_work_queue;
}

static void microui_loop_work(struct k_work *work)
{
	int64_t current_time = k_uptime_get();

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
		}
	}
	renderer_present();

	int64_t render_time = k_uptime_get() - current_time;
	int64_t wait_time = CONFIG_MICROUI_DISPLAY_REFRESH_PERIOD - render_time;

	if (wait_time < 0) {
		wait_time = 0;
	}

	k_work_schedule_for_queue(&mu_work_queue, k_work_delayable_from_work(work),
				  K_MSEC(wait_time));
}

static K_WORK_DELAYABLE_DEFINE(mu_loop_work, microui_loop_work);

static int microui_init(void)
{
	renderer_init();

	mu_init(&mu_ctx);
	mu_ctx.text_width = renderer_get_text_width;
	mu_ctx.text_height = renderer_get_text_height;

	k_work_queue_init(&mu_work_queue);
	k_work_queue_start(&mu_work_queue, mu_work_stack, K_KERNEL_STACK_SIZEOF(mu_work_stack),
			   CONFIG_MICROUI_EVENT_LOOP_THREAD_PRIORITY, NULL);
	k_work_submit_to_queue(&mu_work_queue, &mu_loop_work.work);

	return 0;
}

SYS_INIT(microui_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
