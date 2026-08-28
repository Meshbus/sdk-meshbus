// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/ztest.h>
#include <zephyr/zui/zui.h>

static void assert_input(uint16_t key_code, int32_t value, enum zui_input_code expected_code,
			 enum zui_input_action expected_action)
{
	struct input_event src = {
		.type = INPUT_EV_KEY,
		.code = key_code,
		.value = value,
	};
	struct zui_input_event dst = {0};

	zassert_ok(zui_input_from_zephyr(&src, &dst));
	zassert_equal(dst.code, expected_code);
	zassert_equal(dst.action, expected_action);
	zassert_equal(dst.value, value);
	zassert_true(dst.sequence > 0);
}

static void assert_wheel(int32_t value, enum zui_input_code expected_code)
{
	struct input_event src = {
		.type = INPUT_EV_REL,
		.code = INPUT_REL_WHEEL,
		.value = value,
	};
	struct zui_input_event dst = {0};

	zassert_ok(zui_input_from_zephyr(&src, &dst));
	zassert_equal(dst.code, expected_code);
	zassert_equal(dst.action, ZUI_INPUT_ACTION_CLICK);
	zassert_equal(dst.value, value);
	zassert_true(dst.sequence > 0);
}

static void assert_keypad_value(uint16_t key_code, char expected)
{
	int32_t value;

	zassert_ok(zui_input_keypad_value_from_zephyr(key_code, &value));
	zassert_equal(value, expected);
}

static void assert_action_from_key(uint16_t key_code, uint32_t expected_action)
{
	struct input_event src = {
		.type = INPUT_EV_KEY,
		.code = key_code,
		.value = 1,
	};
	struct zui_input_event event = {0};
	struct zui_action_state state = {0};

	zassert_ok(zui_input_from_zephyr(&src, &event));
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, expected_action);
	zassert_equal(state.pressed, expected_action);
}

static void assert_action_from_keypad_value(int32_t value, enum zui_input_action input_action,
					    uint32_t expected_action)
{
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = input_action,
		.value = value,
	};
	struct zui_action_state state = {0};

	zassert_ok(zui_action_state_update(&state, &event));
	if (input_action == ZUI_INPUT_ACTION_LONG_PRESS) {
		zassert_equal(state.long_pressed, expected_action);
	} else {
		zassert_equal(state.pressed, expected_action);
		zassert_equal(state.released, expected_action);
	}
}

static void assert_no_action_from_keypad_value(int32_t value, enum zui_input_action input_action)
{
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = input_action,
		.value = value,
	};
	struct zui_action_state state = {0};

	zassert_equal(zui_action_state_update(&state, &event), -ENOTSUP);
}

ZTEST(zui_input, test_key_code_mapping)
{
	assert_input(INPUT_KEY_UP, 1, ZUI_INPUT_CODE_UP, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_2, 1, ZUI_INPUT_CODE_UP, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_BTN_DPAD_DOWN, 1, ZUI_INPUT_CODE_DOWN, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_8, 1, ZUI_INPUT_CODE_DOWN, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_RIGHT, 1, ZUI_INPUT_CODE_RIGHT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_6, 1, ZUI_INPUT_CODE_RIGHT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_BTN_DPAD_LEFT, 1, ZUI_INPUT_CODE_LEFT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_4, 1, ZUI_INPUT_CODE_LEFT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_ENTER, 1, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_KPDOT, 1, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_BTN_SELECT, 1, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_BACKSPACE, 1, ZUI_INPUT_CODE_BACK, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_KPASTERISK, 1, ZUI_INPUT_CODE_BACK, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_BTN_BACK, 1, ZUI_INPUT_CODE_BACK, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_MENU, 1, ZUI_INPUT_CODE_MENU, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_HOME, 1, ZUI_INPUT_CODE_HOME, ZUI_INPUT_ACTION_PRESS);
}

ZTEST(zui_input, test_t9_keypad_action_mapping)
{
	assert_action_from_key(INPUT_KEY_2, ZUI_ACTION_UP);
	assert_action_from_key(INPUT_KEY_8, ZUI_ACTION_DOWN);
	assert_action_from_key(INPUT_KEY_4, ZUI_ACTION_LEFT);
	assert_action_from_key(INPUT_KEY_6, ZUI_ACTION_RIGHT);
	assert_action_from_key(INPUT_KEY_KPDOT, ZUI_ACTION_PRIMARY);
	assert_action_from_key(INPUT_KEY_KPASTERISK, ZUI_ACTION_SECONDARY);

	assert_action_from_keypad_value('2', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_UP);
	assert_action_from_keypad_value('8', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_DOWN);
	assert_action_from_keypad_value('4', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_LEFT);
	assert_action_from_keypad_value('6', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_RIGHT);
	assert_action_from_keypad_value('.', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_PRIMARY);
	assert_action_from_keypad_value('*', ZUI_INPUT_ACTION_CLICK, ZUI_ACTION_SECONDARY);
	assert_action_from_keypad_value('*', ZUI_INPUT_ACTION_LONG_PRESS, ZUI_ACTION_CANCEL);
	assert_no_action_from_keypad_value('5', ZUI_INPUT_ACTION_CLICK);
}

ZTEST(zui_input, test_keypad_code_mapping)
{
	int32_t value;

	assert_keypad_value(INPUT_KEY_0, '0');
	assert_keypad_value(INPUT_KEY_2, '2');
	assert_keypad_value(INPUT_KEY_9, '9');
	assert_keypad_value(INPUT_KEY_KPASTERISK, '*');
	assert_keypad_value(INPUT_KEY_KPDOT, '.');
	zassert_equal(zui_input_keypad_value_from_zephyr(INPUT_KEY_A, NULL), -EINVAL);

	zassert_equal(zui_input_keypad_value_from_zephyr(INPUT_KEY_A, &value), -ENOTSUP);
}

ZTEST(zui_input, test_wheel_mapping)
{
	assert_wheel(1, ZUI_INPUT_CODE_RIGHT);
	assert_wheel(3, ZUI_INPUT_CODE_RIGHT);
	assert_wheel(-1, ZUI_INPUT_CODE_LEFT);
	assert_wheel(-2, ZUI_INPUT_CODE_LEFT);
}

ZTEST(zui_input, test_action_mapping)
{
	assert_input(INPUT_KEY_ENTER, 1, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_PRESS);
	assert_input(INPUT_KEY_ENTER, 0, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_RELEASE);
	assert_input(INPUT_KEY_ENTER, 2, ZUI_INPUT_CODE_SELECT, ZUI_INPUT_ACTION_CLICK);
}

ZTEST(zui_input, test_names)
{
	zassert_str_equal(zui_input_code_name(ZUI_INPUT_CODE_UP), "up");
	zassert_str_equal(zui_input_code_name(ZUI_INPUT_CODE_SELECT), "select");
	zassert_str_equal(zui_input_code_name(ZUI_INPUT_CODE_MENU), "menu");
	zassert_str_equal(zui_input_code_name(ZUI_INPUT_CODE_KEYPAD), "keypad");
	zassert_str_equal(zui_input_code_name((enum zui_input_code)99), "unknown");

	zassert_str_equal(zui_input_action_name(ZUI_INPUT_ACTION_PRESS), "press");
	zassert_str_equal(zui_input_action_name(ZUI_INPUT_ACTION_CLICK), "click");
	zassert_str_equal(zui_input_action_name(ZUI_INPUT_ACTION_LONG_PRESS), "long-press");
	zassert_str_equal(zui_input_action_name((enum zui_input_action)99), "unknown");
}

ZTEST(zui_input, test_invalid_inputs)
{
	struct input_event src = {
		.type = INPUT_EV_KEY,
		.code = INPUT_KEY_ENTER,
		.value = 1,
	};
	struct zui_input_event dst;

	zassert_equal(zui_input_from_zephyr(NULL, &dst), -EINVAL);
	zassert_equal(zui_input_from_zephyr(&src, NULL), -EINVAL);

	src.type = INPUT_EV_REL;
	src.code = INPUT_REL_WHEEL;
	src.value = 0;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENODATA);

	src.type = INPUT_EV_REL;
	src.code = INPUT_REL_X;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENOTSUP);

	src.type = INPUT_EV_KEY;
	src.code = INPUT_KEY_A;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENOTSUP);

	src.code = INPUT_KEY_1;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENOTSUP);

	src.code = INPUT_KEY_0;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENOTSUP);

	src.code = INPUT_KEY_ENTER;
	src.value = 3;
	zassert_equal(zui_input_from_zephyr(&src, &dst), -ENOTSUP);
}

ZTEST(zui_input, test_host_submit_uses_converted_event)
{
	struct input_event src = {
		.type = INPUT_EV_KEY,
		.code = INPUT_KEY_ENTER,
		.value = 1,
	};
	struct zui_input_event dst;

	zassert_ok(zui_input_from_zephyr(&src, &dst));
	zassert_equal(dst.code, ZUI_INPUT_CODE_SELECT);
	zassert_equal(dst.action, ZUI_INPUT_ACTION_PRESS);
}

ZTEST(zui_input, test_action_state_tracks_edges_and_down_mask)
{
	struct zui_action_state state = {0};
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_SELECT,
		.action = ZUI_INPUT_ACTION_PRESS,
	};

	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, ZUI_ACTION_PRIMARY);
	zassert_equal(state.pressed, ZUI_ACTION_PRIMARY);
	zassert_equal(state.released, 0);

	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, ZUI_ACTION_PRIMARY);
	zassert_equal(state.pressed, ZUI_ACTION_PRIMARY);

	zui_action_state_clear_edges(&state);
	zassert_equal(state.down, ZUI_ACTION_PRIMARY);
	zassert_equal(state.pressed, 0);
	zassert_equal(state.released, 0);
	zassert_equal(state.long_pressed, 0);

	event.action = ZUI_INPUT_ACTION_RELEASE;
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, 0);
	zassert_equal(state.pressed, 0);
	zassert_equal(state.released, ZUI_ACTION_PRIMARY);

	zui_action_state_reset(&state);
	zassert_equal(state.down, 0);
	zassert_equal(state.pressed, 0);
	zassert_equal(state.released, 0);
}

ZTEST(zui_input, test_action_state_click_and_long_press_mapping)
{
	struct zui_action_state state = {0};
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_BACK,
		.action = ZUI_INPUT_ACTION_CLICK,
	};

	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, 0);
	zassert_equal(state.pressed, ZUI_ACTION_SECONDARY);
	zassert_equal(state.released, ZUI_ACTION_SECONDARY);

	zui_action_state_clear_edges(&state);
	event.action = ZUI_INPUT_ACTION_PRESS;
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, ZUI_ACTION_SECONDARY);

	event.action = ZUI_INPUT_ACTION_LONG_PRESS;
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.down, ZUI_ACTION_SECONDARY);
	zassert_equal(state.long_pressed, ZUI_ACTION_CANCEL);

	zui_action_state_clear_edges(&state);
	event.code = ZUI_INPUT_CODE_MENU;
	event.action = ZUI_INPUT_ACTION_CLICK;
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.pressed, ZUI_ACTION_MENU);
	zassert_equal(state.released, ZUI_ACTION_MENU);

	zui_action_state_clear_edges(&state);
	event.code = ZUI_INPUT_CODE_HOME;
	event.action = ZUI_INPUT_ACTION_LONG_PRESS;
	zassert_ok(zui_action_state_update(&state, &event));
	zassert_equal(state.long_pressed, ZUI_ACTION_CANCEL);
}

ZTEST(zui_input, test_action_state_invalid_and_unmapped_events)
{
	struct zui_action_state state = {0};
	struct zui_input_event event = {
		.code = ZUI_INPUT_CODE_KEYPAD,
		.action = ZUI_INPUT_ACTION_CLICK,
		.value = '1',
	};

	zassert_equal(zui_action_state_update(NULL, &event), -EINVAL);
	zassert_equal(zui_action_state_update(&state, NULL), -EINVAL);
	zassert_equal(zui_action_state_update(&state, &event), -ENOTSUP);

	event.action = ZUI_INPUT_ACTION_LONG_PRESS;
	zassert_equal(zui_action_state_update(&state, &event), -ENOTSUP);

	event.code = (enum zui_input_code)99;
	event.action = ZUI_INPUT_ACTION_CLICK;
	zassert_equal(zui_action_state_update(&state, &event), -EINVAL);

	zui_action_state_reset(NULL);
	zui_action_state_clear_edges(NULL);
}

ZTEST_SUITE(zui_input, NULL, NULL, NULL, NULL, NULL);
