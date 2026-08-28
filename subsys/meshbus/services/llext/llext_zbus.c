/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "llext_bridge_common.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/init.h>
#if defined(CONFIG_MESHBUS_BLUETOOTH)
#include <zephyr/meshbus/bluetooth.h>
#endif
#if defined(CONFIG_MESHBUS_DISPLAY)
#include <zephyr/meshbus/display.h>
#endif
#if defined(CONFIG_MESHBUS_GNSS)
#include <zephyr/meshbus/gnss.h>
#endif
#if defined(CONFIG_MESHBUS_INPUT)
#include <zephyr/meshbus/input.h>
#endif
#if defined(CONFIG_MESHBUS_INDICATOR)
#include <zephyr/meshbus/indicator.h>
#endif
#include <zephyr/meshbus/llext.h>
#if defined(CONFIG_MESHBUS_MESSAGE)
#include <zephyr/meshbus/message.h>
#endif
#if defined(CONFIG_MESHBUS_MESHCORE)
#include <zephyr/meshbus/meshcore.h>
#endif
#if defined(CONFIG_MESHBUS_CONTACT)
#include <zephyr/meshbus/contact.h>
#endif
#if defined(CONFIG_MESHBUS_NOTIFY)
#include <zephyr/meshbus/notify.h>
#endif
#if defined(CONFIG_MESHBUS_POWER)
#include <zephyr/meshbus/power.h>
#endif
#if defined(CONFIG_MESHBUS_RADIO)
#include <zephyr/meshbus/radio.h>
#endif
#if defined(CONFIG_MESHBUS_TELEMETRY)
#include <zephyr/meshbus/telemetry.h>
#endif
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#define MESHBUS_LLEXT_ZBUS_MAX_SUBSCRIBERS 8U

struct meshbus_llext_zbus_desc {
	const struct zbus_channel *chan;
	size_t msg_size;
	bool publish_allowed;
};

static const struct meshbus_llext_zbus_desc zbus_descs[MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT] = {
#if defined(CONFIG_MESHBUS_INPUT)
	[MESHBUS_LLEXT_ZBUS_INPUT_KEY_CHAN] = {
		.chan = &meshbus_input_key_chan,
		.msg_size = sizeof(struct meshbus_input_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_INPUT_ACTION_CHAN] = {
		.chan = &meshbus_input_action_chan,
		.msg_size = sizeof(struct meshbus_input_act_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_NOTIFY)
	[MESHBUS_LLEXT_ZBUS_NOTIFY_CHAN] = {
		.chan = &meshbus_notify_chan,
		.msg_size = sizeof(meshbus_notify_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_BLUETOOTH)
	[MESHBUS_LLEXT_ZBUS_BLUETOOTH_PAIRING_CHAN] = {
		.chan = &meshbus_bluetooth_pairing_chan,
		.msg_size = sizeof(struct meshbus_bluetooth_pairing_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_BLUETOOTH_STATE_CHAN] = {
		.chan = &meshbus_bluetooth_state_chan,
		.msg_size = sizeof(struct meshbus_bluetooth_state_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_DISPLAY)
	[MESHBUS_LLEXT_ZBUS_DISPLAY_STATE_CHAN] = {
		.chan = &meshbus_display_state_chan,
		.msg_size = sizeof(struct meshbus_display_state_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_GNSS)
	[MESHBUS_LLEXT_ZBUS_GNSS_DATA_CHAN] = {
		.chan = &meshbus_gnss_data_chan,
		.msg_size = sizeof(struct meshbus_gnss_data_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_LLEXT)
	[MESHBUS_LLEXT_ZBUS_LLEXT_EVENT_CHAN] = {
		.chan = &meshbus_llext_state_chan,
		.msg_size = sizeof(struct meshbus_llext_state_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_MESSAGE)
	[MESHBUS_LLEXT_ZBUS_MESSAGE_SEND_TO_NODE_REQUEST_CHAN] = {
		.chan = &meshbus_message_send_to_node_request_chan,
		.msg_size = sizeof(struct meshbus_message_send_to_node_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_MESSAGE_SEND_TO_CHANNEL_REQUEST_CHAN] = {
		.chan = &meshbus_message_send_to_channel_request_chan,
		.msg_size = sizeof(struct meshbus_message_send_to_channel_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_MESSAGE_RESPONSE_CHAN] = {
		.chan = &meshbus_message_response_chan,
		.msg_size = sizeof(struct meshbus_message_response_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_MESSAGE_ACK_RESPONSE_CHAN] = {
		.chan = &meshbus_message_ack_response_chan,
		.msg_size = sizeof(struct meshbus_message_ack_response_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_MESHCORE)
	[MESHBUS_LLEXT_ZBUS_MESHCORE_CONFIG_RESET_CHAN] = {
		.chan = &meshbus_meshcore_config_reset_chan,
		.msg_size = sizeof(meshbus_meshcore_config_reset_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_MESHCORE_ADVERT_REQUEST_CHAN] = {
		.chan = &meshbus_meshcore_advert_request_chan,
		.msg_size = sizeof(meshbus_meshcore_advert_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_MESHCORE_NODE_DISCOVER_REQUEST_CHAN] = {
		.chan = &meshbus_meshcore_node_discover_request_chan,
		.msg_size = sizeof(meshbus_meshcore_node_discover_request_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_CONTACT)
	[MESHBUS_LLEXT_ZBUS_CONTACT_SHARE_REQUEST_CHAN] = {
		.chan = &meshbus_contact_share_request_chan,
		.msg_size = sizeof(meshbus_contact_share_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_DISCOVER_PATH_REQUEST_CHAN] = {
		.chan = &meshbus_contact_discover_path_request_chan,
		.msg_size = sizeof(meshbus_contact_discover_path_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_TRACE_PATH_REQUEST_CHAN] = {
		.chan = &meshbus_contact_trace_path_request_chan,
		.msg_size = sizeof(meshbus_contact_trace_path_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_TELEMETRY_REQUEST_CHAN] = {
		.chan = &meshbus_contact_telemetry_request_chan,
		.msg_size = sizeof(meshbus_contact_telemetry_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_ADVERT_RESPONSE_CHAN] = {
		.chan = &meshbus_contact_advert_response_chan,
		.msg_size = sizeof(meshbus_contact_response_advert_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_PATH_RESPONSE_CHAN] = {
		.chan = &meshbus_contact_path_response_chan,
		.msg_size = sizeof(meshbus_contact_response_path_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_TRACE_PATH_RESPONSE_CHAN] = {
		.chan = &meshbus_contact_trace_path_response_chan,
		.msg_size = sizeof(meshbus_contact_response_trace_path_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_TELEMETRY_RESPONSE_CHAN] = {
		.chan = &meshbus_contact_telemetry_response_chan,
		.msg_size = sizeof(meshbus_contact_response_telemetry_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_CONTACT_DISCOVER_RESPONSE_CHAN] = {
		.chan = &meshbus_contact_discover_response_chan,
		.msg_size = sizeof(meshbus_contact_response_discover_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_POWER)
	[MESHBUS_LLEXT_ZBUS_POWER_FUEL_GAUGE_DATA_CHAN] = {
		.chan = &meshbus_power_fuel_gauge_data_chan,
		.msg_size = sizeof(struct meshbus_power_fuel_gauge_data_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MESHBUS_RADIO)
	[MESHBUS_LLEXT_ZBUS_RADIO_RECEIVE_CHAN] = {
		.chan = &meshbus_radio_receive_chan,
		.msg_size = sizeof(struct meshbus_radio_receive_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_RADIO_PUBLISH_CHAN] = {
		.chan = &meshbus_radio_publish_chan,
		.msg_size = sizeof(struct meshbus_radio_publish_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_RADIO_TX_DONE_CHAN] = {
		.chan = &meshbus_radio_tx_done_chan,
		.msg_size = sizeof(struct meshbus_radio_tx_done_event),
		.publish_allowed = false,
	},
	[MESHBUS_LLEXT_ZBUS_RADIO_STATE_CHAN] = {
		.chan = &meshbus_radio_state_chan,
		.msg_size = sizeof(struct meshbus_radio_state_event),
		.publish_allowed = false,
	},
	[MESHBUS_LLEXT_ZBUS_RADIO_CW_CHAN] = {
		.chan = &meshbus_radio_cw_chan,
		.msg_size = sizeof(struct meshbus_radio_cw_request_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_RADIO_CW_DONE_CHAN] = {
		.chan = &meshbus_radio_cw_done_chan,
		.msg_size = sizeof(struct meshbus_radio_cw_done_event),
		.publish_allowed = false,
	},
#endif
#if defined(CONFIG_MESHBUS_TELEMETRY)
	[MESHBUS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN] = {
		.chan = &meshbus_telemetry_data_chan,
		.msg_size = sizeof(struct meshbus_telemetry_data_event),
		.publish_allowed = false,
	},
#endif
#if defined(CONFIG_MESHBUS_INDICATOR)
	[MESHBUS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN] = {
		.chan = &meshbus_indicator_light_play_chan,
		.msg_size = sizeof(struct meshbus_indicator_light_play_event),
		.publish_allowed = true,
	},
	[MESHBUS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN] = {
		.chan = &meshbus_indicator_buzzer_play_chan,
		.msg_size = sizeof(struct meshbus_indicator_buzzer_play_event),
		.publish_allowed = true,
	},
#endif
};

static struct mb_llext_bridge_subscriber
	zbus_subscribers[MESHBUS_LLEXT_ZBUS_MAX_SUBSCRIBERS];
static struct mb_llext_bridge zbus_bridge;

BUILD_ASSERT(MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT <= MB_LLEXT_BRIDGE_MAX_CHANNELS,
	     "meshbus LLEXT ZBus bridge cannot address all public channels");

static bool meshbus_llext_zbus_channel_is_valid(enum meshbus_llext_zbus_channel channel)
{
	return channel >= 0 && channel < MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT;
}

static uint64_t meshbus_llext_zbus_available_mask(void)
{
	uint64_t mask = 0ULL;

	for (int i = 0; i < MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan != NULL) {
			mask |= MESHBUS_LLEXT_ZBUS_CH_BIT(i);
		}
	}

	return mask;
}

static int meshbus_llext_zbus_channel_from_chan(const struct zbus_channel *chan)
{
	for (int i = 0; i < MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan == chan) {
			return i;
		}
	}

	return -ENOENT;
}

static void meshbus_llext_zbus_listener_cb(const struct zbus_channel *chan)
{
	int channel;

	channel = meshbus_llext_zbus_channel_from_chan(chan);
	if (channel < 0) {
		return;
	}

	(void)mb_llext_bridge_schedule(&zbus_bridge, (size_t)channel);
}

ZBUS_LISTENER_DEFINE(meshbus_llext_zbus_listener, meshbus_llext_zbus_listener_cb);

static int meshbus_llext_zbus_subscribe_channel(const struct zbus_channel *chan,
						const struct zbus_observer *obs)
{
	int rc;

	rc = zbus_chan_add_obs(chan, obs, K_NO_WAIT);
	if (rc == 0 || rc == -EALREADY || rc == -EEXIST) {
		return 0;
	}

	return rc;
}

static int meshbus_llext_zbus_init(void)
{
	int first_err = 0;

	mb_llext_bridge_init(&zbus_bridge,
			     zbus_subscribers,
			     ARRAY_SIZE(zbus_subscribers),
			     MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT);

	for (int i = 0; i < MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan == NULL) {
			continue;
		}

		int rc = meshbus_llext_zbus_subscribe_channel(zbus_descs[i].chan,
							      &meshbus_llext_zbus_listener);

		if ((rc != 0) && (first_err == 0)) {
			first_err = rc;
		}
	}

	return first_err;
}

SYS_INIT(meshbus_llext_zbus_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int meshbus_llext_zbus_subscribe(struct k_event *evt, uint64_t channel_mask)
{
	uint64_t allowed_mask = meshbus_llext_zbus_available_mask();

	if (allowed_mask == 0ULL) {
		return -ENODEV;
	}

	return mb_llext_bridge_subscribe(&zbus_bridge, evt, channel_mask, allowed_mask);
}

int meshbus_llext_zbus_unsubscribe(struct k_event *evt)
{
	return mb_llext_bridge_unsubscribe(&zbus_bridge, evt);
}

int meshbus_llext_zbus_take_pending(struct k_event *evt, uint64_t *pending_mask)
{
	size_t channel_id;
	int rc;

	if (evt == NULL || pending_mask == NULL) {
		return -EINVAL;
	}

	*pending_mask = 0ULL;
	for (;;) {
		rc = mb_llext_bridge_take_pending(&zbus_bridge, evt, &channel_id);
		if (rc == -ENOMSG) {
			break;
		}
		if (rc != 0) {
			return rc;
		}

		if (channel_id < MESHBUS_LLEXT_ZBUS_CHANNEL_COUNT &&
		    zbus_descs[channel_id].chan != NULL) {
			*pending_mask |= MESHBUS_LLEXT_ZBUS_CH_BIT(channel_id);
		}
	}

	return (*pending_mask != 0ULL) ? 0 : -ENOMSG;
}

int meshbus_llext_zbus_read(enum meshbus_llext_zbus_channel channel, void *msg, size_t msg_size)
{
	const struct meshbus_llext_zbus_desc *desc;

	if (!meshbus_llext_zbus_channel_is_valid(channel) || msg == NULL) {
		return -EINVAL;
	}

	desc = &zbus_descs[channel];
	if (desc->chan == NULL) {
		return -ENODEV;
	}
	if (msg_size != desc->msg_size) {
		return -EINVAL;
	}

	return zbus_chan_read(desc->chan, msg, K_NO_WAIT);
}

int meshbus_llext_zbus_publish(enum meshbus_llext_zbus_channel channel, const void *msg,
			       size_t msg_size)
{
	const struct meshbus_llext_zbus_desc *desc;

	if (!meshbus_llext_zbus_channel_is_valid(channel) || msg == NULL) {
		return -EINVAL;
	}

	desc = &zbus_descs[channel];
	if (desc->chan == NULL) {
		return -ENODEV;
	}
	if (!desc->publish_allowed) {
		return -ENOTSUP;
	}
	if (msg_size != desc->msg_size) {
		return -EINVAL;
	}

	return zbus_chan_pub(desc->chan, msg, K_NO_WAIT);
}
