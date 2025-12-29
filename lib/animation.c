/*
 * Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file animation.c
 * @brief Immediate-mode animation system for MicroUI
 *
 * This implementation follows MicroUI's immediate-mode paradigm where
 * animation state is cached internally using IDs. Simply call the animation
 * functions each frame with your target values, and they return the current
 * animated value.
 */

#include <microui/animation.h>
#include <microui/microui.h>
#include <zephyr/sys/util.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*
 * Internal easing functions
 */

static mu_Real ease_linear(mu_Real t)
{
	return t;
}

static mu_Real ease_in_quad(mu_Real t)
{
	return t * t;
}

static mu_Real ease_out_quad(mu_Real t)
{
	return t * (2.0f - t);
}

static mu_Real ease_in_out_quad(mu_Real t)
{
	if (t < 0.5f) {
		return 2.0f * t * t;
	}
	return -1.0f + (4.0f - 2.0f * t) * t;
}

static mu_Real ease_in_cubic(mu_Real t)
{
	return t * t * t;
}

static mu_Real ease_out_cubic(mu_Real t)
{
	mu_Real f = t - 1.0f;
	return f * f * f + 1.0f;
}

static mu_Real ease_in_out_cubic(mu_Real t)
{
	if (t < 0.5f) {
		return 4.0f * t * t * t;
	}
	mu_Real f = 2.0f * t - 2.0f;
	return 0.5f * f * f * f + 1.0f;
}

static mu_Real ease_in(mu_Real t)
{
	return 1.0f - cosf(t * M_PI / 2.0f);
}

static mu_Real ease_out(mu_Real t)
{
	return sinf(t * M_PI / 2.0f);
}

static mu_Real ease_in_out(mu_Real t)
{
	return 0.5f * (1.0f - cosf(M_PI * t));
}

static mu_Real ease_out_elastic(mu_Real t)
{
	if (t == 0.0f || t == 1.0f) {
		return t;
	}
	mu_Real p = 0.3f;
	mu_Real s = p / 4.0f;
	return powf(2.0f, -10.0f * t) * sinf((t - s) * (2.0f * M_PI) / p) + 1.0f;
}

static mu_Real ease_out_bounce(mu_Real t)
{
	if (t < 1.0f / 2.75f) {
		return 7.5625f * t * t;
	} else if (t < 2.0f / 2.75f) {
		t -= 1.5f / 2.75f;
		return 7.5625f * t * t + 0.75f;
	} else if (t < 2.5f / 2.75f) {
		t -= 2.25f / 2.75f;
		return 7.5625f * t * t + 0.9375f;
	} else {
		t -= 2.625f / 2.75f;
		return 7.5625f * t * t + 0.984375f;
	}
}

static mu_Real ease_in_back(mu_Real t)
{
	const mu_Real s = 1.70158f;
	return t * t * ((s + 1.0f) * t - s);
}

static mu_Real ease_out_back(mu_Real t)
{
	const mu_Real s = 1.70158f;
	t -= 1.0f;
	return t * t * ((s + 1.0f) * t + s) + 1.0f;
}

/**
 * @brief Easing function lookup table indexed by enum mu_easing
 */
static const mu_EasingFunc easing_funcs[] = {
	[MU_EASE_LINEAR] = ease_linear,
	[MU_EASE_IN] = ease_in,
	[MU_EASE_OUT] = ease_out,
	[MU_EASE_IN_OUT] = ease_in_out,
	[MU_EASE_IN_QUAD] = ease_in_quad,
	[MU_EASE_OUT_QUAD] = ease_out_quad,
	[MU_EASE_IN_OUT_QUAD] = ease_in_out_quad,
	[MU_EASE_IN_CUBIC] = ease_in_cubic,
	[MU_EASE_OUT_CUBIC] = ease_out_cubic,
	[MU_EASE_IN_OUT_CUBIC] = ease_in_out_cubic,
	[MU_EASE_OUT_ELASTIC] = ease_out_elastic,
	[MU_EASE_OUT_BOUNCE] = ease_out_bounce,
	[MU_EASE_IN_BACK] = ease_in_back,
	[MU_EASE_OUT_BACK] = ease_out_back,
};

BUILD_ASSERT(ARRAY_SIZE(easing_funcs) == MU_EASE_MAX, "Easing function lookup array size mismatch");

mu_AnimId mu_anim_id(const char *name)
{
	mu_AnimId hash = 2166136261u;
	while (*name) {
		hash ^= (unsigned char)*name++;
		hash *= 16777619u;
	}
	return hash;
}

static mu_AnimState *get_anim_state(mu_Context *ctx, mu_Id id, bool create)
{
	/* First, look for existing slot with this ID */
	int idx = mu_pool_get(ctx, ctx->anim_pool, MU_ANIM_POOL_SIZE, id);
	if (idx >= 0) {
		mu_pool_update(ctx, ctx->anim_pool, idx);
		return &ctx->anim_states[idx];
	}

	if (!create) {
		return NULL;
	}

	/* Not found, allocate a new slot */
	idx = mu_pool_init(ctx, ctx->anim_pool, MU_ANIM_POOL_SIZE, id);
	if (idx >= 0) {
		/* Initialize new state */
		mu_AnimState *state = &ctx->anim_states[idx];
		memset(state, 0, sizeof(mu_AnimState));
		return state;
	}

	/* Pool is full - find oldest entry and reuse it */
	int oldest_idx = -1;
	int oldest_frame = ctx->frame;
	for (int i = 0; i < MU_ANIM_POOL_SIZE; i++) {
		if (ctx->anim_pool[i].last_update < oldest_frame) {
			oldest_frame = ctx->anim_pool[i].last_update;
			oldest_idx = i;
		}
	}

	if (oldest_idx >= 0) {
		ctx->anim_pool[oldest_idx].id = id;
		mu_pool_update(ctx, ctx->anim_pool, oldest_idx);
		mu_AnimState *state = &ctx->anim_states[oldest_idx];
		memset(state, 0, sizeof(mu_AnimState));
		return state;
	}

	return NULL;
}

mu_Real mu_anim_ex(mu_Context *ctx, mu_AnimId id, mu_Real start, mu_Real end, uint32_t duration_ms,
		   mu_EasingFunc easing, bool loop)
{
	mu_AnimState *state = get_anim_state(ctx, id, true);

	if (!state) {
		return end;
	}

	/* Use linear if no easing provided */
	if (easing == NULL) {
		easing = ease_linear;
	}

	/* Initialize if this is a new animation or parameters changed */
	if (state->duration_ms != duration_ms || state->start != start || state->end != end ||
	    state->easing != easing || state->loop != loop) {
		state->start = start;
		state->end = end;
		state->duration_ms = duration_ms;
		state->start_time_ms = ctx->curr_time_ms;
		state->easing = easing;
		state->finished = 0;
		state->current = start;
		state->loop = loop ? 1 : 0;
	}

	/* Calculate progress */
	uint32_t elapsed = ctx->curr_time_ms - state->start_time_ms;
	mu_Real t = (duration_ms > 0) ? (mu_Real)elapsed / (mu_Real)duration_ms : 1.0f;

	if (t >= 1.0f) {
		t = 1.0f;
	}

	/* Apply easing and interpolate */
	mu_Real eased = state->easing(CLAMP(t, 0.0f, 1.0f));
	state->current = state->start + (state->end - state->start) * eased;

	/* Reset loop AFTER calculating the value, so we return end value first */
	if (t >= 1.0f) {
		if (state->loop) {
			state->start_time_ms = ctx->curr_time_ms;
		} else {
			state->finished = 1;
		}
	}

	return state->current;
}

mu_Real mu_anim(mu_Context *ctx, mu_AnimId id, mu_Real start, mu_Real end, uint32_t duration_ms,
		enum mu_easing easing, bool loop)
{
	mu_EasingFunc func =
		(easing < ARRAY_SIZE(easing_funcs)) ? easing_funcs[easing] : ease_linear;
	return mu_anim_ex(ctx, id, start, end, duration_ms, func, loop);
}

bool mu_anim_done(mu_Context *ctx, mu_AnimId id)
{
	mu_AnimState *state = get_anim_state(ctx, id, false);

	if (!state) {
		return true;
	}

	return state->finished != 0;
}

void mu_anim_reset(mu_Context *ctx, mu_AnimId id)
{
	mu_AnimState *state = get_anim_state(ctx, id, false);

	if (state) {
		state->start_time_ms = ctx->curr_time_ms;
		state->finished = 0;
		state->current = state->start;
	}
}

uint32_t mu_anim_speed_to_time(mu_Real speed, mu_Real start, mu_Real end)
{
	if (speed <= 0.0f) {
		return 0;
	}

	mu_Real distance = fabsf(end - start);
	mu_Real time_sec = distance / speed;

	return (uint32_t)(time_sec * 1000.0f);
}

int mu_anim_count(mu_Context *ctx)
{
	int count = 0;
	for (int i = 0; i < MU_ANIM_POOL_SIZE; i++) {
		if (ctx->anim_pool[i].id != 0) {
			count++;
		}
	}
	return count;
}
