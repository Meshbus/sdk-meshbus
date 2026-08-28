// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/zui/zui.h>

struct callback_state {
	int draw_count;
	int input_count;
	int event_count;
	int enter_count;
	int exit_count;
	int order_count;
	char order[8];
	bool input_consumed;
	bool event_consumed;
	enum zui_input_code last_code;
	uint32_t last_event_code;
};

struct forward_state {
	struct zui_screen *target;
	int draw_rc;
	int input_rc;
	int event_rc;
};

static void record_order(struct callback_state *state, char token)
{
	if (state->order_count < ARRAY_SIZE(state->order)) {
		state->order[state->order_count] = token;
	}
	state->order_count++;
}

static void screen_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct callback_state *state = user_data;

	zassert_not_null(draw);
	state->draw_count++;
}

static bool screen_input(const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	zassert_not_null(event);
	state->input_count++;
	state->last_code = event->code;
	return state->input_consumed;
}

static bool screen_event(const struct zui_screen_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	zassert_not_null(event);
	state->event_count++;
	state->last_event_code = event->code;
	return state->event_consumed;
}

static void screen_enter(void *user_data)
{
	struct callback_state *state = user_data;

	state->enter_count++;
	record_order(state, 'E');
}

static void screen_exit(void *user_data)
{
	struct callback_state *state = user_data;

	state->exit_count++;
	record_order(state, 'X');
}

static const struct zui_screen_ops callback_ops = {
	.draw = screen_draw,
	.input = screen_input,
	.enter = screen_enter,
	.exit = screen_exit,
	.event = screen_event,
};

static void forward_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct forward_state *state = user_data;

	state->draw_rc = zui_screen_draw(state->target, draw);
}

static bool forward_input(const struct zui_input_event *event, void *user_data)
{
	struct forward_state *state = user_data;

	state->input_rc = zui_screen_submit_input(state->target, event);
	return state->input_rc > 0;
}

static bool forward_event(const struct zui_screen_event *event, void *user_data)
{
	struct forward_state *state = user_data;

	state->event_rc = zui_screen_dispatch_event(state->target, event);
	return state->event_rc > 0;
}

static const struct zui_screen_ops forward_ops = {
	.draw = forward_draw,
	.input = forward_input,
	.event = forward_event,
};

ZTEST(zui_screen, test_inert_screen_and_invalid_arguments)
{
	struct callback_state state = {0};
	struct zui_screen *screen = zui_screen_create(NULL, &state);
	struct zui_input_event input = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_CUSTOM,
		.code = 77,
	};

	zassert_not_null(screen);
	zassert_equal(zui_screen_get_user_data(screen), &state);
	zassert_false(zui_screen_is_entered(screen));

	zassert_is_null(zui_screen_get_user_data(NULL));
	zassert_false(zui_screen_redraw_is_requested(NULL));
	zassert_equal(zui_screen_request_redraw(NULL), -EINVAL);
	zassert_equal(zui_screen_clear_redraw(NULL), -EINVAL);
	zassert_equal(zui_screen_set_tick_period(NULL, 10), -EINVAL);
	zassert_equal(zui_screen_tick_period(NULL), 0);
	zassert_equal(zui_screen_draw(NULL, (struct zui_draw_ctx *)&state), -EINVAL);
	zassert_equal(zui_screen_draw(screen, NULL), -EINVAL);
	zassert_equal(zui_screen_submit_input(NULL, &input), -EINVAL);
	zassert_equal(zui_screen_submit_input(screen, NULL), -EINVAL);
	zassert_equal(zui_screen_dispatch_event(NULL, &event), -EINVAL);
	zassert_equal(zui_screen_dispatch_event(screen, NULL), -EINVAL);
	zassert_equal(zui_screen_enter(NULL), -EINVAL);
	zassert_equal(zui_screen_exit(NULL), -EINVAL);

	zassert_equal(zui_screen_draw(screen, (struct zui_draw_ctx *)&state), -ENOSYS);
	zassert_equal(zui_screen_submit_input(screen, &input), -ENOSYS);
	zassert_equal(zui_screen_dispatch_event(screen, &event), -ENOSYS);
	zassert_ok(zui_screen_enter(screen));
	zassert_true(zui_screen_is_entered(screen));
	zassert_ok(zui_screen_set_tick_period(screen, 25));
	zassert_equal(zui_screen_tick_period(screen), 25);
	zassert_ok(zui_screen_set_tick_period(screen, 0));
	zassert_equal(zui_screen_tick_period(screen), 0);
	zassert_ok(zui_screen_exit(screen));
	zassert_false(zui_screen_is_entered(screen));

	zui_screen_destroy(screen);
}

ZTEST(zui_screen, test_callbacks_and_redraw_state)
{
	struct callback_state state = {
		.input_consumed = true,
		.event_consumed = true,
	};
	struct zui_input_event input = {
		.code = ZUI_INPUT_CODE_BACK,
		.action = ZUI_INPUT_ACTION_LONG_PRESS,
	};
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_BACK,
		.code = 101,
	};
	struct zui_screen *screen = zui_screen_create(&callback_ops, &state);

	zassert_not_null(screen);

	zassert_ok(zui_screen_request_redraw(screen));
	zassert_true(zui_screen_redraw_is_requested(screen));
	zassert_ok(zui_screen_draw(screen, (struct zui_draw_ctx *)&state));
	zassert_equal(state.draw_count, 1);
	zassert_false(zui_screen_redraw_is_requested(screen));

	zassert_ok(zui_screen_request_redraw(screen));
	zassert_ok(zui_screen_clear_redraw(screen));
	zassert_false(zui_screen_redraw_is_requested(screen));

	zassert_equal(zui_screen_submit_input(screen, &input), 1);
	zassert_equal(state.input_count, 1);
	zassert_equal(state.last_code, ZUI_INPUT_CODE_BACK);
	state.input_consumed = false;
	zassert_equal(zui_screen_submit_input(screen, &input), 0);

	zassert_equal(zui_screen_dispatch_event(screen, &event), 1);
	zassert_equal(state.event_count, 1);
	zassert_equal(state.last_event_code, 101);
	state.event_consumed = false;
	zassert_equal(zui_screen_dispatch_event(screen, &event), 0);

	zui_screen_destroy(screen);
}

ZTEST(zui_screen, test_lifecycle_order_and_destroy_exit)
{
	struct callback_state state = {0};
	struct zui_screen *screen = zui_screen_create(&callback_ops, &state);

	zassert_not_null(screen);

	zassert_ok(zui_screen_enter(screen));
	zassert_ok(zui_screen_enter(screen));
	zassert_true(zui_screen_is_entered(screen));
	zassert_equal(state.enter_count, 1);
	zassert_equal(state.exit_count, 0);

	zassert_ok(zui_screen_exit(screen));
	zassert_ok(zui_screen_exit(screen));
	zassert_false(zui_screen_is_entered(screen));
	zassert_equal(state.enter_count, 1);
	zassert_equal(state.exit_count, 1);
	zassert_equal(state.order[0], 'E');
	zassert_equal(state.order[1], 'X');

	zassert_ok(zui_screen_enter(screen));
	zui_screen_destroy(screen);
	zassert_equal(state.enter_count, 2);
	zassert_equal(state.exit_count, 2);
	zassert_equal(state.order[2], 'E');
	zassert_equal(state.order[3], 'X');
}

ZTEST(zui_screen, test_manual_forwarding_for_composite_screens)
{
	struct callback_state child_state = {
		.input_consumed = true,
		.event_consumed = true,
	};
	struct forward_state parent_state = {0};
	struct zui_input_event input = {
		.code = ZUI_INPUT_CODE_MENU,
		.action = ZUI_INPUT_ACTION_CLICK,
	};
	struct zui_screen_event event = {
		.type = ZUI_SCREEN_EVENT_TICK,
		.code = 5,
	};
	struct zui_screen *child = zui_screen_create(&callback_ops, &child_state);
	struct zui_screen *parent;

	zassert_not_null(child);
	parent_state.target = child;
	parent = zui_screen_create(&forward_ops, &parent_state);
	zassert_not_null(parent);

	zassert_ok(zui_screen_draw(parent, (struct zui_draw_ctx *)&child_state));
	zassert_ok(parent_state.draw_rc);
	zassert_equal(child_state.draw_count, 1);

	zassert_equal(zui_screen_submit_input(parent, &input), 1);
	zassert_equal(parent_state.input_rc, 1);
	zassert_equal(child_state.input_count, 1);
	zassert_equal(child_state.last_code, ZUI_INPUT_CODE_MENU);

	zassert_equal(zui_screen_dispatch_event(parent, &event), 1);
	zassert_equal(parent_state.event_rc, 1);
	zassert_equal(child_state.event_count, 1);
	zassert_equal(child_state.last_event_code, 5);

	zui_screen_destroy(parent);
	zui_screen_destroy(child);
}

ZTEST_SUITE(zui_screen, NULL, NULL, NULL, NULL, NULL);
