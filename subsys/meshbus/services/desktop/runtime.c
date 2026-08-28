/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include "registry/apps_registry_prvi.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_MESHBUS_DISPLAY)
#include <zephyr/meshbus/display.h>
#endif
#if defined(CONFIG_MESHBUS_POWER)
#include <zephyr/meshbus/power.h>
#endif
#if defined(CONFIG_ZBUS) && \
	(defined(CONFIG_MESHBUS_DISPLAY) || defined(CONFIG_MESHBUS_POWER))
#include <zephyr/zbus/zbus.h>
#endif
#include <zephyr/zui/zui.h>

#ifndef CONFIG_MESHBUS_DESKTOP_THREAD_PRIORITY
#define CONFIG_MESHBUS_DESKTOP_THREAD_PRIORITY 0
#endif

#ifndef CONFIG_MESHBUS_DESKTOP_THREAD_STACK_SIZE
#define CONFIG_MESHBUS_DESKTOP_THREAD_STACK_SIZE 4096
#endif

LOG_MODULE_REGISTER(meshbus_desktop, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

K_THREAD_STACK_DEFINE(meshbus_desktop_thread_stack, CONFIG_MESHBUS_DESKTOP_THREAD_STACK_SIZE);
static struct k_thread meshbus_desktop_thread;

static struct zui_desktop *desktop_instance;
static void zui_desktop_complete_app_exit(struct zui_desktop *desktop);

#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
static uint32_t zui_desktop_cycles_since_us(uint32_t start_cycles)
{
	uint64_t us = k_cyc_to_us_floor64((uint32_t)(k_cycle_get_32() - start_cycles));

	return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static void zui_desktop_perf_record_draw(struct zui_desktop *desktop, uint32_t draw_us,
					 uint32_t present_us)
{
	if (desktop == NULL) {
		return;
	}

	desktop->perf.draw_calls++;
	desktop->perf.draw_us_total += draw_us;
	desktop->perf.present_us_total += present_us;
	if (draw_us > desktop->perf.max_draw_us) {
		desktop->perf.max_draw_us = draw_us;
	}
	if (present_us > desktop->perf.max_present_us) {
		desktop->perf.max_present_us = present_us;
	}
}

static void zui_desktop_perf_record_poll(struct zui_desktop *desktop, int dispatches,
					 uint32_t poll_us)
{
	if (desktop == NULL) {
		return;
	}

	desktop->perf.poll_calls++;
	if (dispatches > 0) {
		desktop->perf.poll_dispatches += (uint32_t)dispatches;
	}
	desktop->perf.poll_us_total += poll_us;
	if (poll_us > desktop->perf.max_poll_us) {
		desktop->perf.max_poll_us = poll_us;
	}
}

static void zui_desktop_perf_record_loop(struct zui_desktop *desktop, uint32_t loop_us)
{
	if (desktop == NULL) {
		return;
	}

	desktop->perf.loops++;
	desktop->perf.loop_us_total += loop_us;
	if (loop_us > desktop->perf.max_loop_us) {
		desktop->perf.max_loop_us = loop_us;
	}
}

static void zui_desktop_perf_maybe_log(struct zui_desktop *desktop, uint32_t now_ms)
{
	uint32_t elapsed_ms;
	uint32_t loop_avg_us;
	uint32_t poll_avg_us;
	uint32_t draw_avg_us;
	uint32_t present_avg_us;

	if (desktop == NULL) {
		return;
	}

	if (desktop->perf.interval_start_ms == 0U) {
		desktop->perf.interval_start_ms = now_ms;
	}

	elapsed_ms = now_ms - desktop->perf.interval_start_ms;
	if (elapsed_ms < CONFIG_MESHBUS_DESKTOP_PERF_LOG_INTERVAL_MS) {
		return;
	}

	loop_avg_us = desktop->perf.loops == 0U ? 0U :
		      (uint32_t)(desktop->perf.loop_us_total / desktop->perf.loops);
	poll_avg_us = desktop->perf.poll_calls == 0U ? 0U :
		      (uint32_t)(desktop->perf.poll_us_total / desktop->perf.poll_calls);
	draw_avg_us = desktop->perf.draw_calls == 0U ? 0U :
		      (uint32_t)(desktop->perf.draw_us_total / desktop->perf.draw_calls);
	present_avg_us = desktop->perf.draw_calls == 0U ? 0U :
			 (uint32_t)(desktop->perf.present_us_total / desktop->perf.draw_calls);

	LOG_INF("Desktop perf: loops=%u poll=%u dispatch=%u draw=%u "
		"loop_avg_us=%u loop_max_us=%u poll_avg_us=%u poll_max_us=%u "
		"draw_avg_us=%u draw_max_us=%u present_avg_us=%u present_max_us=%u",
		desktop->perf.loops, desktop->perf.poll_calls,
		desktop->perf.poll_dispatches, desktop->perf.draw_calls,
		loop_avg_us, desktop->perf.max_loop_us, poll_avg_us,
		desktop->perf.max_poll_us, draw_avg_us, desktop->perf.max_draw_us,
		present_avg_us, desktop->perf.max_present_us);

	memset(&desktop->perf, 0, sizeof(desktop->perf));
	desktop->perf.interval_start_ms = now_ms;
}
#endif

enum zui_desktop_input_filter {
	ZUI_DESKTOP_INPUT_FILTER_SAME_CODE_CLICKS,
	ZUI_DESKTOP_INPUT_FILTER_ANY_CLICKS,
	ZUI_DESKTOP_INPUT_FILTER_OLDEST,
};

static bool zui_desktop_input_is_directional_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK &&
	       (event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_DOWN ||
		event->code == ZUI_INPUT_CODE_LEFT || event->code == ZUI_INPUT_CODE_RIGHT);
}

static size_t zui_desktop_input_filter_queue(struct zui_desktop *desktop,
					     enum zui_desktop_input_filter filter,
					     const struct zui_input_event *event)
{
	/* Keep release events ahead of stale directional repeats under UI load. */
	struct zui_input_event queued[CONFIG_ZUI_INPUT_QUEUE_SIZE];
	struct zui_input_event current;
	size_t count = 0U;
	size_t dropped = 0U;
	bool dropped_oldest = false;

	while (k_msgq_get(&desktop->input_msgq, &current, K_NO_WAIT) == 0) {
		bool drop = false;

		switch (filter) {
		case ZUI_DESKTOP_INPUT_FILTER_SAME_CODE_CLICKS:
			drop = zui_desktop_input_is_directional_click(&current) &&
			       event != NULL && current.code == event->code;
			break;
		case ZUI_DESKTOP_INPUT_FILTER_ANY_CLICKS:
			drop = zui_desktop_input_is_directional_click(&current);
			break;
		case ZUI_DESKTOP_INPUT_FILTER_OLDEST:
			drop = !dropped_oldest;
			break;
		default:
			break;
		}

		if (drop) {
			dropped++;
			dropped_oldest = true;
			continue;
		}

		if (count < ARRAY_SIZE(queued)) {
			queued[count++] = current;
		} else {
			dropped++;
		}
	}

	for (size_t i = 0U; i < count; i++) {
		(void)k_msgq_put(&desktop->input_msgq, &queued[i], K_NO_WAIT);
	}

	return dropped;
}

void zui_desktop_request_redraw(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	(void)zui_host_request_redraw(desktop->host);
	(void)zui_screen_request_redraw(zui_router_screen(desktop->router,
							  zui_router_current(desktop->router)));
	k_sem_give(&desktop->redraw_sem);
}

int zui_desktop_switch(struct zui_desktop *desktop, uint32_t screen_id)
{
	int ret;

	if (desktop == NULL || desktop->router == NULL) {
		return -EINVAL;
	}

	ret = zui_router_switch(desktop->router, screen_id);
	if (ret == 0) {
		zui_desktop_request_redraw(desktop);
	}

	return ret;
}

static void zui_desktop_host_redraw_requested(struct zui_host *host, void *user_data)
{
	struct zui_desktop *desktop = user_data;

	ARG_UNUSED(host);

	if (desktop != NULL) {
		k_sem_give(&desktop->redraw_sem);
	}
}

#if defined(CONFIG_MESHBUS_DISPLAY)
static void zui_desktop_refresh_sleep_policy(struct zui_desktop *desktop)
{
	bool should_suspend;

	if (desktop == NULL || desktop->host == NULL) {
		return;
	}

	should_suspend = !meshbus_display_is_active() &&
			 zui_router_current(desktop->router) == MESHBUS_DESKTOP_VIEW_DASHBOARD;
	if (should_suspend == desktop->ui_suspended_for_sleep) {
		return;
	}

	(void)zui_host_set_suspended(desktop->host, should_suspend);
	desktop->ui_suspended_for_sleep = should_suspend;
	zui_desktop_request_redraw(desktop);
}
#else
static void zui_desktop_refresh_sleep_policy(struct zui_desktop *desktop)
{
	ARG_UNUSED(desktop);
}
#endif

#if defined(CONFIG_MESHBUS_DISPLAY) && defined(CONFIG_ZBUS)
static void meshbus_desktop_display_state_cb(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);

	zui_desktop_refresh_sleep_policy(desktop_instance);
}

ZBUS_LISTENER_DEFINE(meshbus_desktop_display_state_listener, meshbus_desktop_display_state_cb);
ZBUS_CHAN_ADD_OBS(meshbus_display_state_chan, meshbus_desktop_display_state_listener, 2);
#endif

#if defined(CONFIG_MESHBUS_POWER) && defined(CONFIG_ZBUS)
static void meshbus_desktop_power_data_cb(const struct zbus_channel *chan)
{
	struct zui_desktop *desktop = desktop_instance;

	ARG_UNUSED(chan);

	if (desktop == NULL) {
		return;
	}

	atomic_set(&desktop->dashboard_header_power_dirty, 1);
	zui_desktop_request_redraw(desktop);
}

ZBUS_LISTENER_DEFINE(meshbus_desktop_power_data_listener, meshbus_desktop_power_data_cb);
ZBUS_CHAN_ADD_OBS(meshbus_power_fuel_gauge_data_chan, meshbus_desktop_power_data_listener, 2);
#endif

bool desktop_input_is_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK;
}

bool desktop_input_is_long(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_LONG_PRESS;
}

bool desktop_shell_should_consume_edge_event(const struct zui_input_event *event)
{
	if (event == NULL ||
	    (event->action != ZUI_INPUT_ACTION_PRESS && event->action != ZUI_INPUT_ACTION_RELEASE)) {
		return false;
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_UP:
	case ZUI_INPUT_CODE_DOWN:
	case ZUI_INPUT_CODE_LEFT:
	case ZUI_INPUT_CODE_RIGHT:
	case ZUI_INPUT_CODE_SELECT:
	case ZUI_INPUT_CODE_BACK:
		return true;
	default:
		return false;
	}
}

static const struct device *zui_desktop_display_device(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	return device_is_ready(display) ? display : NULL;
#else
	return NULL;
#endif
}

int zui_desktop_register_screen(struct zui_desktop *desktop, uint32_t id,
				       struct zui_screen *screen)
{
	int ret;

	ret = zui_router_register_screen(desktop->router, id, screen);
	if (ret != 0) {
		LOG_ERR("Failed to register Desktop screen %u: %d", id, ret);
	}

	return ret;
}

bool desktop_open_app(struct zui_desktop *desktop, meshbus_desktop_app_handle_t handle,
		      uint32_t return_screen_id)
{
	const struct meshbus_desktop_app_desc *app;
	int ret;

	if (desktop == NULL || !meshbus_desktop_app_handle_is_valid(handle)) {
		return false;
	}

	ret = meshbus_desktop_app_registry_resolve_handle(handle, &app);
	if (ret != 0 || app == NULL || app->app_main == NULL) {
		LOG_WRN("App handle invalid: handle=0x%08x (%d)", (unsigned)handle, ret);
		return false;
	}

	desktop->active_app_handle = handle;
	desktop->active_app = app;
	desktop->app_return_screen_id = return_screen_id;
	(void)zui_host_set_layer_enabled(desktop->host, ZUI_LAYER_DESKTOP, false);
	ret = desktop_app_registry_start(desktop, handle);
	if (ret != 0) {
		LOG_WRN("App start failed: id=%s handle=0x%08x (%d)",
			app->id != NULL ? app->id : "(null)", (unsigned)handle, ret);
		desktop->active_app = NULL;
		desktop->active_app_handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
		(void)zui_host_set_layer_enabled(desktop->host, ZUI_LAYER_DESKTOP, true);
		(void)zui_host_send_layer_to_front(desktop->host, ZUI_LAYER_DESKTOP);
		(void)zui_desktop_switch(desktop, return_screen_id);
		return false;
	}

	return true;
}

static struct zui_desktop *zui_desktop_alloc(void)
{
	struct zui_desktop *desktop;
	const struct device *display;
	int ret;

	desktop = k_calloc(1U, sizeof(*desktop));
	if (desktop == NULL) {
		return NULL;
	}

	k_sem_init(&desktop->redraw_sem, 0, 1);
	k_mutex_init(&desktop->input_mutex);
	k_msgq_init(&desktop->input_msgq, (char *)desktop->input_events,
		    sizeof(desktop->input_events[0]), ARRAY_SIZE(desktop->input_events));
	desktop->host = zui_host_create(desktop);
	desktop->router = zui_router_create();
	if (desktop->host == NULL || desktop->router == NULL) {
		LOG_ERR("Failed to allocate Desktop host/router");
		goto fail;
	}
	(void)zui_host_set_redraw_callback(desktop->host, zui_desktop_host_redraw_requested,
					   desktop);

	ret = zui_host_attach_router(desktop->host, ZUI_LAYER_DESKTOP, desktop->router);
	if (ret != 0) {
		LOG_ERR("Failed to attach Desktop router: %d", ret);
		goto fail;
	}

	if (desktop_dashboard_init(desktop) != 0 || desktop_main_menu_init(desktop) != 0 ||
#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	    desktop_launcher_init(desktop) != 0 ||
#endif
	    desktop_power_menu_init(desktop) != 0) {
		goto fail;
	}

	ret = zui_router_switch(desktop->router, MESHBUS_DESKTOP_VIEW_DASHBOARD);
	if (ret != 0) {
		LOG_ERR("Failed to switch Desktop HOME screen: %d", ret);
		goto fail;
	}

	display = zui_desktop_display_device();
	if (display == NULL) {
		LOG_WRN("Desktop display device is not ready");
	}
	desktop->draw = zui_draw_ctx_create(display);
	if (desktop->draw == NULL) {
		LOG_WRN("Desktop draw context unavailable");
	}

	desktop->active_app_handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	desktop->app_return_screen_id = MESHBUS_DESKTOP_VIEW_DASHBOARD;
	desktop->app_shared_stack_size = CONFIG_MESHBUS_DESKTOP_APP_SHARED_STACK_SIZE;
	desktop->app_shared_stack = k_thread_stack_alloc(desktop->app_shared_stack_size, 0);
	if (desktop->app_shared_stack == NULL) {
		LOG_ERR("Failed to allocate shared app stack (%u)",
			(unsigned int)desktop->app_shared_stack_size);
		goto fail;
	}

	return desktop;

fail:
	desktop_power_menu_deinit(desktop);
#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	desktop_launcher_deinit(desktop);
#endif
	desktop_main_menu_deinit(desktop);
	desktop_dashboard_deinit(desktop);
	if (desktop->draw != NULL) {
		zui_draw_ctx_destroy(desktop->draw);
	}
	if (desktop->router != NULL) {
		zui_router_destroy(desktop->router);
	}
	if (desktop->host != NULL) {
		zui_host_destroy(desktop->host);
	}
	if (desktop->app_shared_stack != NULL) {
		(void)k_thread_stack_free(desktop->app_shared_stack);
	}
	k_free(desktop);
	return NULL;
}

static void zui_desktop_free(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	if (desktop_app_registry_detach_desktop_all(desktop) != 0) {
		LOG_ERR("Refusing to free Desktop while an app owns the shared stack");
		return;
	}

	if (desktop_instance == desktop) {
		desktop_instance = NULL;
	}
	if (desktop->app_shared_stack != NULL) {
		(void)k_thread_stack_free(desktop->app_shared_stack);
		desktop->app_shared_stack = NULL;
		desktop->app_shared_stack_size = 0U;
	}
	desktop_power_menu_deinit(desktop);
#if defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	desktop_launcher_deinit(desktop);
#endif
	desktop_main_menu_deinit(desktop);
	desktop_dashboard_deinit(desktop);

	if (desktop->draw != NULL) {
		zui_draw_ctx_destroy(desktop->draw);
		desktop->draw = NULL;
	}
	if (desktop->router != NULL) {
		zui_router_destroy(desktop->router);
		desktop->router = NULL;
	}
	if (desktop->host != NULL) {
		zui_host_destroy(desktop->host);
		desktop->host = NULL;
	}
	k_free(desktop);
}

static void zui_desktop_draw(struct zui_desktop *desktop)
{
	int ret;
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
	uint32_t draw_start_cycles;
	uint32_t draw_us;
	uint32_t present_start_cycles;
	uint32_t present_us;
#endif

	if (desktop == NULL || desktop->draw == NULL || desktop->host == NULL) {
		return;
	}

#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
	draw_start_cycles = k_cycle_get_32();
#endif
	ret = zui_host_draw(desktop->host, desktop->draw);
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
	draw_us = zui_desktop_cycles_since_us(draw_start_cycles);
#endif
	if (ret != 0) {
		LOG_DBG("Desktop host draw returned %d", ret);
	}
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
	present_start_cycles = k_cycle_get_32();
#endif
	ret = zui_draw_present(desktop->draw);
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
	present_us = zui_desktop_cycles_since_us(present_start_cycles);
	zui_desktop_perf_record_draw(desktop, draw_us, present_us);
#endif
	if (ret != 0) {
		LOG_DBG("Desktop draw present returned %d", ret);
	}
}

static void zui_desktop_drain_input(struct zui_desktop *desktop)
{
	struct zui_input_event event;
	int ret;

	if (desktop == NULL || desktop->host == NULL) {
		return;
	}

	while (true) {
		k_mutex_lock(&desktop->input_mutex, K_FOREVER);
		ret = k_msgq_get(&desktop->input_msgq, &event, K_NO_WAIT);
		k_mutex_unlock(&desktop->input_mutex);
		if (ret != 0) {
			break;
		}
		(void)zui_host_submit_input(desktop->host, &event);
	}
}

static k_timeout_t zui_desktop_wait_timeout(struct zui_desktop *desktop)
{
	uint32_t dashboard_timeout_ms;
	int32_t tick_timeout_ms;

	if (desktop == NULL || desktop->host == NULL) {
		dashboard_timeout_ms = desktop_dashboard_draw_timeout_ms(desktop);
		return K_MSEC(dashboard_timeout_ms);
	}
	if (zui_host_is_suspended(desktop->host)) {
		return K_FOREVER;
	}

	dashboard_timeout_ms = desktop_dashboard_draw_timeout_ms(desktop);
	tick_timeout_ms = zui_host_next_timeout_ms(desktop->host);
	if (tick_timeout_ms == 0) {
		return K_NO_WAIT;
	}
	if (tick_timeout_ms > 0 && tick_timeout_ms < dashboard_timeout_ms) {
		return K_MSEC(tick_timeout_ms);
	}

	return K_MSEC(dashboard_timeout_ms);
}

static void meshbus_desktop_thread_entry(void *arg1, void *arg2, void *arg3)
{
	struct zui_desktop *desktop = arg1;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (desktop == NULL) {
		LOG_ERR("Desktop thread started without desktop instance");
		return;
	}

	(void)zui_host_run(desktop->host);
	zui_desktop_draw(desktop);

	while (true) {
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
		uint32_t loop_start_cycles = k_cycle_get_32();
#endif
		(void)k_sem_take(&desktop->redraw_sem, zui_desktop_wait_timeout(desktop));
		zui_desktop_complete_app_exit(desktop);
		desktop_power_menu_poll(desktop);
		zui_desktop_refresh_sleep_policy(desktop);
		zui_desktop_drain_input(desktop);
		if (!zui_host_is_suspended(desktop->host)) {
			int poll_ret;
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
			uint32_t poll_start_cycles;
#endif
			desktop_dashboard_tab_poll(desktop);
			desktop_dashboard_poll(desktop);
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
			poll_start_cycles = k_cycle_get_32();
#endif
			poll_ret = zui_host_poll(desktop->host);
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
			zui_desktop_perf_record_poll(desktop, poll_ret,
						     zui_desktop_cycles_since_us(poll_start_cycles));
#endif
			zui_desktop_draw(desktop);
		}
#if IS_ENABLED(CONFIG_MESHBUS_DESKTOP_PERF_LOG)
		zui_desktop_perf_record_loop(desktop,
					     zui_desktop_cycles_since_us(loop_start_cycles));
		zui_desktop_perf_maybe_log(desktop, k_uptime_get_32());
#endif
	}
}

struct zui_desktop *zui_desktop_get_instance(void)
{
	return desktop_instance;
}

int zui_desktop_submit_input(struct zui_desktop *desktop, const struct zui_input_event *event)
{
	int ret;

	if (desktop == NULL || event == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&desktop->input_mutex, K_FOREVER);

	if (zui_desktop_input_is_directional_click(event)) {
		(void)zui_desktop_input_filter_queue(
			desktop, ZUI_DESKTOP_INPUT_FILTER_SAME_CODE_CLICKS, event);
	}

	ret = k_msgq_put(&desktop->input_msgq, event, K_NO_WAIT);
	if (ret != 0 && event->action != ZUI_INPUT_ACTION_CLICK) {
		(void)zui_desktop_input_filter_queue(desktop,
						     ZUI_DESKTOP_INPUT_FILTER_ANY_CLICKS,
						     event);
		ret = k_msgq_put(&desktop->input_msgq, event, K_NO_WAIT);
	}
	if (ret != 0 && event->action == ZUI_INPUT_ACTION_RELEASE) {
		(void)zui_desktop_input_filter_queue(desktop, ZUI_DESKTOP_INPUT_FILTER_OLDEST,
						     event);
		ret = k_msgq_put(&desktop->input_msgq, event, K_NO_WAIT);
	}
	k_mutex_unlock(&desktop->input_mutex);
	if (ret != 0) {
		return ret == -ENOMSG ? -ENOSPC : ret;
	}

	k_sem_give(&desktop->redraw_sem);
	return 0;
}

bool meshbus_desktop_external_app_is_active(void)
{
#if !defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	return false;
#else
	struct zui_desktop *desktop = desktop_instance;
	meshbus_desktop_app_handle_t handle;

	if (desktop == NULL) {
		return false;
	}

	handle = desktop->active_app_handle;
	return desktop_app_registry_is_external_handle(handle) &&
	       desktop_app_registry_is_running(handle);
#endif
}

void zui_desktop_request_app_exit(struct zui_desktop *desktop)
{
	if (desktop == NULL) {
		return;
	}

	atomic_set(&desktop->app_exit_pending, 1);
	k_sem_give(&desktop->redraw_sem);
}

static void zui_desktop_complete_app_exit(struct zui_desktop *desktop)
{
	meshbus_desktop_app_handle_t handle;
	int ret;

	if (desktop == NULL || !atomic_cas(&desktop->app_exit_pending, 1, 0)) {
		return;
	}

	handle = desktop->active_app_handle;
	ret = desktop_app_registry_complete_exit(handle, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to reclaim app thread: handle=0x%08x (%d)",
			(unsigned int)handle, ret);
		return;
	}

	if (desktop_app_registry_is_external_handle(handle)) {
		ret = desktop_app_registry_external_release(handle);
		if (ret != 0) {
			LOG_ERR("Failed to release external app: handle=0x%08x (%d)",
				(unsigned int)handle, ret);
		}
	}

	desktop->active_app = NULL;
	desktop->active_app_handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	if (desktop->host != NULL && desktop->router != NULL) {
		(void)zui_host_set_layer_enabled(desktop->host, ZUI_LAYER_DESKTOP, true);
		(void)zui_host_send_layer_to_front(desktop->host, ZUI_LAYER_DESKTOP);
		(void)zui_desktop_switch(desktop, desktop->app_return_screen_id);
	}
}

int meshbus_desktop_external_app_start(const struct meshbus_desktop_external_app_desc *desc)
{
#if !defined(CONFIG_MESHBUS_DESKTOP_LAUNCHER)
	ARG_UNUSED(desc);
	return -ENOTSUP;
#else
	meshbus_desktop_app_handle_t handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	struct zui_desktop *desktop = desktop_instance;
	int ret;

	if (desktop == NULL) {
		return -ENODEV;
	}
	if (desktop->router == NULL ||
	    zui_router_current(desktop->router) != MESHBUS_DESKTOP_VIEW_LAUNCHER) {
		return -EBUSY;
	}

	ret = desktop_app_registry_external_prepare(desc, &handle);
	if (ret != 0) {
		return ret;
	}

	if (!desktop_open_app(desktop, handle, MESHBUS_DESKTOP_VIEW_LAUNCHER)) {
		(void)desktop_app_registry_external_release(handle);
		return -EBUSY;
	}

	return 0;
#endif
}

int meshbus_desktop_init(void)
{
	struct zui_desktop *desktop;
	k_tid_t tid;

	LOG_INF("Starting Meshbus desktop runtime");

	desktop = zui_desktop_alloc();
	if (desktop == NULL) {
		LOG_ERR("Failed to allocate Desktop runtime");
		return 0;
	}
	desktop_instance = desktop;

	zui_desktop_refresh_sleep_policy(desktop);

	tid = k_thread_create(&meshbus_desktop_thread, meshbus_desktop_thread_stack,
			      K_THREAD_STACK_SIZEOF(meshbus_desktop_thread_stack),
			      meshbus_desktop_thread_entry, desktop, NULL, NULL,
			      CONFIG_MESHBUS_DESKTOP_THREAD_PRIORITY, 0, K_NO_WAIT);
	if (tid == NULL) {
		LOG_ERR("Failed to create Desktop thread");
		zui_desktop_free(desktop);
		return 0;
	}

	k_thread_name_set(tid, "meshbus_desktop");
	return 0;
}

SYS_INIT(meshbus_desktop_init, APPLICATION, 999);
