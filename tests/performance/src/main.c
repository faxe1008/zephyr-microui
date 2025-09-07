#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/display.h>
#include <microui/event_loop.h>
#include "montserrat_12.h"

#define DISPLAY_NODE   DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH  DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT DT_PROP(DISPLAY_NODE, height)

#define RENDER_COUNT 100

static const struct device *display_dev = DEVICE_DT_GET(DISPLAY_NODE);
static uint32_t frame_count = 0;
static void render_suite_before(void *f)
{
	frame_count = 0;
	display_set_pixel_format(display_dev, BIT(CONFIG_DUMMY_DISPLAY_COLOR_FORMAT));
}

void perf_basic_text_rendering(mu_Context *ctx)
{
	mu_begin(ctx);
	if (mu_begin_window(ctx, "Style Editor", mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))) {
		mu_layout_row(ctx, 2, (int[]){50 + (frame_count % 50), -1}, 0);
		mu_label(ctx,
			 "Lorem ipsum dolor sit amet, consetetur sadipscing elitr,\n"
			 "sed diam nonumy eirmod tempor invidunt ut labore et dolore\n"
			 "magna aliquyam erat, sed diam voluptua. At vero eos et accusam\n"
			 "et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea\n"
			 "takimata sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor\n"
			 "sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor\n");
		mu_end_window(ctx);
	}
	mu_end(ctx);
	frame_count++;
}

ZTEST(microui_render, test_text_render)
{
	mu_setup(perf_basic_text_rendering);
	mu_Context *ctx = mu_get_context();
	mu_set_font(ctx, &font);

	for (int i = 0; i < RENDER_COUNT; i++) {
		mu_handle_tick();
	}
}

void perf_basic_recolor(mu_Context *ctx)
{
	mu_begin(ctx);
	if (mu_begin_window(ctx, "Style Editor", mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))) {
		mu_layout_row(ctx, 2, (int[]){50, -1}, 0);
		ctx->style->colors[MU_COLOR_BUTTON].r = frame_count % 255;
		ctx->style->colors[MU_COLOR_BUTTON].g = (frame_count * 2) % 255;
		ctx->style->colors[MU_COLOR_BUTTON].b = (frame_count * 2) % 255;
		ctx->style->colors[MU_COLOR_BUTTON].a = (frame_count * 4) % 255;
		mu_button_ex(ctx, "Hello World!", MU_ICON_CLOSE, 0);
		mu_end_window(ctx);
	}
	mu_end(ctx);
	frame_count++;
}

ZTEST(microui_render, test_recoloring)
{
	mu_setup(perf_basic_recolor);

	for (int i = 0; i < RENDER_COUNT; i++) {
		mu_handle_tick();
	}
}

ZTEST_SUITE(microui_render, NULL, NULL, render_suite_before, NULL, NULL);
