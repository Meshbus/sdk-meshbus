/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI input event API
 */

#ifndef ZEPHYR_INCLUDE_ZUI_INPUT_H_
#define ZEPHYR_INCLUDE_ZUI_INPUT_H_

#include <stdint.h>

struct input_event;

#ifdef __cplusplus
extern "C" {
#endif

enum zui_input_code {
	ZUI_INPUT_CODE_UP = 0,
	ZUI_INPUT_CODE_DOWN = 1,
	ZUI_INPUT_CODE_RIGHT = 2,
	ZUI_INPUT_CODE_LEFT = 3,
	ZUI_INPUT_CODE_SELECT = 4,
	ZUI_INPUT_CODE_BACK = 5,
	ZUI_INPUT_CODE_MENU,
	ZUI_INPUT_CODE_HOME,
	ZUI_INPUT_CODE_KEYPAD,
	ZUI_INPUT_CODE_COUNT,
};

enum zui_input_action {
	ZUI_INPUT_ACTION_PRESS = 0,
	ZUI_INPUT_ACTION_RELEASE = 1,
	ZUI_INPUT_ACTION_CLICK = 2,
	ZUI_INPUT_ACTION_LONG_PRESS = 3,
	ZUI_INPUT_ACTION_COUNT,
};

struct zui_input_event {
	uint32_t sequence;
	enum zui_input_code code;
	enum zui_input_action action;
	int32_t value;
};

enum zui_action {
	ZUI_ACTION_UP = (1U << 0),
	ZUI_ACTION_DOWN = (1U << 1),
	ZUI_ACTION_LEFT = (1U << 2),
	ZUI_ACTION_RIGHT = (1U << 3),
	ZUI_ACTION_PRIMARY = (1U << 4),
	ZUI_ACTION_SECONDARY = (1U << 5),
	ZUI_ACTION_MENU = (1U << 6),
	ZUI_ACTION_CANCEL = (1U << 7),
};

struct zui_action_state {
	uint32_t down;
	uint32_t pressed;
	uint32_t released;
	uint32_t long_pressed;
};

const char *zui_input_code_name(enum zui_input_code code);
const char *zui_input_action_name(enum zui_input_action action);
int zui_input_from_zephyr(const struct input_event *src, struct zui_input_event *dst);
int zui_input_keypad_value_from_zephyr(uint16_t code, int32_t *out);

void zui_action_state_reset(struct zui_action_state *state);

/**
 * Clear one-frame edge masks while preserving the current down mask.
 */
void zui_action_state_clear_edges(struct zui_action_state *state);

/**
 * Apply a ZUI input event to a default action state.
 *
 * Default mapping: D-pad and T9 2/8/4/6 keys map to direction bits, SELECT
 * plus T9 5/# maps to PRIMARY, and short BACK plus T9 star maps to SECONDARY.
 * Long BACK/MENU/HOME or long T9 star maps to CANCEL. PRESS/RELEASE update
 * `down` plus edge masks. CLICK updates pressed and released for one frame
 * without changing `down`. LONG_PRESS updates only `long_pressed`.
 */
int zui_action_state_update(struct zui_action_state *state, const struct zui_input_event *event);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZUI_INPUT_H_ */
