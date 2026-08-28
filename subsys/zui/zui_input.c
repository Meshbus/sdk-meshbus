/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/zui/input.h>

static atomic_t zui_input_sequence;

static bool zui_input_code_valid(enum zui_input_code code)
{
	return code >= 0 && code < ZUI_INPUT_CODE_COUNT;
}

static bool zui_input_action_valid(enum zui_input_action action)
{
	return action >= 0 && action < ZUI_INPUT_ACTION_COUNT;
}

static uint32_t zui_action_mask_for_code(enum zui_input_code code)
{
	switch (code) {
	case ZUI_INPUT_CODE_UP:
		return ZUI_ACTION_UP;
	case ZUI_INPUT_CODE_DOWN:
		return ZUI_ACTION_DOWN;
	case ZUI_INPUT_CODE_LEFT:
		return ZUI_ACTION_LEFT;
	case ZUI_INPUT_CODE_RIGHT:
		return ZUI_ACTION_RIGHT;
	case ZUI_INPUT_CODE_SELECT:
		return ZUI_ACTION_PRIMARY;
	case ZUI_INPUT_CODE_BACK:
		return ZUI_ACTION_SECONDARY;
	case ZUI_INPUT_CODE_MENU:
		return ZUI_ACTION_MENU;
	default:
		return 0U;
	}
}

static uint32_t zui_action_mask_for_keypad_value(int32_t value)
{
	switch (value) {
	case '2':
		return ZUI_ACTION_UP;
	case '8':
		return ZUI_ACTION_DOWN;
	case '4':
		return ZUI_ACTION_LEFT;
	case '6':
		return ZUI_ACTION_RIGHT;
	case '.':
		return ZUI_ACTION_PRIMARY;
	case '*':
		return ZUI_ACTION_SECONDARY;
	default:
		return 0U;
	}
}

static uint32_t zui_action_mask_for_event(const struct zui_input_event *event)
{
	if (event->code == ZUI_INPUT_CODE_KEYPAD) {
		return zui_action_mask_for_keypad_value(event->value);
	}

	return zui_action_mask_for_code(event->code);
}

static uint32_t zui_action_long_mask_for_event(const struct zui_input_event *event)
{
	if (event->code == ZUI_INPUT_CODE_KEYPAD) {
		return event->value == '*' ? ZUI_ACTION_CANCEL :
					     zui_action_mask_for_keypad_value(event->value);
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_BACK:
	case ZUI_INPUT_CODE_MENU:
	case ZUI_INPUT_CODE_HOME:
		return ZUI_ACTION_CANCEL;
	default:
		return zui_action_mask_for_code(event->code);
	}
}

static int zui_input_code_from_zephyr(uint16_t code, enum zui_input_code *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	switch (code) {
	case INPUT_KEY_UP:
	case INPUT_KEY_2:
	case INPUT_BTN_DPAD_UP:
		*out = ZUI_INPUT_CODE_UP;
		return 0;
	case INPUT_KEY_DOWN:
	case INPUT_KEY_8:
	case INPUT_BTN_DPAD_DOWN:
		*out = ZUI_INPUT_CODE_DOWN;
		return 0;
	case INPUT_KEY_RIGHT:
	case INPUT_KEY_6:
	case INPUT_BTN_DPAD_RIGHT:
		*out = ZUI_INPUT_CODE_RIGHT;
		return 0;
	case INPUT_KEY_LEFT:
	case INPUT_KEY_4:
	case INPUT_BTN_DPAD_LEFT:
		*out = ZUI_INPUT_CODE_LEFT;
		return 0;
	case INPUT_KEY_ENTER:
	case INPUT_KEY_KPENTER:
	case INPUT_KEY_KPDOT:
	case INPUT_BTN_SELECT:
	case INPUT_BTN_SOUTH:
		*out = ZUI_INPUT_CODE_SELECT;
		return 0;
	case INPUT_KEY_BACK:
	case INPUT_KEY_BACKSPACE:
	case INPUT_KEY_ESC:
	case INPUT_KEY_KPASTERISK:
	case INPUT_BTN_BACK:
		*out = ZUI_INPUT_CODE_BACK;
		return 0;
	case INPUT_KEY_MENU:
	case INPUT_BTN_MODE:
	case INPUT_BTN_START:
		*out = ZUI_INPUT_CODE_MENU;
		return 0;
	case INPUT_KEY_HOME:
	case INPUT_BTN_TASK:
		*out = ZUI_INPUT_CODE_HOME;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int zui_input_action_from_zephyr(int32_t value, enum zui_input_action *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	switch (value) {
	case 0:
		*out = ZUI_INPUT_ACTION_RELEASE;
		return 0;
	case 1:
		*out = ZUI_INPUT_ACTION_PRESS;
		return 0;
	case 2:
		*out = ZUI_INPUT_ACTION_CLICK;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int zui_input_wheel_from_zephyr(const struct input_event *src, enum zui_input_code *code,
				       enum zui_input_action *action)
{
	if (src == NULL || code == NULL || action == NULL) {
		return -EINVAL;
	}

	if (src->type != INPUT_EV_REL || src->code != INPUT_REL_WHEEL) {
		return -ENOTSUP;
	}

	if (src->value == 0) {
		return -ENODATA;
	}

	*code = src->value > 0 ? ZUI_INPUT_CODE_RIGHT : ZUI_INPUT_CODE_LEFT;
	*action = ZUI_INPUT_ACTION_CLICK;

	return 0;
}

const char *zui_input_code_name(enum zui_input_code code)
{
	static const char *const names[ZUI_INPUT_CODE_COUNT] = {
		[ZUI_INPUT_CODE_UP] = "up",         [ZUI_INPUT_CODE_DOWN] = "down",
		[ZUI_INPUT_CODE_RIGHT] = "right",   [ZUI_INPUT_CODE_LEFT] = "left",
		[ZUI_INPUT_CODE_SELECT] = "select", [ZUI_INPUT_CODE_BACK] = "back",
		[ZUI_INPUT_CODE_MENU] = "menu",     [ZUI_INPUT_CODE_HOME] = "home",
		[ZUI_INPUT_CODE_KEYPAD] = "keypad",
	};

	return zui_input_code_valid(code) ? names[code] : "unknown";
}

const char *zui_input_action_name(enum zui_input_action action)
{
	static const char *const names[ZUI_INPUT_ACTION_COUNT] = {
		[ZUI_INPUT_ACTION_PRESS] = "press",   [ZUI_INPUT_ACTION_RELEASE] = "release",
		[ZUI_INPUT_ACTION_CLICK] = "click",   [ZUI_INPUT_ACTION_LONG_PRESS] = "long-press",
	};

	return zui_input_action_valid(action) ? names[action] : "unknown";
}

int zui_input_keypad_value_from_zephyr(uint16_t code, int32_t *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	switch (code) {
	case INPUT_KEY_0:
		*out = '0';
		return 0;
	case INPUT_KEY_1:
		*out = '1';
		return 0;
	case INPUT_KEY_2:
		*out = '2';
		return 0;
	case INPUT_KEY_3:
		*out = '3';
		return 0;
	case INPUT_KEY_4:
		*out = '4';
		return 0;
	case INPUT_KEY_5:
		*out = '5';
		return 0;
	case INPUT_KEY_6:
		*out = '6';
		return 0;
	case INPUT_KEY_7:
		*out = '7';
		return 0;
	case INPUT_KEY_8:
		*out = '8';
		return 0;
	case INPUT_KEY_9:
		*out = '9';
		return 0;
	case INPUT_KEY_KPASTERISK:
		*out = '*';
		return 0;
	case INPUT_KEY_KPDOT:
		*out = '.';
		return 0;
	default:
		return -ENOTSUP;
	}
}

void zui_action_state_reset(struct zui_action_state *state)
{
	if (state == NULL) {
		return;
	}

	*state = (struct zui_action_state){0};
}

void zui_action_state_clear_edges(struct zui_action_state *state)
{
	if (state == NULL) {
		return;
	}

	state->pressed = 0U;
	state->released = 0U;
	state->long_pressed = 0U;
}

int zui_action_state_update(struct zui_action_state *state, const struct zui_input_event *event)
{
	uint32_t mask;

	if (state == NULL || event == NULL || !zui_input_code_valid(event->code) ||
	    !zui_input_action_valid(event->action)) {
		return -EINVAL;
	}

	if (event->action == ZUI_INPUT_ACTION_LONG_PRESS) {
		mask = zui_action_long_mask_for_event(event);
		if (mask == 0U) {
			return -ENOTSUP;
		}

		state->long_pressed |= mask;
		return 0;
	}

	mask = zui_action_mask_for_event(event);
	if (mask == 0U) {
		return -ENOTSUP;
	}

	switch (event->action) {
	case ZUI_INPUT_ACTION_PRESS:
		state->pressed |= mask & ~state->down;
		state->down |= mask;
		return 0;
	case ZUI_INPUT_ACTION_RELEASE:
		state->released |= mask & state->down;
		state->down &= ~mask;
		return 0;
	case ZUI_INPUT_ACTION_CLICK:
		state->pressed |= mask;
		state->released |= mask;
		return 0;
	default:
		return -EINVAL;
	}
}

int zui_input_from_zephyr(const struct input_event *src, struct zui_input_event *dst)
{
	enum zui_input_code code;
	enum zui_input_action action;
	int rc;

	if (src == NULL || dst == NULL) {
		return -EINVAL;
	}

	switch (src->type) {
	case INPUT_EV_KEY:
		rc = zui_input_code_from_zephyr(src->code, &code);
		if (rc != 0) {
			return rc;
		}

		rc = zui_input_action_from_zephyr(src->value, &action);
		if (rc != 0) {
			return rc;
		}
		break;
	case INPUT_EV_REL:
		rc = zui_input_wheel_from_zephyr(src, &code, &action);
		if (rc != 0) {
			return rc;
		}
		break;
	default:
		return -ENOTSUP;
	}

	*dst = (struct zui_input_event){
		.sequence = (uint32_t)atomic_inc(&zui_input_sequence) + 1U,
		.code = code,
		.action = action,
		.value = src->value,
	};

	return 0;
}
