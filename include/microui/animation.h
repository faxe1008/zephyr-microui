/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file animation.h
 * @brief MicroUI Immediate-Mode Animation System
 *
 * Provides a lightweight animation framework that follows MicroUI's
 * immediate-mode paradigm. Animation state is cached internally by ID,
 * allowing you to simply call animation functions inline each frame.
 *
 * @note Animation state is stored in a fixed-size pool within mu_Context.
 *       Unused animations are automatically garbage collected after a
 *       configurable number of frames.
 */

#ifndef ZEPHYR_MODULES_MICROUI_ANIMATION_H_
#define ZEPHYR_MODULES_MICROUI_ANIMATION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <microui/microui.h>

/**
 * @brief Animation ID type (alias for mu_Id)
 *
 * Used to uniquely identify animations in the animation pool.
 * Create using mu_anim_id("name") or ensure uniqueness manually.
 */
typedef mu_Id mu_AnimId;

/**
 * @brief Create an animation ID from a string
 *
 * Computes a hash of the string to use as an animation identifier.
 *
 * @param name String to use as the animation identifier
 * @return mu_AnimId that can be passed to animation functions
 *
 * @code
 * mu_AnimId id = mu_anim_id("my_animation");
 * mu_Real val = mu_anim(ctx, id, 0.0, 100.0, 500, MU_EASE_OUT);
 *
 * // Or use inline
 * mu_Real x = mu_anim(ctx, mu_anim_id("move"), 0.0, 100.0, 300, MU_EASE_OUT);
 * @endcode
 */
mu_AnimId mu_anim_id(const char *name);

/**
 * @brief Built-in easing types for use with mu_anim()
 */
enum mu_easing {
	MU_EASE_LINEAR = 0,   /**< Linear interpolation */
	MU_EASE_IN,           /**< Ease in (slow start) */
	MU_EASE_OUT,          /**< Ease out (slow end) */
	MU_EASE_IN_OUT,       /**< Ease in-out (slow start and end) */
	MU_EASE_IN_QUAD,      /**< Quadratic ease in */
	MU_EASE_OUT_QUAD,     /**< Quadratic ease out */
	MU_EASE_IN_OUT_QUAD,  /**< Quadratic ease in-out */
	MU_EASE_IN_CUBIC,     /**< Cubic ease in */
	MU_EASE_OUT_CUBIC,    /**< Cubic ease out */
	MU_EASE_IN_OUT_CUBIC, /**< Cubic ease in-out */
	MU_EASE_OUT_ELASTIC,  /**< Elastic ease out (springy) */
	MU_EASE_OUT_BOUNCE,   /**< Bounce ease out */
	MU_EASE_IN_BACK,      /**< Back ease in (pulls back first) */
	MU_EASE_OUT_BACK,     /**< Back ease out (overshoots) */
	MU_EASE_MAX
};

/**
 * @brief Time-based animation with custom easing function.
 *
 * Animates from a start value to an end value over a fixed duration
 * using a custom easing function. The animation starts when first called
 * and progresses over the specified duration.
 *
 * @param ctx         MicroUI context
 * @param id          Animation ID (use MU_ANIM_ID("name"))
 * @param start       Starting value
 * @param end         Ending value
 * @param duration_ms Duration in milliseconds
 * @param easing      Easing function (use NULL for linear)
 * @param loop        Whether the animation should loop when finished
 *
 * @return Current interpolated value
 *
 * @code
 * // Using a custom easing function
 * mu_Real my_ease(mu_Real t) { return t * t * t; }
 * mu_Real val = mu_anim_ex(ctx, MU_ANIM_ID("custom"), 0.0, 100.0, 1000, my_ease, false);
 * @endcode
 */
mu_Real mu_anim_ex(mu_Context *ctx, mu_AnimId id, mu_Real start, mu_Real end, uint32_t duration_ms,
		   mu_EasingFunc easing, bool loop);

/**
 * @brief Time-based animation with built-in easing.
 *
 * Convenience wrapper around mu_anim_ex that selects a built-in easing
 * function based on the easing type enum.
 *
 * @param ctx         MicroUI context
 * @param id          Animation ID (use MU_ANIM_ID("name"))
 * @param start       Starting value
 * @param end         Ending value
 * @param duration_ms Duration in milliseconds
 * @param easing      Built-in easing type
 * @param loop        Whether the animation should loop when finished
 *
 * @return Current interpolated value
 *
 * @code
 * mu_Real alpha = mu_anim(ctx, MU_ANIM_ID("fade_in"), 0.0, 1.0, 500, MU_EASE_OUT, false);
 * @endcode
 */
mu_Real mu_anim(mu_Context *ctx, mu_AnimId id, mu_Real start, mu_Real end, uint32_t duration_ms,
		enum mu_easing easing, bool loop);

/**
 * @brief Check if an animation has completed.
 *
 * @param ctx MicroUI context
 * @param id  Animation ID (use MU_ANIM_ID("name"))
 *
 * @return true if the animation has finished, false otherwise
 */
bool mu_anim_done(mu_Context *ctx, mu_AnimId id);

/**
 * @brief Reset an animation to start again.
 *
 * @param ctx MicroUI context
 * @param id  Animation ID (use MU_ANIM_ID("name"))
 */
void mu_anim_reset(mu_Context *ctx, mu_AnimId id);

/**
 * @brief Calculate animation duration from speed (rate of change).
 *
 * This utility converts a speed value (units per second) into a duration
 * in milliseconds, which can then be used with mu_anim. This is useful
 * when you know the desired rate of change but not the duration.
 *
 * @param speed Speed in units per second (rate of change)
 * @param start Starting value
 * @param end   Ending value
 *
 * @return Duration in milliseconds
 *
 * @code
 * // Animate at 20 units per second
 * uint32_t duration = mu_anim_speed_to_time(20, 0, 100);  // Returns 5000ms
 * mu_Real val = mu_anim(ctx, MU_ANIM_ID("move"), 0.0, 100.0, duration, MU_EASE_OUT);
 * @endcode
 */
uint32_t mu_anim_speed_to_time(mu_Real speed, mu_Real start, mu_Real end);

/**
 * @brief Get the number of active animations in the pool.
 *
 * @param ctx MicroUI context
 *
 * @return Number of active animation slots
 */
int mu_anim_count(mu_Context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODULES_MICROUI_ANIMATION_H_ */
