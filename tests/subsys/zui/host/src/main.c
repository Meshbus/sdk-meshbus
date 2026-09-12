// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>
#include <zui/zui.h>

struct callback_state {
	int enter_count;
	int exit_count;
	int input_count;
	int event_count;
	int redraw_count;
	bool input_consumed;
	bool fallback_consumed;
	bool event_consumed;
	int32_t last_value;
	uint32_t last_event_code;
	enum zui_input_code last_code;
	enum zui_input_action last_action;
};

struct host_lock_state {
	struct zui_host *host;
	struct k_sem input_entered;
	struct k_sem release_input;
	struct k_sem input_done;
	struct k_sem detach_done;
	atomic_t detach_returned;
	int input_rc;
	int detach_rc;
};

K_THREAD_STACK_DEFINE(host_lock_input_stack, 1024);
K_THREAD_STACK_DEFINE(host_lock_detach_stack, 1024);
static struct k_thread host_lock_input_thread;
static struct k_thread host_lock_detach_thread;

static void screen_enter(void *user_data)
{
	struct callback_state *state = user_data;

	state->enter_count++;
}

static void screen_exit(void *user_data)
{
	struct callback_state *state = user_data;

	state->exit_count++;
}

static bool screen_input(const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	state->input_count++;
	state->last_code = event->code;
	state->last_action = event->action;
	state->last_value = event->value;
	return state->input_consumed ||
	       (state->fallback_consumed && event->code != ZUI_INPUT_CODE_KEYPAD);
}

static bool blocking_screen_input(const struct zui_input_event *event, void *user_data)
{
	struct host_lock_state *state = user_data;

	ARG_UNUSED(event);

	k_sem_give(&state->input_entered);
	(void)k_sem_take(&state->release_input, K_FOREVER);
	return true;
}

static bool screen_event(const struct zui_screen_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	state->event_count++;
	state->last_event_code = event->code;
	return state->event_consumed;
}

static void host_redraw(struct zui_host *host, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(host);

	state->redraw_count++;
}

static const struct zui_screen_ops test_screen_ops = {
	.input = screen_input,
	.enter = screen_enter,
	.exit = screen_exit,
	.event = screen_event,
};

static const struct zui_screen_ops blocking_screen_ops = {
	.input = blocking_screen_input,
};

static void host_lock_input_entry(void *p1, void *p2, void *p3)
{
	struct host_lock_state *state = p1;
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	state->input_rc = zui_host_submit_input(state->host, &event);
	k_sem_give(&state->input_done);
}

static void host_lock_detach_entry(void *p1, void *p2, void *p3)
{
	struct host_lock_state *state = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	state->detach_rc = zui_host_detach_router(state->host, ZUI_LAYER_FULLSCREEN);
	atomic_set(&state->detach_returned, 1);
	k_sem_give(&state->detach_done);
}

static struct zui_screen *new_test_screen(struct callback_state *state)
{
	state->input_consumed = true;
	state->event_consumed = true;
	return zui_screen_create(&test_screen_ops, state);
}

ZTEST(zui_host, test_core_default_host_lifecycle)
{
	struct zui_host *host;
	struct zui_runtime_stats stats;
	struct zui_version version = zui_get_version();

	zassert_equal(zui_deinit(), 0);
	zassert_is_null(zui_get_default_host());
	zassert_equal(zui_get_runtime_stats(NULL), -EINVAL);
	zassert_ok(zui_get_runtime_stats(&stats));
	zassert_true(version.major == 0U && version.minor >= 1U);

	zassert_ok(zui_init());
	host = zui_get_default_host();
	zassert_not_null(host);
	zassert_ok(zui_init());
	zassert_equal(zui_get_default_host(), host);
	zassert_ok(zui_deinit());
	zassert_is_null(zui_get_default_host());

	host = zui_host_create(NULL);
	zassert_not_null(host);
	zassert_equal(zui_get_default_host(), host);
	zassert_equal(zui_deinit(), -EBUSY);
	zui_host_destroy(host);
	zassert_is_null(zui_get_default_host());
}

ZTEST(zui_host, test_router_register_switch_and_unregister)
{
	struct callback_state first = {0};
	struct callback_state second = {0};
	struct zui_router *router = zui_router_create();
	struct zui_screen *first_screen = new_test_screen(&first);
	struct zui_screen *second_screen = new_test_screen(&second);

	zassert_not_null(router);
	zassert_not_null(first_screen);
	zassert_not_null(second_screen);

	zassert_equal(zui_router_register_screen(NULL, 1, first_screen), -EINVAL);
	zassert_equal(zui_router_register_screen(router, 0, first_screen), -EINVAL);
	zassert_equal(zui_router_register_screen(router, 1, NULL), -EINVAL);
	zassert_ok(zui_router_register_screen(router, 1, first_screen));
	zassert_equal(zui_router_register_screen(router, 1, first_screen), -EEXIST);
	zassert_ok(zui_router_register_screen(router, 2, second_screen));
	zassert_equal(zui_router_screen_count(router), 2);
	zassert_equal(zui_router_screen(router, 1), first_screen);
	zassert_is_null(zui_router_screen(router, 99));

	zassert_ok(zui_router_switch(router, 1));
	zassert_equal(zui_router_current(router), 1);
	zassert_equal(first.enter_count, 1);
	zassert_equal(first.exit_count, 0);

	zassert_ok(zui_router_switch(router, 1));
	zassert_equal(first.enter_count, 1);
	zassert_equal(first.exit_count, 0);

	zassert_ok(zui_router_switch(router, 2));
	zassert_equal(first.exit_count, 1);
	zassert_equal(second.enter_count, 1);
	zassert_equal(zui_router_switch(router, 99), -ENOENT);

	zassert_ok(zui_router_unregister_screen(router, 2));
	zassert_equal(second.exit_count, 1);
	zassert_equal(zui_router_current(router), 0);
	zassert_equal(zui_router_screen_count(router), 1);
	zassert_equal(zui_router_unregister_screen(router, 2), -ENOENT);

	zui_router_destroy(router);
	zui_screen_destroy(first_screen);
	zui_screen_destroy(second_screen);
}

ZTEST(zui_host, test_router_dispatches_events_to_current_screen)
{
	struct callback_state state = {0};
	struct zui_router *router = zui_router_create();
	struct zui_screen *screen = new_test_screen(&state);
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_CUSTOM,
		.code = 42,
	};

	zassert_not_null(router);
	zassert_not_null(screen);
	zassert_equal(zui_router_dispatch_event(router, &event), -ENODEV);
	zassert_equal(zui_router_dispatch_event(NULL, &event), -EINVAL);
	zassert_equal(zui_router_dispatch_event(router, NULL), -EINVAL);

	zassert_ok(zui_router_register_screen(router, 1, screen));
	zassert_ok(zui_router_switch(router, 1));
	zassert_equal(zui_router_dispatch_event(router, &event), 1);
	zassert_equal(state.event_count, 1);
	zassert_equal(state.last_event_code, 42);

	state.event_consumed = false;
	zassert_equal(zui_router_dispatch_event(router, &event), 0);
	zassert_equal(state.event_count, 2);

	zui_router_destroy(router);
	zui_screen_destroy(screen);
}

ZTEST(zui_host, test_host_routes_input_by_layer_state_and_order)
{
	struct callback_state desktop_state = {0};
	struct callback_state fullscreen_state = {0};
	struct zui_host *host = zui_host_create(&desktop_state);
	struct zui_router *desktop_router = zui_router_create();
	struct zui_router *fullscreen_router = zui_router_create();
	struct zui_screen *desktop_screen = new_test_screen(&desktop_state);
	struct zui_screen *fullscreen_screen = new_test_screen(&fullscreen_state);
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};

	zassert_not_null(host);
	zassert_equal(zui_host_get_user_data(host), &desktop_state);
	zassert_not_null(desktop_router);
	zassert_not_null(fullscreen_router);
	zassert_not_null(desktop_screen);
	zassert_not_null(fullscreen_screen);

	zassert_ok(zui_router_register_screen(desktop_router, 1, desktop_screen));
	zassert_ok(zui_router_register_screen(fullscreen_router, 2, fullscreen_screen));
	zassert_ok(zui_router_switch(desktop_router, 1));
	zassert_ok(zui_router_switch(fullscreen_router, 2));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_DESKTOP, desktop_router));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, fullscreen_router));

	zassert_ok(zui_host_submit_input(host, &event));
	zassert_equal(fullscreen_state.input_count, 1);
	zassert_equal(desktop_state.input_count, 0);
	zassert_equal(fullscreen_state.last_code, ZUI_INPUT_CODE_SELECT);

	zassert_ok(zui_host_set_layer_enabled(host, ZUI_LAYER_FULLSCREEN, false));
	zassert_ok(zui_host_submit_input(host, &event));
	zassert_equal(desktop_state.input_count, 1);
	zassert_false(zui_host_layer_is_enabled(host, ZUI_LAYER_FULLSCREEN));

	zassert_ok(zui_host_set_layer_enabled(host, ZUI_LAYER_FULLSCREEN, true));
	zassert_ok(zui_host_send_layer_to_front(host, ZUI_LAYER_DESKTOP));
	zassert_ok(zui_host_submit_input(host, &event));
	zassert_equal(desktop_state.input_count, 2);

	desktop_state.input_consumed = false;
	zassert_equal(zui_host_submit_input(host, &event), -ENODATA);
	zassert_equal(desktop_state.input_count, 3);

	zassert_ok(zui_host_set_input_locked(host, true));
	zassert_true(zui_host_is_input_locked(host));
	zassert_equal(zui_host_submit_input(host, &event), -EBUSY);
	zassert_ok(zui_host_set_input_locked(host, false));

	zassert_ok(zui_host_set_suspended(host, true));
	zassert_true(zui_host_is_suspended(host));
	zassert_equal(zui_host_submit_input(host, &event), -EBUSY);
	zassert_ok(zui_host_set_suspended(host, false));

	zassert_ok(zui_host_run(host));
	zassert_ok(zui_host_stop(host));
	zassert_ok(zui_host_request_redraw(host));

	zui_host_destroy(host);
	zui_router_destroy(desktop_router);
	zui_router_destroy(fullscreen_router);
	zui_screen_destroy(desktop_screen);
	zui_screen_destroy(fullscreen_screen);
}

ZTEST(zui_host, test_host_detach_waits_for_in_flight_input_callback)
{
	struct host_lock_state state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_router *router = zui_router_create();
	struct zui_screen *screen = zui_screen_create(&blocking_screen_ops, &state);
	bool detach_returned_early;

	zassert_not_null(host);
	zassert_not_null(router);
	zassert_not_null(screen);

	state.host = host;
	k_sem_init(&state.input_entered, 0, 1);
	k_sem_init(&state.release_input, 0, 1);
	k_sem_init(&state.input_done, 0, 1);
	k_sem_init(&state.detach_done, 0, 1);
	atomic_clear(&state.detach_returned);

	zassert_ok(zui_router_register_screen(router, 1, screen));
	zassert_ok(zui_router_switch(router, 1));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, router));

	(void)k_thread_create(&host_lock_input_thread, host_lock_input_stack,
			      K_THREAD_STACK_SIZEOF(host_lock_input_stack),
			      host_lock_input_entry, &state, NULL, NULL, K_PRIO_PREEMPT(0), 0,
			      K_NO_WAIT);
	zassert_ok(k_sem_take(&state.input_entered, K_SECONDS(1)));

	(void)k_thread_create(&host_lock_detach_thread, host_lock_detach_stack,
			      K_THREAD_STACK_SIZEOF(host_lock_detach_stack),
			      host_lock_detach_entry, &state, NULL, NULL, K_PRIO_PREEMPT(0), 0,
			      K_NO_WAIT);
	k_sleep(K_MSEC(50));
	detach_returned_early = atomic_get(&state.detach_returned) != 0;

	k_sem_give(&state.release_input);
	zassert_ok(k_sem_take(&state.input_done, K_SECONDS(1)));
	zassert_ok(k_sem_take(&state.detach_done, K_SECONDS(1)));
	zassert_false(detach_returned_early);
	zassert_ok(state.input_rc);
	zassert_ok(state.detach_rc);

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(screen);
}

ZTEST(zui_host, test_keypad_fallback_routes_unconsumed_navigation)
{
	struct callback_state state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_router *router = zui_router_create();
	struct zui_screen *screen = new_test_screen(&state);
	const struct zui_input_event keypad_up = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_CLICK,
		.value = '2',
	};
	const struct zui_input_event keypad_dot = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_CLICK,
		.value = '.',
	};
	const struct zui_input_event keypad_star = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_CLICK,
		.value = '*',
	};
	const struct zui_input_event keypad_star_long = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_LONG_PRESS,
		.value = '*',
	};
	const struct zui_input_event keypad_ignored = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_CLICK,
		.value = '1',
	};

	zassert_not_null(host);
	zassert_not_null(router);
	zassert_not_null(screen);
	zassert_ok(zui_router_register_screen(router, 1, screen));
	zassert_ok(zui_router_switch(router, 1));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_DESKTOP, router));

	state.input_consumed = false;
	state.fallback_consumed = true;
	zassert_ok(zui_host_submit_input(host, &keypad_up));
	zassert_equal(state.input_count, 2);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_UP);
	zassert_equal(state.last_action, ZUI_INPUT_ACTION_CLICK);
	zassert_equal(state.last_value, 1);

	state.input_count = 0;
	zassert_ok(zui_host_submit_input(host, &keypad_dot));
	zassert_equal(state.input_count, 2);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_SELECT);
	zassert_equal(state.last_action, ZUI_INPUT_ACTION_CLICK);
	zassert_equal(state.last_value, 1);

	state.input_count = 0;
	zassert_ok(zui_host_submit_input(host, &keypad_star));
	zassert_equal(state.input_count, 2);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_BACK);
	zassert_equal(state.last_action, ZUI_INPUT_ACTION_CLICK);
	zassert_equal(state.last_value, 1);

	state.input_count = 0;
	zassert_ok(zui_host_submit_input(host, &keypad_star_long));
	zassert_equal(state.input_count, 2);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_BACK);
	zassert_equal(state.last_action, ZUI_INPUT_ACTION_LONG_PRESS);
	zassert_equal(state.last_value, 1);

	state.input_count = 0;
	zassert_equal(zui_host_submit_input(host, &keypad_ignored), -ENODATA);
	zassert_equal(state.input_count, 1);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_KEYPAD);
	zassert_equal(state.last_value, '1');

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(screen);
}

ZTEST(zui_host, test_screen_redraw_invalidates_attached_host)
{
	struct callback_state state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_router *router = zui_router_create();
	struct zui_screen *screen = new_test_screen(&state);

	zassert_not_null(host);
	zassert_not_null(router);
	zassert_not_null(screen);

	zassert_ok(zui_host_set_redraw_callback(host, host_redraw, &state));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_DESKTOP, router));
	zassert_ok(zui_router_register_screen(router, 1, screen));

	zassert_ok(zui_screen_request_redraw(screen));
	zassert_equal(state.redraw_count, 1);
	zassert_true(zui_screen_redraw_is_requested(screen));

	zassert_ok(zui_host_detach_router(host, ZUI_LAYER_DESKTOP));
	zassert_ok(zui_screen_request_redraw(screen));
	zassert_equal(state.redraw_count, 1);

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(screen);
}

ZTEST(zui_host, test_host_poll_dispatches_visible_screen_ticks)
{
	struct callback_state desktop_state = {0};
	struct callback_state fullscreen_state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_router *desktop_router = zui_router_create();
	struct zui_router *fullscreen_router = zui_router_create();
	struct zui_screen *desktop_screen = new_test_screen(&desktop_state);
	struct zui_screen *fullscreen_screen = new_test_screen(&fullscreen_state);

	zassert_not_null(host);
	zassert_not_null(desktop_router);
	zassert_not_null(fullscreen_router);
	zassert_not_null(desktop_screen);
	zassert_not_null(fullscreen_screen);

	zassert_equal(zui_host_poll(NULL), -EINVAL);
	zassert_equal(zui_host_next_timeout_ms(NULL), -EINVAL);
	zassert_equal(zui_host_next_timeout_ms(host), -EBUSY);
	zassert_equal(zui_host_poll(host), 0);

	zassert_ok(zui_screen_set_tick_period(desktop_screen, 10));
	zassert_ok(zui_screen_set_tick_period(fullscreen_screen, 10));
	zassert_ok(zui_router_register_screen(desktop_router, 1, desktop_screen));
	zassert_ok(zui_router_register_screen(fullscreen_router, 2, fullscreen_screen));
	zassert_ok(zui_router_switch(desktop_router, 1));
	zassert_ok(zui_router_switch(fullscreen_router, 2));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_DESKTOP, desktop_router));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, fullscreen_router));
	zassert_ok(zui_host_run(host));
	zassert_true(zui_host_next_timeout_ms(host) >= 0);

	k_sleep(K_MSEC(20));
	zassert_equal(zui_host_poll(host), 1);
	zassert_equal(fullscreen_state.event_count, 1);
	zassert_true(fullscreen_state.last_event_code >= 1);
	zassert_equal(desktop_state.event_count, 0);

	zassert_ok(zui_host_set_layer_enabled(host, ZUI_LAYER_FULLSCREEN, false));
	k_sleep(K_MSEC(20));
	zassert_equal(zui_host_poll(host), 1);
	zassert_equal(desktop_state.event_count, 1);

	zassert_ok(zui_host_stop(host));
	k_sleep(K_MSEC(20));
	zassert_equal(zui_host_poll(host), 0);
	zassert_equal(desktop_state.event_count, 1);
	zassert_equal(zui_host_next_timeout_ms(host), -EBUSY);

	zassert_ok(zui_host_run(host));
	zassert_ok(zui_host_set_suspended(host, true));
	zassert_equal(zui_host_poll(host), 0);
	zassert_equal(zui_host_next_timeout_ms(host), -EBUSY);
	zassert_ok(zui_host_set_suspended(host, false));

	zassert_ok(zui_screen_set_tick_period(desktop_screen, 0));
	zassert_equal(zui_host_next_timeout_ms(host), -ENODATA);

	zui_host_destroy(host);
	zui_router_destroy(desktop_router);
	zui_router_destroy(fullscreen_router);
	zui_screen_destroy(desktop_screen);
	zui_screen_destroy(fullscreen_screen);
}

ZTEST(zui_host, test_entered_unattached_screen_redraw_invalidates_default_host)
{
	struct callback_state state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_screen *screen = new_test_screen(&state);

	zassert_not_null(host);
	zassert_not_null(screen);

	zassert_ok(zui_host_set_redraw_callback(host, host_redraw, &state));

	zassert_ok(zui_screen_request_redraw(screen));
	zassert_equal(state.redraw_count, 0);

	zassert_ok(zui_screen_enter(screen));
	zassert_ok(zui_screen_request_redraw(screen));
	zassert_equal(state.redraw_count, 1);

	zui_screen_destroy(screen);
	zui_host_destroy(host);
}

ZTEST(zui_host, test_host_toast_lifetime)
{
	struct zui_host *host = zui_host_create(NULL);
	struct zui_toast_config toast = {
		.title = "Toast",
		.text = "Ready",
		.timeout_ms = 80,
	};
	uint32_t toast_id;

	zassert_not_null(host);

	toast_id = zui_toast_show(host, &toast);
	zassert_not_equal(toast_id, 0);
	zassert_true(zui_toast_is_visible(host, toast_id));
#if CONFIG_ZUI_TOAST_MAX_SLOTS == 1
	zassert_equal(zui_toast_show(host, &toast), 0);
#endif
	zassert_ok(zui_toast_dismiss(host, toast_id));
	zassert_true(zui_toast_is_visible(host, toast_id));
	k_sleep(K_MSEC(400));
	zassert_false(zui_toast_is_visible(host, toast_id));
	zassert_equal(zui_toast_dismiss(host, toast_id), -ENOENT);

	toast_id = zui_toast_show(host, &toast);
	zassert_not_equal(toast_id, 0);
	zassert_true(zui_toast_is_visible(host, toast_id));
	k_sleep(K_MSEC(500));
	zassert_false(zui_toast_is_visible(host, toast_id));

	toast_id = zui_toast_show(host, &toast);
	zassert_not_equal(toast_id, 0);
	zassert_equal(zui_toast_dismiss_all(host), 1);
	k_sleep(K_MSEC(400));
	zassert_false(zui_toast_is_visible(host, toast_id));

	zui_host_destroy(host);
}

ZTEST(zui_host, test_host_toast_blocks_input_until_hidden)
{
	struct callback_state state = {0};
	struct zui_host *host = zui_host_create(NULL);
	struct zui_router *router = zui_router_create();
	struct zui_screen *screen = new_test_screen(&state);
	struct zui_toast_config toast = {
		.title = "Toast",
		.text = "Ready",
		.timeout_ms = 1000,
	};
	struct zui_input_event down_click = {
		.code = ZUI_INPUT_CODE_DOWN,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_input_event select_click = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_input_event back_click = {
		.code = ZUI_INPUT_CODE_BACK,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	uint32_t toast_id;

	zassert_not_null(host);
	zassert_not_null(router);
	zassert_not_null(screen);
	zassert_ok(zui_router_register_screen(router, 1, screen));
	zassert_ok(zui_router_switch(router, 1));
	zassert_ok(zui_host_attach_router(host, ZUI_LAYER_DESKTOP, router));

	toast_id = zui_toast_show(host, &toast);
	zassert_not_equal(toast_id, 0);
	zassert_ok(zui_host_submit_input(host, &down_click));
	zassert_equal(state.input_count, 0);
	zassert_true(zui_toast_is_visible(host, toast_id));

	zassert_ok(zui_host_submit_input(host, &select_click));
	zassert_equal(state.input_count, 0);
	zassert_true(zui_toast_is_visible(host, toast_id));

	zassert_ok(zui_host_submit_input(host, &down_click));
	zassert_equal(state.input_count, 0);
	k_sleep(K_MSEC(400));
	zassert_false(zui_toast_is_visible(host, toast_id));

	zassert_ok(zui_host_submit_input(host, &down_click));
	zassert_equal(state.input_count, 1);

	toast_id = zui_toast_show(host, &toast);
	zassert_not_equal(toast_id, 0);
	zassert_ok(zui_host_submit_input(host, &back_click));
	zassert_equal(state.input_count, 1);
	k_sleep(K_MSEC(400));
	zassert_false(zui_toast_is_visible(host, toast_id));

	zui_host_destroy(host);
	zui_router_destroy(router);
	zui_screen_destroy(screen);
}

ZTEST_SUITE(zui_host, NULL, NULL, NULL, NULL, NULL);
