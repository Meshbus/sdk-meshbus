/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/display.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(meshbus_display_sample, LOG_LEVEL_INF);

static void display_state_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (chan != &meshbus_display_state_chan || message == NULL) {
		return;
	}

	const struct meshbus_display_state_event *event = message;

	LOG_INF("display/state: active=%u", event->active ? 1U : 0U);
}

ZBUS_ASYNC_LISTENER_DEFINE(display_state_listener, display_state_listener_cb);

int main(void)
{
	meshbus_display_config cfg;
	int rc;

	LOG_INF("Meshbus display sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

	rc = meshbus_display_config_get(&cfg);
	if (rc == 0) {
		LOG_INF("display/config: brightness=%u sleep_timeout=%u invert=%u active=%u",
			(unsigned int)cfg.brightness, (unsigned int)cfg.sleep_timeout,
			cfg.invert ? 1U : 0U, meshbus_display_is_active() ? 1U : 0U);
	} else {
		LOG_ERR("Failed to read display config: %d", rc);
	}

	rc = zbus_chan_add_obs(&meshbus_display_state_chan, &display_state_listener,
			       K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("Failed to subscribe meshbus_display_state_chan: %d", rc);
	} else {
		LOG_INF("Subscribed to meshbus_display_state_chan");
	}

	return 0;
}
