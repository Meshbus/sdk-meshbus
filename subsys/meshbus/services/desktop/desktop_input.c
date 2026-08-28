/* SPDX-License-Identifier: Apache-2.0 */

#include "desktop_private.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_ZBUS) && defined(CONFIG_MESHBUS_INPUT) && defined(CONFIG_MESHBUS_DESKTOP)

#include <zephyr/dt-bindings/input/input-event-codes.h>
#if defined(CONFIG_MESHBUS_DISPLAY)
#include <zephyr/meshbus/display.h>
#endif
#include <zephyr/meshbus/input.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/zui/zui.h>

LOG_MODULE_REGISTER(meshbus_desktop_input_bridge, CONFIG_MESHBUS_DESKTOP_LOG_LEVEL);

static atomic_t event_sequence;
static atomic_t pressed_mask;
static atomic_t press_drop_count;
static atomic_t action_drop_count;
static atomic_t release_drop_count;
#if defined(CONFIG_MESHBUS_DISPLAY)
static atomic_t wake_suppress_armed;
static atomic_t wake_suppress_type = ATOMIC_INIT(-1);
static atomic_t wake_suppress_code = ATOMIC_INIT(-1);
static atomic_t wake_suppress_wait_release;
#endif

static int desktop_input_put(enum zui_input_code code, enum zui_input_action action, int32_t value)
{
	struct zui_input_event ev = {
		.sequence = (uint32_t)atomic_inc(&event_sequence),
		.code = code,
		.action = action,
		.value = value,
	};

	struct zui_desktop *desktop = zui_desktop_get_instance();

	if (desktop == NULL || desktop->host == NULL) {
		return -ENODEV;
	}

	return zui_desktop_submit_input(desktop, &ev);
}

static void desktop_input_record_drop(enum zui_input_code code,
				      enum zui_input_action action, int ret)
{
	atomic_t *counter;
	const char *kind;
	uint32_t count;

	switch (action) {
	case ZUI_INPUT_ACTION_PRESS:
		counter = &press_drop_count;
		kind = "press";
		break;
	case ZUI_INPUT_ACTION_RELEASE:
		counter = &release_drop_count;
		kind = "release";
		break;
	default:
		counter = &action_drop_count;
		kind = "action";
		break;
	}

	count = (uint32_t)atomic_inc(counter) + 1U;
	if ((count & (count - 1U)) == 0U) {
		LOG_WRN("Dropped Desktop input %s: code=%u err=%d total=%u",
			kind, code, ret, count);
	}
}

static int desktop_input_submit(enum zui_input_code code,
				enum zui_input_action action, int32_t value)
{
	int ret = desktop_input_put(code, action, value);

	if (ret != 0) {
		desktop_input_record_drop(code, action, ret);
	}

	return ret;
}

static bool code_to_zui_code(uint16_t code, enum zui_input_code *out_code)
{
	if (out_code == NULL) {
		return false;
	}

	switch (code) {
	case INPUT_BTN_BACK:
	case INPUT_KEY_ESC:
	case INPUT_KEY_BACKSPACE:
	case INPUT_KEY_KPASTERISK:
		*out_code = ZUI_INPUT_CODE_BACK;
		return true;
	case INPUT_BTN_SELECT:
	case INPUT_KEY_ENTER:
	case INPUT_KEY_KPDOT:
		*out_code = ZUI_INPUT_CODE_SELECT;
		return true;

	case INPUT_KEY_UP:
	case INPUT_KEY_2:
	case INPUT_BTN_DPAD_UP:
		*out_code = ZUI_INPUT_CODE_UP;
		return true;
	case INPUT_KEY_DOWN:
	case INPUT_KEY_8:
	case INPUT_BTN_DPAD_DOWN:
		*out_code = ZUI_INPUT_CODE_DOWN;
		return true;
	case INPUT_KEY_LEFT:
	case INPUT_KEY_4:
	case INPUT_BTN_DPAD_LEFT:
		*out_code = ZUI_INPUT_CODE_LEFT;
		return true;
	case INPUT_KEY_RIGHT:
	case INPUT_KEY_6:
	case INPUT_BTN_DPAD_RIGHT:
		*out_code = ZUI_INPUT_CODE_RIGHT;
		return true;
	default:
		return false;
	}
}

static bool meshbus_act_evt_to_zui(const struct meshbus_input_act_event *in,
				   enum zui_input_code *out_code,
				   enum zui_input_action *out_action,
				   int32_t *out_value)
{
	if (in == NULL || out_code == NULL || out_action == NULL || out_value == NULL) {
		return false;
	}

	enum zui_input_code code;
	enum zui_input_action action;
	int32_t value = 1;

	switch (in->action) {
	case INPUT_ACT_SCROLL_CW:
		code = ZUI_INPUT_CODE_RIGHT;
		action = ZUI_INPUT_ACTION_CLICK;
		value = 1;
		break;
	case INPUT_ACT_SCROLL_CCW:
		code = ZUI_INPUT_CODE_LEFT;
		action = ZUI_INPUT_ACTION_CLICK;
		value = -1;
		break;
	case INPUT_ACT_KEY_SHORT:
		if (zui_input_keypad_value_from_zephyr(in->code, &value) == 0) {
			code = ZUI_INPUT_CODE_KEYPAD;
			action = ZUI_INPUT_ACTION_CLICK;
			break;
		}
		value = 1;
		if (!code_to_zui_code(in->code, &code)) {
			return false;
		}
		action = ZUI_INPUT_ACTION_CLICK;
		break;
	case INPUT_ACT_KEY_LONG:
		if (zui_input_keypad_value_from_zephyr(in->code, &value) == 0) {
			code = ZUI_INPUT_CODE_KEYPAD;
			action = ZUI_INPUT_ACTION_LONG_PRESS;
			break;
		}
		value = 1;
		if (!code_to_zui_code(in->code, &code)) {
			return false;
		}
		action = ZUI_INPUT_ACTION_LONG_PRESS;
		break;
	default:
		return false;
	}

	*out_code = code;
	*out_action = action;
	*out_value = value;

	return true;
}

#if defined(CONFIG_MESHBUS_DISPLAY)
static void wake_suppress_arm(uint8_t type, uint16_t code, bool wait_release)
{
	atomic_set(&wake_suppress_type, (atomic_val_t)type);
	atomic_set(&wake_suppress_code, (atomic_val_t)code);
	atomic_set(&wake_suppress_wait_release, wait_release ? 1 : 0);
	atomic_set(&wake_suppress_armed, 1);
}

static void wake_suppress_disarm(void)
{
	atomic_set(&wake_suppress_armed, 0);
	atomic_set(&wake_suppress_wait_release, 0);
	atomic_set(&wake_suppress_type, -1);
	atomic_set(&wake_suppress_code, -1);
}

static bool wake_suppress_is_match(uint8_t type, uint16_t code)
{
	atomic_val_t suppress_type;
	atomic_val_t suppress_code;

	if (atomic_get(&wake_suppress_armed) == 0) {
		return false;
	}

	suppress_type = atomic_get(&wake_suppress_type);
	suppress_code = atomic_get(&wake_suppress_code);
	if (suppress_type < 0 || suppress_code < 0) {
		return false;
	}

	return ((uint8_t)suppress_type == type) && ((uint16_t)suppress_code == code);
}

static bool meshbus_desktop_input_is_scroll_act(const struct meshbus_input_act_event *evt)
{
	return evt != NULL && evt->type == INPUT_EV_REL &&
	       (evt->action == INPUT_ACT_SCROLL_CW || evt->action == INPUT_ACT_SCROLL_CCW);
}

static bool meshbus_desktop_input_should_consume_act(const struct meshbus_input_act_event *evt)
{
	if (evt == NULL) {
		return false;
	}

	bool is_scroll = meshbus_desktop_input_is_scroll_act(evt);

	if (!meshbus_display_is_active()) {
		meshbus_display_active(true);
		if (is_scroll) {
			wake_suppress_disarm();
			return false;
		}
		wake_suppress_arm(evt->type, evt->code, evt->type == INPUT_EV_KEY);
		return true;
	}

	if (!wake_suppress_is_match(evt->type, evt->code)) {
		return false;
	}

	if (is_scroll) {
		wake_suppress_disarm();
		return false;
	}

	/*
	 * Key wake suppression remains armed until raw RELEASE so held directional
	 * keys do not start navigating immediately after waking the display.
	 */
	if (atomic_get(&wake_suppress_wait_release) == 0) {
		wake_suppress_disarm();
	}
	return true;
}

static bool meshbus_desktop_input_should_consume_raw(const struct meshbus_input_event *evt)
{
	if (evt == NULL) {
		return false;
	}

	if (!meshbus_display_is_active()) {
		if (evt->type == INPUT_EV_KEY) {
			/*
			 * Ignore trailing RELEASE while sleeping; otherwise the same key
			 * used to enter sleep immediately wakes the display again.
			 */
			if (evt->value == 0) {
				return true;
			}

			meshbus_display_active(true);
			wake_suppress_arm(evt->type, evt->code, true);
			return true;
		}

		meshbus_display_active(true);
		wake_suppress_disarm();
		return true;
	}

	if (!wake_suppress_is_match(evt->type, evt->code)) {
		return false;
	}

	if (evt->type != INPUT_EV_KEY ||
	    atomic_get(&wake_suppress_wait_release) == 0 || evt->value == 0) {
		wake_suppress_disarm();
	}

	return true;
}
#else
static bool meshbus_desktop_input_should_consume_act(const struct meshbus_input_act_event *evt)
{
	ARG_UNUSED(evt);
	return false;
}

static bool meshbus_desktop_input_should_consume_raw(const struct meshbus_input_event *evt)
{
	ARG_UNUSED(evt);
	return false;
}
#endif

static void meshbus_desktop_input_act_cb(const struct zbus_channel *chan)
{
	const struct meshbus_input_act_event *evt;
	enum zui_input_code code;
	enum zui_input_action action;
	int32_t value;
	bool was_pressed;

	evt = zbus_chan_const_msg(chan);
	if (evt == NULL) {
		return;
	}

	if (meshbus_desktop_input_should_consume_act(evt)) {
		return;
	}

	if (!meshbus_act_evt_to_zui(evt, &code, &action, &value)) {
		return;
	}

	LOG_DBG("Meshbus act -> ZUI: action=%u type=%u code=0x%04x => key=%u evt=%u",
		evt->action, evt->type, evt->code, code, action);

	if (code == ZUI_INPUT_CODE_KEYPAD) {
		(void)desktop_input_submit(code, action, value);
		return;
	}

	/* For scroll actions there is no raw PRESS/RELEASE; synthesize fully. */
	if (evt->action == INPUT_ACT_SCROLL_CW || evt->action == INPUT_ACT_SCROLL_CCW) {
		if (desktop_input_submit(code, ZUI_INPUT_ACTION_PRESS, value) != 0) {
			return;
		}
		(void)desktop_input_submit(code, action, value);
		(void)desktop_input_submit(code, ZUI_INPUT_ACTION_RELEASE, value);
		return;
	}

	if (code >= ZUI_INPUT_CODE_COUNT) {
		return;
	}

	was_pressed = atomic_test_bit(&pressed_mask, code);

	/* Ensure SHORT/LONG isn't dropped if raw PRESS was missed (e.g. injection). */
	if (!was_pressed) {
		if (desktop_input_submit(code, ZUI_INPUT_ACTION_PRESS, value) != 0) {
			return;
		}
		atomic_set_bit(&pressed_mask, code);
	}

	(void)desktop_input_submit(code, action, value);

	/* If we synthesized PRESS, also synthesize RELEASE (raw won't follow). */
	if (!was_pressed) {
		if (desktop_input_submit(code, ZUI_INPUT_ACTION_RELEASE, value) == 0) {
			atomic_clear_bit(&pressed_mask, code);
		}
	}
}

static void meshbus_desktop_input_raw_cb(const struct zbus_channel *chan)
{
	const struct meshbus_input_event *evt;
	enum zui_input_code code;
	bool is_pressed;

	evt = zbus_chan_const_msg(chan);
	if (evt == NULL) {
		return;
	}

	if (meshbus_desktop_input_should_consume_raw(evt)) {
		return;
	}

	/* Only key events have meaningful PRESS/RELEASE semantics for ZUI. */
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	if (!code_to_zui_code(evt->code, &code)) {
		return;
	}

	if (code >= ZUI_INPUT_CODE_COUNT) {
		return;
	}
	is_pressed = atomic_test_bit(&pressed_mask, code);

	if (evt->value != 0) {
		/* PRESS (and optionally REPEAT when value==2) */
		if (!is_pressed) {
			if (desktop_input_submit(code, ZUI_INPUT_ACTION_PRESS, evt->value) == 0) {
				atomic_set_bit(&pressed_mask, code);
			}
		} else if (evt->value == 2) {
			(void)desktop_input_submit(code, ZUI_INPUT_ACTION_CLICK, evt->value);
		}
		return;
	}

	/* RELEASE */
	if (!is_pressed) {
		return;
	}

	if (desktop_input_submit(code, ZUI_INPUT_ACTION_RELEASE, evt->value) == 0) {
		atomic_clear_bit(&pressed_mask, code);
	}
}

ZBUS_LISTENER_DEFINE(meshbus_desktop_input_act_listener, meshbus_desktop_input_act_cb);
ZBUS_CHAN_ADD_OBS(meshbus_input_action_chan, meshbus_desktop_input_act_listener, 2);

ZBUS_LISTENER_DEFINE(meshbus_desktop_input_raw_listener, meshbus_desktop_input_raw_cb);
ZBUS_CHAN_ADD_OBS(meshbus_input_key_chan, meshbus_desktop_input_raw_listener, 2);

uint32_t desktop_input_pressed_mask_get(void)
{
	return (uint32_t)atomic_get(&pressed_mask);
}

void desktop_input_drop_counts_get(struct desktop_input_drop_counts *counts)
{
	if (counts == NULL) {
		return;
	}

	counts->press = (uint32_t)atomic_get(&press_drop_count);
	counts->action = (uint32_t)atomic_get(&action_drop_count);
	counts->release = (uint32_t)atomic_get(&release_drop_count);
}

#else

uint32_t desktop_input_pressed_mask_get(void)
{
	return 0U;
}

void desktop_input_drop_counts_get(struct desktop_input_drop_counts *counts)
{
	if (counts != NULL) {
		*counts = (struct desktop_input_drop_counts){0};
	}
}

#endif /* CONFIG_ZBUS && CONFIG_MESHBUS_INPUT && CONFIG_MESHBUS_DESKTOP */
