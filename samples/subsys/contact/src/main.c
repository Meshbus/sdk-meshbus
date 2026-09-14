/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <contact/contact.h>
#include <meshcore/meshcore.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(mbs_contact_sample, LOG_LEVEL_INF);

static void contact_listener_cb(const struct zbus_channel *chan, const void *message)
{
	ARG_UNUSED(message);

	if (chan == &mbs_meshcore_advert_request_chan) {
		const mbs_meshcore_advert_request_event *event = message;

		LOG_INF("contact/advert request: flood=%u", event->flood ? 1U : 0U);
	} else if (chan == &mbs_contact_share_request_chan) {
		LOG_INF("contact/share request");
	} else if (chan == &mbs_contact_discover_path_request_chan) {
		LOG_INF("contact/discover-path request");
	} else if (chan == &mbs_contact_trace_path_request_chan) {
		LOG_INF("contact/trace-path request");
	} else if (chan == &mbs_contact_telemetry_request_chan) {
		LOG_INF("contact/telemetry request");
	} else if (chan == &mbs_contact_advert_response_chan) {
		const mbs_contact_response_advert_event *event = message;

		LOG_INF("contact/advert response: name=\"%s\" role=%d", event->name, event->role);
	} else if (chan == &mbs_contact_path_response_chan) {
		const mbs_contact_response_path_event *event = message;

		LOG_INF("contact/path response: discover=%u timestamp=%u",
			event->is_discover ? 1U : 0U, event->timestamp);
	} else if (chan == &mbs_contact_trace_path_response_chan) {
		const mbs_contact_response_trace_path_event *event = message;

		LOG_INF("contact/trace-path response: timestamp=%u state=%u", event->timestamp,
			(unsigned int)event->state);
	} else if (chan == &mbs_contact_telemetry_response_chan) {
		const mbs_contact_response_telemetry_event *event = message;

		LOG_INF("contact/telemetry response: timestamp=%u payload_len=%u", event->timestamp,
			(unsigned int)event->payload_len);
	}
}

ZBUS_ASYNC_LISTENER_DEFINE(contact_listener, contact_listener_cb);

static void subscribe_contact_channel(const struct zbus_channel *chan, const char *name)
{
	int rc = zbus_chan_add_obs(chan, &contact_listener, K_MSEC(100));

	if (rc != 0) {
		LOG_ERR("Failed to subscribe %s: %d", name, rc);
	} else {
		LOG_INF("Subscribed to %s", name);
	}
}

int main(void)
{
	LOG_INF("Meshbus contact sample started");
	LOG_INF("Build timestamp: " __DATE__ " " __TIME__);
	LOG_INF("Contact store: count=%u capacity=%u",
		(unsigned int)mbs_contact_store_count(),
		(unsigned int)mbs_contact_store_size());

	subscribe_contact_channel(&mbs_meshcore_advert_request_chan, "mbs_meshcore_advert_request_chan");
	subscribe_contact_channel(&mbs_contact_share_request_chan,
			       "mbs_contact_share_request_chan");
	subscribe_contact_channel(&mbs_contact_discover_path_request_chan,
			       "mbs_contact_discover_path_request_chan");
	subscribe_contact_channel(&mbs_contact_trace_path_request_chan,
			       "mbs_contact_trace_path_request_chan");
	subscribe_contact_channel(&mbs_contact_telemetry_request_chan,
			       "mbs_contact_telemetry_request_chan");
	subscribe_contact_channel(&mbs_contact_advert_response_chan,
			       "mbs_contact_advert_response_chan");
	subscribe_contact_channel(&mbs_contact_path_response_chan,
			       "mbs_contact_path_response_chan");
	subscribe_contact_channel(&mbs_contact_trace_path_response_chan,
			       "mbs_contact_trace_path_response_chan");
	subscribe_contact_channel(&mbs_contact_telemetry_response_chan,
			       "mbs_contact_telemetry_response_chan");

	return 0;
}
