/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <microui/zmu.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(microui_input, LOG_LEVEL_INF);

#define TOUCH_DEV                                                                                  \
	COND_CODE_1(DT_HAS_CHOSEN(zephyr_touch), DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), NULL)

struct microui_input {
	uint16_t x;
	uint16_t y;

	uint8_t mouse_button;
	uint8_t down: 1;
	uint8_t up: 1;
};

K_MSGQ_DEFINE(input_events, sizeof(struct microui_input), 12, 4);

static struct microui_input pending_input;
static bool pending_input_valid = false;

bool mu_handle_input_events(void)
{
	struct microui_input input_evt;
	mu_Context *mu_ctx = mu_get_context();
	bool events_handled = false;

	/* If a button event was deferred last frame, handle it now so it becomes
	   visible to mu_begin() in this frame. */
	if (pending_input_valid) {
		if (pending_input.down) {
			mu_input_mousedown(mu_ctx, pending_input.x, pending_input.y,
					   pending_input.mouse_button);
		} else if (pending_input.up) {
			mu_input_mouseup(mu_ctx, pending_input.x, pending_input.y,
					 pending_input.mouse_button);
		}
		pending_input_valid = false;
		events_handled = true;
	}

	/* Drain incoming events. We always apply moves immediately so the
	   pointer is up-to-date. For a button event we apply only the move and
	   defer the button change until the next call of this function. */
	while (k_msgq_get(&input_events, &input_evt, K_NO_WAIT) == 0) {
		mu_input_mousemove(mu_ctx, input_evt.x, input_evt.y);
		events_handled = true;

		if (input_evt.down || input_evt.up) {
			/* Defer button event till next loop iteration (so it happens after
			   a frame has run with the new mouse position). */
			pending_input = input_evt;
			pending_input_valid = true;
			break;
		}
	}

	return events_handled;
}

static void input_callback(struct input_event *event, void *user_data)
{
	static bool mouse_pressed = false;
	static struct microui_input mu_input;

	switch (event->code) {
	case INPUT_ABS_X:
		mu_input.x = event->value;
		break;
	case INPUT_ABS_Y:
		mu_input.y = event->value;
		break;
	case INPUT_BTN_TOUCH:
		mu_input.mouse_button = MU_MOUSE_LEFT;

		if (!mouse_pressed) {
			if (event->value) {
				mu_input.down = true;
				mu_input.up = false;
			} else {
				mu_input.down = false;
				mu_input.up = false;
			}
		} else {
			if (event->value) {
				mu_input.down = false;
				mu_input.up = false;
			} else {
				mu_input.down = false;
				mu_input.up = true;
			}
		}
		mouse_pressed = event->value;
		break;
	}

	if (!event->sync) {
		return;
	}

	k_msgq_put(&input_events, &mu_input, K_FOREVER);
}

INPUT_CALLBACK_DEFINE(TOUCH_DEV, input_callback, NULL);
