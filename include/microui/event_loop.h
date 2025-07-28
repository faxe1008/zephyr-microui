#ifndef ZEPHYR_MODULES_MICROUI_RENDERER_H_
#define ZEPHYR_MODULES_MICROUI_RENDERER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "microui.h"
#include <zephyr/kernel.h>

/**
 * @brief Get the current MicroUI context.
 * This function retrieves the current MicroUI context, which is used for rendering and managing the UI state.
 *
 * @return A pointer to the current `mu_Context` structure.
 */
mu_Context* mu_get_context(void);


/**
 * @brief Get the work queue used by MicroUI.
 * This function retrieves the work queue that MicroUI uses for scheduling tasks.
 * 
 * @return A pointer to the `k_work_q` structure used by MicroUI.
 */
struct k_work_q* mu_get_work_queue(void);

/**
 * @brief Set the background color for MicroUI.
 * This function sets the background color for the MicroUI context.
 *
 * @param color The color to set as the background.
 */
void mu_set_bg_color(mu_Color color);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_RENDERER_H_ */
