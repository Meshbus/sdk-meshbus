/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(meshbus_message_sample, LOG_LEVEL_INF);

static void message_listener_cb(const struct zbus_channel *chan, const void *message)
{
	if (message == NULL) {
		return;
	}

	if (chan == &meshbus_message_send_to_node_request_chan) {
		const struct meshbus_message_send_to_node_request_event *event = message;

		LOG_INF("message/send-node: len=%u attempt=%u flood=%u",
			(unsigned int)event->payload_len, (unsigned int)event->attempt,
			event->flood ? 1U : 0U);
	} else if (chan == &meshbus_message_send_to_channel_request_chan) {
		const struct meshbus_message_send_to_channel_request_event *event = message;

		LOG_INF("message/send-channel: channel=%u len=%u",
			(unsigned int)event->channel_index, (unsigned int)event->payload_len);
	} else if (chan == &meshbus_message_response_chan) {
		const struct meshbus_message_response_event *event = message;

		LOG_INF("message/received: type=%d len=%u sender=\"%s\"", event->type,
			(unsigned int)event->payload_len, event->sender_name);
	} else if (chan == &meshbus_message_ack_response_chan) {
		const struct meshbus_message_ack_response_event *event = message;

		LOG_INF("message/ack: attempt=%u target=%02x%02x%02x%02x",
			(unsigned int)event->attempt, event->target[0], event->target[1],
			event->target[2], event->target[3]);
	}
}

ZBUS_ASYNC_LISTENER_DEFINE(message_listener, message_listener_cb);

static void subscribe_message_channel(const struct zbus_channel *chan, const char *name)
{
	int rc = zbus_chan_add_obs(chan, &message_listener, K_MSEC(100));

	if (rc != 0) {
		LOG_ERR("Failed to subscribe %s: %d", name, rc);
	} else {
		LOG_INF("Subscribed to %s", name);
	}
}

int main(void)
{
	LOG_INF("Meshbus message sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);

	subscribe_message_channel(&meshbus_message_send_to_node_request_chan,
				  "meshbus_message_send_to_node_request_chan");
	subscribe_message_channel(&meshbus_message_send_to_channel_request_chan,
				  "meshbus_message_send_to_channel_request_chan");
	subscribe_message_channel(&meshbus_message_response_chan, "meshbus_message_response_chan");
	subscribe_message_channel(&meshbus_message_ack_response_chan,
				  "meshbus_message_ack_response_chan");

	return 0;
}
