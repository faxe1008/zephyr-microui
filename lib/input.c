#include <microui/event_loop.h>
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

static void process_input_events(struct k_work *work);

K_MSGQ_DEFINE(input_events, sizeof(struct microui_input), 12, 4);
K_WORK_DEFINE(input_work, process_input_events);

void process_input_events(struct k_work *work)
{
	struct microui_input input_evt;
	mu_Context *mu_ctx = mu_get_context();

	while (k_msgq_get(&input_events, &input_evt, K_NO_WAIT) == 0) {
		if (input_evt.up) {
			mu_input_mouseup(mu_ctx, input_evt.x, input_evt.y, input_evt.mouse_button);
		} else if (input_evt.down) {
			mu_input_mousedown(mu_ctx, input_evt.x, input_evt.y,
					   input_evt.mouse_button);
		} else {
			mu_input_mousemove(mu_ctx, input_evt.x, input_evt.y);
		}
	}
}

static void input_callback(struct input_event *event, void *user_data)
{
	static bool mouse_pressed = false;
	static struct microui_input mu_input;
	struct k_work_q *workq = mu_get_work_queue();

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
	k_work_submit_to_queue(workq, &input_work);
}

INPUT_CALLBACK_DEFINE(TOUCH_DEV, input_callback, NULL);
