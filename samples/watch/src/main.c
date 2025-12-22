/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <microui/zmu.h>
#include <microui/microui.h>
#include <microui/font.h>
#include <microui/image.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(microui_watchface, LOG_LEVEL_INF);


#define DISPLAY_NODE            DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH           DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT          DT_PROP(DISPLAY_NODE, height)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Screen types */
enum screen_type {
	SCREEN_WATCHFACE,
	SCREEN_MUSIC_PLAYER,
	SCREEN_COUNT
};

static enum screen_type current_screen = SCREEN_WATCHFACE;

/* Watchface state */
static int battery_percent = 100;
static uint32_t current_steps = 3500;
#define DAILY_STEP_GOAL 10000

/* VSCode Like Theme */

#define WINDOW_BG_COLOR        mu_color(0x1F, 0x1F, 0x1F, 0xFF)

/* Music player button colors */
#define BTN_CIRCLE_NORMAL      mu_color(0x3A, 0x3A, 0x3A, 0xFF)
#define BTN_CIRCLE_HOVER       mu_color(0x50, 0x50, 0x50, 0xFF)
#define BTN_CIRCLE_PRESSED     mu_color(0x28, 0x28, 0x28, 0xFF)
#define BTN_ICON_NORMAL        mu_color(0xE0, 0xE0, 0xE0, 0xFF)
#define BTN_ICON_HOVER         mu_color(0xFF, 0xFF, 0xFF, 0xFF)
#define BTN_ICON_PRESSED       mu_color(0xA0, 0xA0, 0xA0, 0xFF)


MU_FONT_DECLARE(montserrat_14);
MU_FONT_DECLARE(montserrat_32);

MU_IMAGE_DECLARE(album0)
MU_IMAGE_DECLARE(album1)
MU_IMAGE_DECLARE(album2)
MU_IMAGE_DECLARE(album3)
MU_IMAGE_DECLARE(music)
MU_IMAGE_DECLARE(back)

struct Song {
    const char* title;
    const char* artist;
    mu_Image album_art;
    mu_Color visualizer_color;
};

struct MusicPlayerState {
    struct Song* playlist;
    uint32_t playlist_size;
    uint8_t current_index;
    bool is_playing;
};

static struct Song playlist[] = {
    { .title = "Midnight Overdrive", .artist = "Neon Drift", .album_art = (mu_Image)&album0,
      .visualizer_color = { 0xFF, 0x00, 0x6E, 0xFF } },  /* Hot pink / neon magenta */
    { .title = "Polar Pulse", .artist = "Aurora Circuit", .album_art = (mu_Image)&album1,
      .visualizer_color = { 0x00, 0xD4, 0xFF, 0xFF } },  /* Electric cyan / aurora blue */
    { .title = "Copper Skies", .artist = "Velvet Engine", .album_art = (mu_Image)&album2,
      .visualizer_color = { 0xFF, 0x8C, 0x42, 0xFF } },  /* Copper orange */
    { .title = "Gravity Garden", .artist = "Solar Bloom", .album_art = (mu_Image)&album3,
      .visualizer_color = { 0x7B, 0xFF, 0x00, 0xFF } },  /* Bright lime green */
};

static struct MusicPlayerState music_player = {
    .playlist = playlist,
    .playlist_size = ARRAY_SIZE(playlist),
    .current_index = 0,
    .is_playing = false,
};

/* Pre-computed audio wave pattern for visualizer (64 samples, values 0.0 - 1.0) */
static const float audio_wave_pattern[] = {
    0.42f, 0.44f, 0.47f, 0.51f, 0.56f, 0.61f, 0.66f, 0.71f,
    0.75f, 0.79f, 0.83f, 0.86f, 0.89f, 0.91f, 0.92f, 0.93f,
    0.92f, 0.91f, 0.89f, 0.87f, 0.84f, 0.80f, 0.76f, 0.72f,
    0.68f, 0.64f, 0.60f, 0.57f, 0.54f, 0.52f, 0.50f, 0.49f,
    0.48f, 0.49f, 0.51f, 0.54f, 0.58f, 0.62f, 0.67f, 0.72f,
    0.77f, 0.81f, 0.85f, 0.88f, 0.90f, 0.92f, 0.93f, 0.93f,
    0.92f, 0.90f, 0.88f, 0.85f, 0.82f, 0.78f, 0.74f, 0.70f,
    0.66f, 0.62f, 0.59f, 0.56f, 0.53f, 0.51f, 0.49f, 0.48f,

    0.47f, 0.49f, 0.52f, 0.56f, 0.60f, 0.65f, 0.70f, 0.74f,
    0.78f, 0.82f, 0.85f, 0.88f, 0.90f, 0.91f, 0.92f, 0.92f,
    0.91f, 0.89f, 0.86f, 0.83f, 0.80f, 0.76f, 0.72f, 0.68f,
    0.64f, 0.60f, 0.57f, 0.54f, 0.52f, 0.50f, 0.49f, 0.48f,
    0.47f, 0.48f, 0.50f, 0.53f, 0.57f, 0.61f, 0.66f, 0.71f,
    0.76f, 0.80f, 0.84f, 0.87f, 0.89f, 0.91f, 0.92f, 0.92f,
    0.91f, 0.89f, 0.87f, 0.84f, 0.81f, 0.77f, 0.73f, 0.69f,
    0.65f, 0.61f, 0.58f, 0.55f, 0.52f, 0.50f, 0.48f, 0.46f
};
#define AUDIO_WAVE_PATTERN_SIZE ARRAY_SIZE(audio_wave_pattern)

/* Symbol drawing types for circular buttons */
enum button_symbol {
	SYMBOL_PLAY,
	SYMBOL_PAUSE,
	SYMBOL_NEXT,
	SYMBOL_PREV
};

static void draw_play_symbol(mu_Context *ctx, mu_Rect r, int size, mu_Color color)
{
	int cx = r.x + r.w / 2;
	int cy = r.y + r.h / 2;
	int half = size / 2;
	mu_Vec2 p0 = mu_vec2(cx - half / 2, cy - half);
	mu_Vec2 p1 = mu_vec2(cx - half / 2, cy + half);
	mu_Vec2 p2 = mu_vec2(cx + half, cy);
	mu_draw_triangle(ctx, p0, p1, p2, color);
}

static void draw_pause_symbol(mu_Context *ctx, mu_Rect r, int size, mu_Color color)
{
	int cx = r.x + r.w / 2;
	int cy = r.y + r.h / 2;
	int bar_w = size / 4;
	int bar_h = size;
	int gap = size / 4;
	
	mu_draw_rect(ctx, mu_rect(cx - gap - bar_w, cy - bar_h / 2, bar_w, bar_h), color);
	mu_draw_rect(ctx, mu_rect(cx + gap, cy - bar_h / 2, bar_w, bar_h), color);
}

static void draw_next_symbol(mu_Context *ctx, mu_Rect r, int size, mu_Color color)
{
	int cx = r.x + r.w / 2;
	int cy = r.y + r.h / 2;
	int half = size / 2;
	int bar_w = size / 6;
	
	int tri_offset = -bar_w;
	mu_Vec2 p0 = mu_vec2(cx - half / 2 + tri_offset, cy - half);
	mu_Vec2 p1 = mu_vec2(cx - half / 2 + tri_offset, cy + half);
	mu_Vec2 p2 = mu_vec2(cx + half / 2 + tri_offset, cy);
	mu_draw_triangle(ctx, p0, p1, p2, color);
	
	mu_draw_rect(ctx, mu_rect(cx + half / 2 + bar_w / 2, cy - half, bar_w, size), color);
}

static void draw_prev_symbol(mu_Context *ctx, mu_Rect r, int size, mu_Color color)
{
	int cx = r.x + r.w / 2;
	int cy = r.y + r.h / 2;
	int half = size / 2;
	int bar_w = size / 6;
	
	mu_draw_rect(ctx, mu_rect(cx - half / 2 - bar_w - bar_w / 2, cy - half, bar_w, size), color);
	
	int tri_offset = bar_w;
	mu_Vec2 p0 = mu_vec2(cx + half / 2 + tri_offset, cy - half);
	mu_Vec2 p1 = mu_vec2(cx + half / 2 + tri_offset, cy + half);
	mu_Vec2 p2 = mu_vec2(cx - half / 2 + tri_offset, cy);
	mu_draw_triangle(ctx, p0, p1, p2, color);
}

static int mu_circular_button(mu_Context *ctx, const void *id_data, int id_size,
			      enum button_symbol symbol, int radius)
{
	int res = 0;
	mu_Id id = mu_get_id(ctx, id_data, id_size);
	mu_Rect r = mu_layout_next(ctx);

	int cx = r.x + r.w / 2;
	int cy = r.y + r.h / 2;

	/* Hit rect is the circular area (approximated by square for simplicity) */
	mu_Rect hit_rect = mu_rect(cx - radius, cy - radius, radius * 2, radius * 2);

	mu_update_control(ctx, id, hit_rect, 0);

	if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
		res |= MU_RES_SUBMIT;
	}

	mu_Color bg_color, icon_color;
	if (ctx->focus == id && ctx->mouse_down == MU_MOUSE_LEFT) {
		bg_color = BTN_CIRCLE_PRESSED;
		icon_color = BTN_ICON_PRESSED;
	} else if (ctx->hover == id) {
		bg_color = BTN_CIRCLE_HOVER;
		icon_color = BTN_ICON_HOVER;
	} else {
		bg_color = BTN_CIRCLE_NORMAL;
		icon_color = BTN_ICON_NORMAL;
	}

	mu_draw_circle(ctx, mu_vec2(cx, cy), radius, bg_color);

	int symbol_size = DIV_ROUND_UP(radius * 2, 3);
	switch (symbol) {
	case SYMBOL_PLAY:
		draw_play_symbol(ctx, r, symbol_size, icon_color);
		break;
	case SYMBOL_PAUSE:
		draw_pause_symbol(ctx, r, symbol_size, icon_color);
		break;
	case SYMBOL_NEXT:
		draw_next_symbol(ctx, r, symbol_size, icon_color);
		break;
	case SYMBOL_PREV:
		draw_prev_symbol(ctx, r, symbol_size, icon_color);
		break;
	}

	return res;
}

static int mu_icon_button(mu_Context *ctx, const void *id_data, int id_size, mu_Image icon)
{
	int res = 0;
	mu_Id id = mu_get_id(ctx, id_data, id_size);
	mu_Rect r = mu_layout_next(ctx);

	int icon_w = 0, icon_h = 0;
	mu_get_img_dimensions(icon, &icon_w, &icon_h);

	int icon_x = r.x + (r.w - icon_w) / 2;
	int icon_y = r.y + (r.h - icon_h) / 2;

	mu_Rect hit_rect = mu_rect(icon_x, icon_y, icon_w, icon_h);

	mu_update_control(ctx, id, hit_rect, 0);

	if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
		res |= MU_RES_SUBMIT;
	}

	mu_draw_image(ctx, mu_vec2(icon_x, icon_y), icon);

	return res;
}

static void draw_battery_icon(mu_Context *ctx, int x, int y, int percent)
{
	int batt_w = 24;
	int batt_h = 12;
	int tip_w = 3;
	int tip_h = 6;
	int border = 2;

	mu_draw_box(ctx, mu_rect(x, y, batt_w, batt_h), mu_color(200, 200, 200, 255));

	int tip_y = y + (batt_h - tip_h) / 2;
	mu_draw_rect(ctx, mu_rect(x + batt_w, tip_y, tip_w, tip_h), mu_color(200, 200, 200, 255));

	mu_Color fill_color;
	if (percent > 50) {
		fill_color = mu_color(0, 200, 0, 255);
	} else if (percent > 20) {
		fill_color = mu_color(255, 165, 0, 255);
	} else {
		fill_color = mu_color(255, 50, 50, 255);
	}

	int fill_max_w = batt_w - border * 2;
	int fill_w = (fill_max_w * percent) / 100;
	if (fill_w > 0) {
		mu_draw_rect(ctx, mu_rect(x + border, y + border, fill_w, batt_h - border * 2), fill_color);
	}
}

static void draw_watchface(mu_Context *ctx)
{
	if (current_screen != SCREEN_WATCHFACE) {
		return;
	}

	if (mu_begin_window_ex(ctx, "Watchface",
			       mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT),
			       MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOSCROLL)) {

		mu_Container *win = mu_get_current_container(ctx);
		int center_x = win->body.x + win->body.w / 2;
		int center_y = win->body.y + win->body.h / 2;
		int radius = (win->body.w < win->body.h ? win->body.w : win->body.h) / 2 - 5;

		float step_progress = (float)current_steps / (float)DAILY_STEP_GOAL;
		if (step_progress > 1.0f) {
			step_progress = 1.0f;
		}

		float start_angle = -90.0f;
		float end_angle = start_angle + (360.0f * step_progress);

		mu_Color arc_color;
		if (step_progress >= 1.0f) {
			arc_color = mu_color(0, 255, 100, 255);
		} else if (step_progress >= 0.5f) {
			arc_color = mu_color(100, 200, 255, 255);
		} else {
			arc_color = mu_color(255, 150, 50, 255);
		}

		if (current_steps > 0) {
			mu_draw_arc(ctx, mu_vec2(center_x, center_y), radius, 6,
				    start_angle, end_angle, arc_color);
		}

		uint32_t uptime_sec = k_uptime_get_32() / 1000;
		int mock_hour = (10 + (uptime_sec / 3600)) % 24;
		int mock_min = (30 + (uptime_sec / 60)) % 60;

		char time_str[16];
		snprintf(time_str, sizeof(time_str), "%02d:%02d", mock_hour, mock_min);

		mu_Font old_font = ctx->style->font;
		mu_set_font(ctx, &montserrat_32);

		int text_w = ctx->text_width(ctx->style->font, time_str, -1);
		int text_h = ctx->text_height(ctx->style->font);

		int batt_x = center_x + 50;
		int batt_y = center_y - text_h / 2 - 50;
		draw_battery_icon(ctx, batt_x, batt_y, battery_percent);

		mu_Vec2 text_pos = mu_vec2(center_x - text_w / 2, center_y - text_h / 2);
		mu_draw_text(ctx, ctx->style->font, time_str, -1, text_pos,
			     ctx->style->colors[MU_COLOR_TEXT]);

		mu_set_font(ctx, old_font);

		int btn_size = 32;
		int btn_spacing = 20;
		int total_width = btn_size * 2 + btn_spacing;
		int btn_row_x = center_x - total_width / 2;
		int btn_row_y = center_y + radius - btn_size - 20;

		mu_layout_row(ctx, 2, (int[]){btn_size, btn_size}, btn_size);

		mu_layout_set_next(ctx, mu_rect(btn_row_x, btn_row_y, btn_size, btn_size), 0);
		if (mu_icon_button(ctx, "music", 5, (mu_Image)&music)) {
			current_screen = SCREEN_MUSIC_PLAYER;
		}

		mu_layout_set_next(ctx, mu_rect(btn_row_x + btn_size + btn_spacing, btn_row_y, btn_size, btn_size), 0);
		mu_layout_next(ctx);

		mu_end_window(ctx);
	}

	static uint32_t last_battery_update = 0;
	uint32_t now = k_uptime_get_32();
	if (now - last_battery_update > 10000) {
		last_battery_update = now;
		if (battery_percent > 0) {
			battery_percent--;
		}
	}

	static uint32_t last_step_update = 0;
	uint32_t now_steps = k_uptime_get_32();
	if (now_steps - last_step_update > 5000) {
		last_step_update = now_steps;
		current_steps += 150;
		current_steps %= DAILY_STEP_GOAL;
	}
}

void draw_music_player(mu_Context* ctx)
{
	if (current_screen != SCREEN_MUSIC_PLAYER) {
		return;
	}

	if (mu_begin_window_ex(ctx, "MusicPlayer",
			       mu_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT),
			       MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOSCROLL)) {

		struct Song *current_song = &music_player.playlist[music_player.current_index];
		mu_Container *win = mu_get_current_container(ctx);
		int padding = ctx->style->padding;
		int content_width = win->body.w - padding * 2;

		int back_btn_size = 24;
		int center_x = DISPLAY_WIDTH / 2;
		int radius = DISPLAY_WIDTH / 2;
		int back_btn_x = center_x - radius / 2 - 12;
		int back_btn_y = 25;
		mu_layout_row(ctx, 1, (int[]){back_btn_size}, back_btn_size);
		mu_layout_set_next(ctx, mu_rect(back_btn_x, back_btn_y, back_btn_size, back_btn_size), 0);
		if (mu_icon_button(ctx, "back", 4, (mu_Image)&back)) {
			current_screen = SCREEN_WATCHFACE;
		}

		int content_start_y = back_btn_y + back_btn_size;
		int top_margin = content_start_y - win->body.y - padding;
		mu_layout_row(ctx, 1, (int[]){-1}, top_margin);
		mu_layout_next(ctx);

		int artist_height = ctx->text_height(ctx->style->font) + padding;
		mu_layout_row(ctx, 1, (int[]){-1}, artist_height);
		mu_draw_control_text(ctx, current_song->artist, mu_layout_next(ctx),
				     MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);

		int album_w = 0, album_h = 0;
		mu_get_img_dimensions(current_song->album_art, &album_w, &album_h);

		int bar_height = 4;
		int bar_gap = 2;
		int bar_stride = bar_height + bar_gap;
		int num_bars = (album_h + bar_gap) / bar_stride;

		int album_left_padding = content_width / 6;
		int visualizer_width = content_width - album_left_padding - album_w - padding;

		mu_layout_row(ctx, 3, (int[]){album_left_padding, album_w, visualizer_width}, album_h);

		mu_layout_next(ctx);

		mu_Rect album_rect = mu_layout_next(ctx);
		mu_draw_image(ctx, mu_vec2(album_rect.x, album_rect.y), current_song->album_art);

		mu_Rect viz_rect = mu_layout_next(ctx);

		int viz_bottom_y = album_rect.y + album_h;

		uint32_t time_ms = k_uptime_get_32();
		int wave_offset = (time_ms / 50) % AUDIO_WAVE_PATTERN_SIZE;

		for (int i = 0; i < num_bars; i++) {
			int bar_y = viz_bottom_y - (num_bars - i) * bar_height - (num_bars - 1 - i) * bar_gap;

			int bar_width;
			if (music_player.is_playing) {
				int wave_index = (wave_offset + i * 3) % AUDIO_WAVE_PATTERN_SIZE;
				float wave_value = audio_wave_pattern[wave_index];
				bar_width = (int)(viz_rect.w * wave_value);
			} else {
				bar_width = viz_rect.w * 15 / 100;
			}

			mu_draw_rect(ctx, mu_rect(viz_rect.x, bar_y, bar_width, bar_height), current_song->visualizer_color);
		}

		int title_height = ctx->text_height(ctx->style->font) + padding * 2;
		mu_layout_row(ctx, 1, (int[]){-1}, title_height);
		mu_draw_control_text(ctx, current_song->title, mu_layout_next(ctx),
				     MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);

		int button_size = 30;
		int layout_spacing = ctx->style->spacing;
		int button_radius = (button_size - layout_spacing) / 2;
		int button_gap = 15;
		int total_buttons_width = 3 * button_size + 2 * button_gap + 6 * layout_spacing;
		int side_padding = (content_width - total_buttons_width) / 2;
		mu_layout_row(ctx, 7,
			      (int[]){side_padding, button_size, button_gap, button_size, button_gap, button_size, side_padding},
			      button_size);

		mu_layout_next(ctx);

		if (mu_circular_button(ctx, "prev", 4, SYMBOL_PREV, button_radius)) {
			if (music_player.current_index > 0) {
				music_player.current_index--;
			} else {
				music_player.current_index = music_player.playlist_size - 1;
			}
		}

		mu_layout_next(ctx);

		enum button_symbol play_pause_symbol = music_player.is_playing ?
						       SYMBOL_PAUSE : SYMBOL_PLAY;
		if (mu_circular_button(ctx, "play_pause", 10, play_pause_symbol, button_radius)) {
			music_player.is_playing = !music_player.is_playing;
		}

		mu_layout_next(ctx);

		if (mu_circular_button(ctx, "next", 4, SYMBOL_NEXT, button_radius)) {
			music_player.current_index =
				(music_player.current_index + 1) % music_player.playlist_size;
		}

		mu_end_window(ctx);
	}
}

void process_frame(mu_Context *ctx)
{
	mu_begin(ctx);

	draw_watchface(ctx);
	draw_music_player(ctx);

	mu_end(ctx);
}

int main(int argc, char **argv)
{
#if DT_HAS_ALIAS(backlight_pwm)
	struct pwm_dt_spec backlight = PWM_DT_SPEC_GET_OR(DT_ALIAS(backlight_pwm), {0});

	if(!pwm_is_ready_dt(&backlight)) {
		return 0;
	}

	pwm_set_dt(&backlight, backlight.period, backlight.period / 3);
#endif /* DT_HAS_ALIAS(backlight_pwm) */

	mu_setup(process_frame);

	mu_Context *ctx = mu_get_context();
    mu_set_font(ctx, &montserrat_14);

    ctx->style->colors[MU_COLOR_WINDOWBG] = WINDOW_BG_COLOR;

#ifdef CONFIG_MICROUI_EVENT_LOOP
	mu_event_loop_start();
#else
	while (true) {
		mu_handle_tick();
		k_sleep(K_MSEC(16));
	}
#endif
	return 0;
}
