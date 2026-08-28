/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus subsystem test application
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <zephyr/meshbus/input.h>

LOG_MODULE_REGISTER(meshbus_test, LOG_LEVEL_INF);

static const char *act_str(uint8_t action)
{
	switch (action) {
	case INPUT_ACT_KEY_SHORT:
		return "short";
	case INPUT_ACT_KEY_LONG:
		return "long";
	case INPUT_ACT_SCROLL_CW:
		return "scroll_cw";
	case INPUT_ACT_SCROLL_CCW:
		return "scroll_ccw";
	default:
		return "unknown";
	}
}

static void input_raw_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (chan != &meshbus_input_key_chan || message == NULL) {
		return;
	}

	const struct meshbus_input_event *evt = message;

	LOG_INF("input/raw: type=%u code=0x%04x value=%d", (unsigned int)evt->type, evt->code,
		evt->value);
}

static void input_act_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (chan != &meshbus_input_action_chan || message == NULL) {
		return;
	}

	const struct meshbus_input_act_event *evt = message;

	LOG_INF("input/act: type=%u code=0x%04x action=%s(%u)", (unsigned int)evt->type, evt->code,
		act_str(evt->action), (unsigned int)evt->action);
}

ZBUS_ASYNC_LISTENER_DEFINE(input_raw_listener, input_raw_listener_cb);
ZBUS_ASYNC_LISTENER_DEFINE(input_act_listener, input_act_listener_cb);

int main(void)
{
	LOG_INF("Meshbus test application started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

#ifdef CONFIG_BOOTLOADER_MCUBOOT
	LOG_INF("MCUboot bootloader support enabled");
#endif

	int rc = zbus_chan_add_obs(&meshbus_input_key_chan, &input_raw_listener, K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("Failed to subscribe meshbus_input_key_chan: %d", rc);
	} else {
		LOG_INF("Subscribed to meshbus_input_key_chan");
	}

	rc = zbus_chan_add_obs(&meshbus_input_action_chan, &input_act_listener, K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("Failed to subscribe meshbus_input_action_chan: %d", rc);
	} else {
		LOG_INF("Subscribed to meshbus_input_action_chan");
	}

	return 0;
}
