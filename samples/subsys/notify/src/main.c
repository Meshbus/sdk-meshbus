/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <notify/notify.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(mbs_notify_sample, LOG_LEVEL_INF);

static void notify_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (chan != &mbs_notify_chan || message == NULL) {
		return;
	}

	const mbs_notify_event *event = message;

	LOG_INF("notify: type=%d len=%u", event->type, event->payload_len);
}

ZBUS_ASYNC_LISTENER_DEFINE(notify_listener, notify_listener_cb);

int main(void)
{
	LOG_INF("Meshbus notify sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

	int rc = zbus_chan_add_obs(&mbs_notify_chan, &notify_listener, K_MSEC(100));
	if (rc != 0) {
		LOG_ERR("Failed to subscribe mbs_notify_chan: %d", rc);
	} else {
		LOG_INF("Subscribed to mbs_notify_chan");
	}

	return 0;
}
