/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zui/core.h>
#include <zephyr/zui/host.h>
#include <zephyr/zui/screen.h>

#include "zui_screen_internal.h"
#include "zui_mem.h"

LOG_MODULE_DECLARE(zui);

struct zui_screen {
	const struct zui_screen_ops *ops;
	void *user_data;
	zui_screen_invalidate_cb invalidate_callback;
	void *invalidate_user_data;
	uint32_t tick_period_ms;
	uint32_t last_tick_ms;
	bool redraw_requested;
	bool entered;
};

static const struct zui_screen_ops zui_screen_empty_ops;

#if IS_ENABLED(CONFIG_ZUI_SCREEN_TICK_PERF_LOG)
struct zui_screen_tick_perf_stats {
	uint32_t interval_start_ms;
	uint32_t events;
	uint32_t due_ticks;
	uint32_t dispatched_ticks;
	uint32_t skipped_ticks;
	uint32_t max_due_ticks;
	uint32_t max_dispatch_us;
	uint64_t dispatch_us_total;
};

static struct zui_screen_tick_perf_stats tick_perf;

static uint32_t zui_screen_cycles_since_us(uint32_t start_cycles)
{
	uint64_t us = k_cyc_to_us_floor64((uint32_t)(k_cycle_get_32() - start_cycles));

	return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static void zui_screen_tick_perf_record(uint32_t now_ms, uint32_t period_ms,
					uint32_t due_ticks, uint32_t dispatched_ticks,
					uint32_t dispatch_us)
{
	uint32_t elapsed_ms;
	uint32_t avg_dispatch_us;

	if (tick_perf.interval_start_ms == 0U) {
		tick_perf.interval_start_ms = now_ms;
	}

	tick_perf.events++;
	tick_perf.due_ticks += due_ticks;
	tick_perf.dispatched_ticks += dispatched_ticks;
	if (due_ticks > dispatched_ticks) {
		tick_perf.skipped_ticks += due_ticks - dispatched_ticks;
	}
	if (due_ticks > tick_perf.max_due_ticks) {
		tick_perf.max_due_ticks = due_ticks;
	}
	if (dispatch_us > tick_perf.max_dispatch_us) {
		tick_perf.max_dispatch_us = dispatch_us;
	}
	tick_perf.dispatch_us_total += dispatch_us;

	elapsed_ms = now_ms - tick_perf.interval_start_ms;
	if (elapsed_ms < CONFIG_ZUI_SCREEN_TICK_PERF_LOG_INTERVAL_MS) {
		return;
	}

	avg_dispatch_us = tick_perf.events == 0U ? 0U :
			  (uint32_t)(tick_perf.dispatch_us_total / tick_perf.events);
	LOG_INF("screen tick perf: period=%u events=%u due=%u dispatched=%u skipped=%u "
		"max_due=%u dispatch_avg_us=%u dispatch_max_us=%u",
		period_ms, tick_perf.events, tick_perf.due_ticks,
		tick_perf.dispatched_ticks, tick_perf.skipped_ticks,
		tick_perf.max_due_ticks, avg_dispatch_us, tick_perf.max_dispatch_us);

	memset(&tick_perf, 0, sizeof(tick_perf));
	tick_perf.interval_start_ms = now_ms;
}
#endif

struct zui_screen *zui_screen_create(const struct zui_screen_ops *ops, void *user_data)
{
	struct zui_screen *screen = zui_malloc(sizeof(*screen));

	if (screen == NULL) {
		return NULL;
	}

	memset(screen, 0, sizeof(*screen));
	screen->ops = ops == NULL ? &zui_screen_empty_ops : ops;
	screen->user_data = user_data;

	return screen;
}

void zui_screen_destroy(struct zui_screen *screen)
{
	if (screen == NULL) {
		return;
	}

	(void)zui_screen_exit(screen);

	zui_free(screen);
}

void *zui_screen_get_user_data(const struct zui_screen *screen)
{
	if (screen == NULL) {
		return NULL;
	}

	return screen->user_data;
}

bool zui_screen_is_entered(const struct zui_screen *screen)
{
	return screen != NULL && screen->entered;
}

int zui_screen_set_tick_period(struct zui_screen *screen, uint32_t period_ms)
{
	if (screen == NULL) {
		return -EINVAL;
	}

	screen->tick_period_ms = period_ms;
	screen->last_tick_ms = k_uptime_get_32();
	return 0;
}

uint32_t zui_screen_tick_period(const struct zui_screen *screen)
{
	return screen == NULL ? 0U : screen->tick_period_ms;
}

int zui_screen_request_redraw(struct zui_screen *screen)
{
	if (screen == NULL) {
		return -EINVAL;
	}

	screen->redraw_requested = true;
	if (screen->invalidate_callback != NULL) {
		screen->invalidate_callback(screen, screen->invalidate_user_data);
	} else if (screen->entered) {
		(void)zui_host_request_redraw(zui_get_default_host());
	}
	return 0;
}

int zui_screen_set_invalidate_callback(struct zui_screen *screen, zui_screen_invalidate_cb callback,
				       void *user_data)
{
	if (screen == NULL) {
		return -EINVAL;
	}

	screen->invalidate_callback = callback;
	screen->invalidate_user_data = user_data;
	return 0;
}

bool zui_screen_redraw_is_requested(const struct zui_screen *screen)
{
	return screen != NULL && screen->redraw_requested;
}

int zui_screen_clear_redraw(struct zui_screen *screen)
{
	if (screen == NULL) {
		return -EINVAL;
	}

	screen->redraw_requested = false;
	return 0;
}

int zui_screen_draw(struct zui_screen *screen, struct zui_draw_ctx *draw)
{
	if (screen == NULL || draw == NULL) {
		return -EINVAL;
	}

	if (screen->ops->draw == NULL) {
		return -ENOSYS;
	}

	screen->redraw_requested = false;
	screen->ops->draw(draw, screen->user_data);
	return 0;
}

int zui_screen_submit_input(struct zui_screen *screen, const struct zui_input_event *event)
{
	if (screen == NULL || event == NULL) {
		return -EINVAL;
	}

	if (screen->ops->input == NULL) {
		return -ENOSYS;
	}

	return screen->ops->input(event, screen->user_data) ? 1 : 0;
}

int zui_screen_dispatch_event(struct zui_screen *screen, const struct zui_screen_event *event)
{
	if (screen == NULL || event == NULL) {
		return -EINVAL;
	}

	if (screen->ops->event == NULL) {
		return -ENOSYS;
	}

	return screen->ops->event(event, screen->user_data) ? 1 : 0;
}

int zui_screen_poll_tick(struct zui_screen *screen, uint32_t now_ms)
{
	uint32_t elapsed;
	uint32_t ticks;
	uint32_t due_ticks;
	struct zui_screen_event event;
	int ret;

	if (screen == NULL) {
		return -EINVAL;
	}

	if (!screen->entered || screen->tick_period_ms == 0U) {
		return 0;
	}

	elapsed = now_ms - screen->last_tick_ms;
	ticks = elapsed / screen->tick_period_ms;
	if (ticks == 0U) {
		return 0;
	}

	due_ticks = ticks;
	if (ticks > CONFIG_ZUI_SCREEN_TICK_CATCH_UP_MAX) {
		ticks = CONFIG_ZUI_SCREEN_TICK_CATCH_UP_MAX;
		/*
		 * Drop old backlog, but leave the next tick due immediately so
		 * heavy screens are processing-bound instead of sleep-throttled.
		 */
		screen->last_tick_ms = now_ms - screen->tick_period_ms;
	} else {
		screen->last_tick_ms += ticks * screen->tick_period_ms;
	}
	event = (struct zui_screen_event){
		.type = ZUI_SCREEN_EVENT_TICK,
		.code = ticks,
	};

#if IS_ENABLED(CONFIG_ZUI_SCREEN_TICK_PERF_LOG)
	uint32_t dispatch_start_cycles = k_cycle_get_32();

	ret = zui_screen_dispatch_event(screen, &event);
	zui_screen_tick_perf_record(now_ms, screen->tick_period_ms, due_ticks, ticks,
				    zui_screen_cycles_since_us(dispatch_start_cycles));
#else
	ret = zui_screen_dispatch_event(screen, &event);
#endif

	return ret;
}

int32_t zui_screen_next_tick_timeout_ms(const struct zui_screen *screen, uint32_t now_ms)
{
	uint32_t elapsed;

	if (screen == NULL) {
		return -EINVAL;
	}

	if (!screen->entered || screen->tick_period_ms == 0U) {
		return -ENODATA;
	}

	elapsed = now_ms - screen->last_tick_ms;
	if (elapsed >= screen->tick_period_ms) {
		return 0;
	}

	return (int32_t)(screen->tick_period_ms - elapsed);
}

int zui_screen_enter(struct zui_screen *screen)
{
	if (screen == NULL || screen->entered) {
		return screen == NULL ? -EINVAL : 0;
	}

	screen->entered = true;
	screen->last_tick_ms = k_uptime_get_32();
	if (screen->ops->enter != NULL) {
		screen->ops->enter(screen->user_data);
	}

	return 0;
}

int zui_screen_exit(struct zui_screen *screen)
{
	if (screen == NULL || !screen->entered) {
		return screen == NULL ? -EINVAL : 0;
	}

	screen->entered = false;
	if (screen->ops->exit != NULL) {
		screen->ops->exit(screen->user_data);
	}

	return 0;
}
