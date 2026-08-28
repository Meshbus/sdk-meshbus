/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "input.h"
#include "zephyr/meshbus/input.h"

LOG_MODULE_REGISTER(meshbus_input_keypad, CONFIG_MESHBUS_INPUT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_input_keypad)
static const struct device *const keypad_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_input_keypad));
#else
static const struct device *const keypad_dev = NULL;
#endif

#if defined(CONFIG_MESHBUS_INPUT_KEYPAD) && DT_HAS_CHOSEN(meshbus_input_keypad)

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define LONG_PRESS_MS      CONFIG_MESHBUS_INPUT_LONG_PRESS_MS
#define DEBOUNCE_MS        CONFIG_MESHBUS_INPUT_DEBOUNCE_MS
#define REPEAT_DELAY_MS    CONFIG_MESHBUS_INPUT_REPEAT_DELAY_MS
#define REPEAT_INTERVAL_MS CONFIG_MESHBUS_INPUT_REPEAT_INTERVAL_MS

struct input_keypad_key {
	struct k_work_delayable action_work; /**< Delayed work for long/repeat detection */
	uint32_t press_time_ms;            /**< Timestamp when key was pressed */
	uint32_t last_change_ms;           /**< Timestamp of last accepted state change */
	uint16_t code;                     /**< Key code */
	bool active;                       /**< Slot is in use */
	bool pressed;                      /**< Current pressed state */
	bool long_sent;                    /**< Long press event already sent */
};
static struct input_keypad_key keypad_keys[CONFIG_MESHBUS_INPUT_KEYPAD_MAX_KEYS];

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static void input_keypad_action_work_handler(struct k_work *work);

static struct input_keypad_key *keypad_get_key(uint16_t code, bool create)
{
	struct input_keypad_key *free_slot = NULL;

	for (int i = 0; i < CONFIG_MESHBUS_INPUT_KEYPAD_MAX_KEYS; i++) {
		if (keypad_keys[i].active && (keypad_keys[i].code == code)) {
			return &keypad_keys[i];
		}
		if (!keypad_keys[i].active && (free_slot == NULL)) {
			free_slot = &keypad_keys[i];
		}
	}

	if (!create) {
		return NULL;
	}

	if (free_slot == NULL) {
		LOG_WRN("Keypad key table full, code=0x%04x", code);
		return NULL;
	}

	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->active = true;
	free_slot->code = code;
	k_work_init_delayable(&free_slot->action_work, input_keypad_action_work_handler);

	return free_slot;
}

static void input_keypad_slot_release(struct input_keypad_key *key)
{
	if (key == NULL) {
		return;
	}

	key->active = false;
	key->pressed = false;
	key->long_sent = false;
	key->press_time_ms = 0U;
	key->last_change_ms = 0U;
}

static void input_keypad_action_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct input_keypad_key *key = CONTAINER_OF(dwork, struct input_keypad_key, action_work);

	if (!key->pressed) {
		return;
	}

	if (meshbus_input_key_is_nav_code(key->code)) {
		LOG_DBG("Keypad 0x%04x repeat short press", key->code);
		meshbus_input_action_event_publish(INPUT_EV_KEY, key->code, INPUT_ACT_KEY_SHORT);
		k_work_reschedule(&key->action_work, K_MSEC(REPEAT_INTERVAL_MS));
		return;
	}

	if (key->long_sent || !meshbus_input_key_uses_long_press(key->code)) {
		return;
	}
	key->long_sent = true;

	LOG_DBG("Keypad 0x%04x long press", key->code);
	meshbus_input_action_event_publish(INPUT_EV_KEY, key->code, INPUT_ACT_KEY_LONG);
}

static void input_keypad_event_handler(struct input_event *evt)
{
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	const uint32_t now_ms = k_uptime_get_32();
	const uint16_t code = evt->code;
	const bool pressed = (evt->value != 0);

	/*
	 * Only allocate a slot on press. Drivers may emit initial RELEASE events
	 * for many codes; allocating on release can fill the key table.
	 */
	struct input_keypad_key *key = keypad_get_key(code, pressed);
	if (key == NULL) {
		return;
	}

	/* Ignore repeated state (e.g. key auto-repeat). */
	if (pressed == key->pressed) {
		return;
	}

	/* Debounce state changes (bounce filter). */
	if ((DEBOUNCE_MS > 0) && (key->last_change_ms != 0U) &&
	    ((now_ms - key->last_change_ms) < DEBOUNCE_MS)) {
		LOG_DBG("Keypad 0x%04x debounce drop: value=%d", code, evt->value);
		return;
	}
	key->last_change_ms = now_ms;

	if (pressed) {
		/* Publish raw event for immediate UI feedback */
		LOG_DBG("Publishing raw input event: type=%u code=0x%04x value=%d", evt->type,
			evt->code, evt->value);
		meshbus_input_key_event_publish(evt->type, evt->code, evt->value);

		/* Key pressed */
		key->pressed = true;
		key->press_time_ms = now_ms;
		key->long_sent = false;

		LOG_DBG("Keypad 0x%04x pressed", code);

		if (meshbus_input_key_is_nav_code(code)) {
			meshbus_input_action_event_publish(INPUT_EV_KEY, code, INPUT_ACT_KEY_SHORT);
			k_work_reschedule(&key->action_work, K_MSEC(REPEAT_DELAY_MS));
		} else if (meshbus_input_key_uses_long_press(code)) {
			k_work_reschedule(&key->action_work, K_MSEC(LONG_PRESS_MS));
		}
	} else {
		/* Key released */
		bool cancelled = true;
		bool emit_short = false;

		key->pressed = false;
		if (k_is_in_isr()) {
			cancelled = k_work_cancel_delayable(&key->action_work);
		} else {
			struct k_work_sync sync;
			cancelled = k_work_cancel_delayable_sync(&key->action_work, &sync);
		}

		LOG_DBG("Keypad 0x%04x released", code);

		if (meshbus_input_key_is_nav_code(code)) {
			goto out_release;
		}

		/*
		 * In ISR context cancellation is async; if cancellation reports
		 * "already running", suppress short-press emission to avoid
		 * duplicate SHORT+LONG events on threshold races.
		 */
		if (k_is_in_isr() && !cancelled && !key->long_sent &&
		    ((now_ms - key->press_time_ms) < LONG_PRESS_MS)) {
			goto out_release;
		}

		if (key->long_sent) {
			goto out_release;
		}

		/*
		 * Fallback: if delayed work didn't run in time (e.g. heavy load),
		 * classify by duration at release to avoid mis-reporting a long press
		 * as SHORT.
		 */
		if (meshbus_input_key_uses_long_press(code) &&
		    ((now_ms - key->press_time_ms) >= LONG_PRESS_MS)) {
			key->long_sent = true;
			LOG_DBG("Keypad 0x%04x long press (late)", code);
			meshbus_input_action_event_publish(INPUT_EV_KEY, code, INPUT_ACT_KEY_LONG);
			goto out_release;
		}

		emit_short = true;

		if (emit_short) {
			LOG_DBG("Keypad 0x%04x short press", code);
			/*
			 * Order is important for GUIs which require complementary
			 * PRESS->SHORT->RELEASE sequences: publish SHORT before the
			 * raw RELEASE.
			 */
			meshbus_input_action_event_publish(INPUT_EV_KEY, code, INPUT_ACT_KEY_SHORT);
		}

out_release:
		/* Always forward raw RELEASE for UI state updates. */
		LOG_DBG("Publishing raw input event: type=%u code=0x%04x value=%d", evt->type,
			evt->code, evt->value);
		meshbus_input_key_event_publish(evt->type, evt->code, evt->value);
		input_keypad_slot_release(key);
	}
}

static void input_keypad_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	input_keypad_event_handler(evt);
}

INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_CHOSEN(meshbus_input_keypad)), input_keypad_cb, NULL,
			    meshbus_keypad);

#endif

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int meshbus_input_keypad_init(void)
{
#if defined(CONFIG_MESHBUS_INPUT_KEYPAD) && DT_HAS_CHOSEN(meshbus_input_keypad)
	if (keypad_dev == NULL) {
		LOG_WRN("No keypad device configured");
	} else if (!device_is_ready(keypad_dev)) {
		LOG_ERR("Keypad device not ready");
	} else {
		LOG_INF("Keypad input initialized");
	}
#endif

	return 0;
}

SYS_INIT(meshbus_input_keypad_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
