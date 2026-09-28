/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_INPUT_INTERNAL_H_
#define MBS_INPUT_INTERNAL_H_

#include <stdbool.h>

#include <zephyr/input/input.h>
#include <input/input.h>

static inline bool mbs_input_key_is_ok_code(uint16_t code)
{
	return code == INPUT_BTN_SELECT || code == INPUT_KEY_ENTER || code == INPUT_KEY_KPENTER ||
	       code == INPUT_KEY_KPDOT;
}

static inline bool mbs_input_key_is_back_code(uint16_t code)
{
	if (code == INPUT_BTN_BACK || code == INPUT_KEY_ESC || code == INPUT_KEY_BACKSPACE ||
	    code == INPUT_KEY_KPASTERISK) {
		return true;
	}

#if defined(INPUT_KEY_BACK)
	if (code == INPUT_KEY_BACK) {
		return true;
	}
#endif

	return false;
}

static inline bool mbs_input_key_is_nav_code(uint16_t code)
{
	switch (code) {
	case INPUT_KEY_UP:
	case INPUT_KEY_DOWN:
	case INPUT_KEY_LEFT:
	case INPUT_KEY_RIGHT:
	case INPUT_BTN_DPAD_UP:
	case INPUT_BTN_DPAD_DOWN:
	case INPUT_BTN_DPAD_LEFT:
	case INPUT_BTN_DPAD_RIGHT:
		return true;
	default:
		return false;
	}
}

static inline bool mbs_input_key_is_t9_nav_code(uint16_t code)
{
	switch (code) {
	case INPUT_KEY_2:
	case INPUT_KEY_4:
	case INPUT_KEY_6:
	case INPUT_KEY_8:
		return true;
	default:
		return false;
	}
}

static inline bool mbs_input_key_uses_long_press(uint16_t code)
{
	return mbs_input_key_is_ok_code(code) || mbs_input_key_is_back_code(code) ||
	       mbs_input_key_is_t9_nav_code(code);
}

#endif /* MBS_INPUT_INTERNAL_H_ */
