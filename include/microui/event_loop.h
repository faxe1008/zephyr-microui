#ifndef ZEPHYR_MODULES_MICROUI_RENDERER_H_
#define ZEPHYR_MODULES_MICROUI_RENDERER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "microui.h"
#include <zephyr/kernel.h>

/**
 * @typedef mu_process_frame_cb
 * @brief Callback type for per-frame UI processing.
 *
 * This callback is invoked once per display frame by the MicroUI event loop.
 * It should call mu_begin/mu_end and build the UI for the current frame.
 *
 * @param ctx Pointer to the mu_Context that the callback must operate on.
 */
typedef void (*mu_process_frame_cb)(mu_Context *ctx);

/**
 * @brief Get the pointer to the global MicroUI context.
 *
 * Returns a pointer to the single global mu_Context used by the event loop.
 * The returned context is owned by the MicroUI subsystem; callers must not
 * free it. Use this to set fonts, styles, or to inspect state from other code.
 *
 * @return mu_Context* Pointer to the global context (never NULL after init).
 */
mu_Context *mu_get_context(void);

/**
 * @brief Initialize the MicroUI event loop subsystem.
 *
 * This must be called once before starting the event loop. It initializes the
 * renderer, the mu context and registers the provided per-frame callback.
 *
 * @param cb Function pointer called for each frame. Must not be NULL.
 *
 * @return int 0 on success, -EINVAL if cb is NULL, or other negative
 *             errno on initialization failure.
 *
 * @note After a successful call, use mu_event_loop_start() to begin the loop.
 * @see mu_event_loop_start, mu_event_loop_stop
 */
int mu_event_loop_init(mu_process_frame_cb cb);

/**
 * @brief Start the MicroUI event loop worker.
 *
 * Spawns/starts the work queue / worker thread that runs the periodic
 * render-and-event loop. The event loop will call the frame callback supplied
 * to mu_event_loop_init() on each refresh.
 *
 * @return int 0 on success, negative errno on failure.
 *
 * @note The function asserts that a frame callback has been registered;
 *       in production builds where asserts may be disabled you should still
 *       ensure mu_event_loop_init() returned success before calling this.
 * @see mu_event_loop_init, mu_event_loop_stop
 */
int mu_event_loop_start(void);

/**
 * @brief Stop the MicroUI event loop worker.
 *
 * Stops the worker thread/work-queue and blocks until the queue has stopped.
 * After this returns the frame callback will no longer be invoked.
 *
 * @return int 0 on success, negative errno on failure.
 *
 * @note Calling mu_event_loop_start() after a successful stop is allowed only
 *       if the underlying queue and state are re-initialized as required.
 */
int mu_event_loop_stop(void);

/**
 * @brief Poll and handle input events for MicroUI.
 *
 * This function reads raw input (buttons, touch, etc.) from the registered
 * input drivers, translates them into MicroUI events (mouse, keyboard,
 * touch) and feeds them into the mu_Context so the next frame sees them.
 *
 * @note This is intended to be called from the event loop worker; if you
 *       call it from another thread ensure any required synchronization.
 * @see mu_event_loop_start
 */
void mu_event_loop_handle_input_events(void);

/**
 * @brief Set the background color for MicroUI.
 *
 * This sets the color that will be used to clear the display before drawing
 * MicroUI commands each frame.
 *
 * @param color The color to set as the background (mu_Color).
 *
 * @note The change takes effect on the next frame render.
 */
void mu_set_bg_color(mu_Color color);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_RENDERER_H_ */
