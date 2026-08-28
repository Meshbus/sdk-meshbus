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

LOG_MODULE_REGISTER(meshbus_input_button, CONFIG_MESHBUS_INPUT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#if DT_HAS_CHOSEN(meshbus_input_buttons)
static const struct device *const buttons_dev = DEVICE_DT_GET(DT_CHOSEN(meshbus_input_buttons));
#else
static const struct device *const buttons_dev = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define LONG_PRESS_MS      CONFIG_MESHBUS_INPUT_LONG_PRESS_MS
#define DEBOUNCE_MS        CONFIG_MESHBUS_INPUT_DEBOUNCE_MS
#define REPEAT_DELAY_MS    CONFIG_MESHBUS_INPUT_REPEAT_DELAY_MS
#define REPEAT_INTERVAL_MS CONFIG_MESHBUS_INPUT_REPEAT_INTERVAL_MS

struct input_button_state {
	struct k_work_delayable action_work; /**< Delayed work for long/repeat detection */
	uint32_t press_time_ms;            /**< Timestamp when button was pressed */
	uint32_t last_change_ms;           /**< Timestamp of last accepted state change */
	uint16_t code;                     /**< Button input code */
	bool pressed;                      /**< Current pressed state */
	bool long_sent;                    /**< Long press event already sent */
};
static struct input_button_state input_buttons[CONFIG_MESHBUS_INPUT_BUTTON_MAX];
static uint8_t input_button_count;

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static void input_button_action_work_handler(struct k_work *work);

static struct input_button_state *input_button_get(uint16_t code, bool create)
{
	for (uint8_t i = 0; i < input_button_count; i++) {
		if (input_buttons[i].code == code) {
			return &input_buttons[i];
		}
	}

	if (!create) {
		return NULL;
	}

	if (input_button_count >= CONFIG_MESHBUS_INPUT_BUTTON_MAX) {
		LOG_WRN("Too many buttons, ignoring code 0x%04x", code);
		return NULL;
	}

	struct input_button_state *btn = &input_buttons[input_button_count++];

	memset(btn, 0, sizeof(*btn));
	btn->code = code;
	k_work_init_delayable(&btn->action_work, input_button_action_work_handler);

	return btn;
}

static void input_button_action_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct input_button_state *btn = CONTAINER_OF(dwork, struct input_button_state, action_work);

	if (!btn->pressed) {
		return;
	}

	if (meshbus_input_key_is_nav_code(btn->code)) {
		LOG_DBG("Button 0x%04x repeat short press", btn->code);
		meshbus_input_action_event_publish(INPUT_EV_KEY, btn->code, INPUT_ACT_KEY_SHORT);
		k_work_reschedule(&btn->action_work, K_MSEC(REPEAT_INTERVAL_MS));
		return;
	}

	if (btn->long_sent || !meshbus_input_key_uses_long_press(btn->code)) {
		return;
	}
	btn->long_sent = true;

	LOG_DBG("Button 0x%04x long press", btn->code);
	meshbus_input_action_event_publish(INPUT_EV_KEY, btn->code, INPUT_ACT_KEY_LONG);
}

static void input_button_event_handler(struct input_event *evt)
{
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	const uint32_t now_ms = k_uptime_get_32();
	const uint16_t code = evt->code;
	const bool pressed = (evt->value != 0);

	struct input_button_state *btn = input_button_get(code, pressed);
	if (btn == NULL) {
		return;
	}

	/* Ignore repeated state (e.g. key auto-repeat). */
	if (pressed == btn->pressed) {
		return;
	}

	/* Debounce state changes (bounce filter). */
	if ((DEBOUNCE_MS > 0) && (btn->last_change_ms != 0U) &&
	    ((now_ms - btn->last_change_ms) < DEBOUNCE_MS)) {
		LOG_DBG("Button 0x%04x debounce drop: value=%d", code, evt->value);
		return;
	}
	btn->last_change_ms = now_ms;

	if (pressed) {
		/* Publish raw event for immediate UI feedback */
		LOG_DBG("Publishing raw input event: type=%u code=0x%04x value=%d", evt->type,
			evt->code, evt->value);
		meshbus_input_key_event_publish(evt->type, evt->code, evt->value);

		/* Button pressed */
		btn->pressed = true;
		btn->press_time_ms = now_ms;
		btn->long_sent = false;

		LOG_DBG("Button 0x%04x pressed", code);

		if (meshbus_input_key_is_nav_code(code)) {
			meshbus_input_action_event_publish(INPUT_EV_KEY, btn->code,
							INPUT_ACT_KEY_SHORT);
			k_work_reschedule(&btn->action_work, K_MSEC(REPEAT_DELAY_MS));
		} else if (meshbus_input_key_uses_long_press(code)) {
			k_work_reschedule(&btn->action_work, K_MSEC(LONG_PRESS_MS));
		}
	} else {
		/* Button released */
		bool publish_act = true;
		bool publish_raw = true;

		btn->pressed = false;
		if (k_is_in_isr()) {
			(void)k_work_cancel_delayable(&btn->action_work);
		} else {
			struct k_work_sync sync;
			(void)k_work_cancel_delayable_sync(&btn->action_work, &sync);
		}

		LOG_DBG("Button 0x%04x released", code);

		if (meshbus_input_key_is_nav_code(code)) {
			publish_act = false;
			goto out_raw;
		}

		if (btn->long_sent) {
			publish_act = false;
			goto out_raw;
		}

		/*
		 * Fallback: if delayed work didn't run in time (e.g. heavy load),
		 * classify by duration at release to avoid mis-reporting a long press
		 * as SHORT.
		 */
		if (meshbus_input_key_uses_long_press(code) &&
		    ((now_ms - btn->press_time_ms) >= LONG_PRESS_MS)) {
			btn->long_sent = true;
			LOG_DBG("Button 0x%04x long press (late)", code);
			meshbus_input_action_event_publish(INPUT_EV_KEY, btn->code, INPUT_ACT_KEY_LONG);
			publish_act = false;
			goto out_raw;
		}

		if (publish_act) {
			LOG_DBG("Button 0x%04x short press", code);
			meshbus_input_action_event_publish(INPUT_EV_KEY, btn->code,
							INPUT_ACT_KEY_SHORT);
		}

out_raw:
		if (publish_raw) {
			/*
			 * Order is important for GUIs which require complementary
			 * PRESS->(SHORT/LONG)->RELEASE sequences: publish the processed
			 * action (SHORT) before the raw RELEASE.
			 */
			LOG_DBG("Publishing raw input event: type=%u code=0x%04x value=%d",
				evt->type, evt->code, evt->value);
			meshbus_input_key_event_publish(evt->type, evt->code, evt->value);
		}
	}
}

static void input_button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	input_button_event_handler(evt);
}

#if DT_HAS_CHOSEN(meshbus_input_buttons)
INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_CHOSEN(meshbus_input_buttons)), input_button_cb, NULL,
			    meshbus_buttons);
#endif

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int meshbus_input_button_init(void)
{
	if (buttons_dev == NULL) {
		LOG_WRN("No button device configured");
	} else if (!device_is_ready(buttons_dev)) {
		LOG_ERR("Button device not ready");
	} else {
		LOG_INF("Button input initialized");
	}

	return 0;
}

SYS_INIT(meshbus_input_button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
