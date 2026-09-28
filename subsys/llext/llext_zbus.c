/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "llext_bridge_common.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/init.h>
#if defined(CONFIG_MBS_BLUETOOTH)
#include <bluetooth/bluetooth.h>
#endif
#if defined(CONFIG_MBS_DISPLAY)
#include <display/display.h>
#endif
#if defined(CONFIG_MBS_GNSS)
#include <gnss/gnss.h>
#endif
#if defined(CONFIG_MBS_INPUT)
#include <input/input.h>
#endif
#if defined(CONFIG_MBS_INDICATOR)
#include <indicator/indicator.h>
#endif
#include <llext/zbus.h>
#if defined(CONFIG_MBS_MESSAGE)
#include <message/message.h>
#endif
#if defined(CONFIG_MBS_MESHCORE)
#include <meshcore/meshcore.h>
#endif
#if defined(CONFIG_MBS_CONTACT)
#include <contact/contact.h>
#endif
#if defined(CONFIG_MBS_NOTIFY)
#include <notify/notify.h>
#endif
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#if defined(CONFIG_MBS_RADIO)
#include <radio/radio.h>
#endif
#if defined(CONFIG_MBS_TELEMETRY)
#include <telemetry/telemetry.h>
#endif
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#define MBS_LLEXT_ZBUS_MAX_SUBSCRIBERS 8U

struct mbs_llext_zbus_desc {
	const struct zbus_channel *chan;
	size_t msg_size;
	bool publish_allowed;
};

static const struct mbs_llext_zbus_desc zbus_descs[MBS_LLEXT_ZBUS_CHANNEL_COUNT] = {
#if defined(CONFIG_MBS_INPUT)
	[MBS_LLEXT_ZBUS_INPUT_KEY_CHAN] = {
		.chan = &mbs_input_raw_event_chan,
		.msg_size = sizeof(struct mbs_input_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_INPUT_ACTION_CHAN] = {
		.chan = &mbs_input_action_chan,
		.msg_size = sizeof(struct mbs_input_act_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_NOTIFY)
	[MBS_LLEXT_ZBUS_NOTIFY_CHAN] = {
		.chan = &mbs_notify_chan,
		.msg_size = sizeof(mbs_notify_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_BLUETOOTH)
	[MBS_LLEXT_ZBUS_BLUETOOTH_PAIRING_CHAN] = {
		.chan = &mbs_bluetooth_pairing_chan,
		.msg_size = sizeof(struct mbs_bluetooth_pairing_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_BLUETOOTH_STATE_CHAN] = {
		.chan = &mbs_bluetooth_state_chan,
		.msg_size = sizeof(struct mbs_bluetooth_state_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_DISPLAY)
	[MBS_LLEXT_ZBUS_DISPLAY_STATE_CHAN] = {
		.chan = &mbs_display_state_chan,
		.msg_size = sizeof(struct mbs_display_state_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_GNSS)
	[MBS_LLEXT_ZBUS_GNSS_DATA_CHAN] = {
		.chan = &mbs_gnss_data_chan,
		.msg_size = sizeof(struct mbs_gnss_data_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_MESSAGE)
	[MBS_LLEXT_ZBUS_MESSAGE_SEND_TO_NODE_REQUEST_CHAN] = {
		.chan = &mbs_message_send_to_node_request_chan,
		.msg_size = sizeof(struct mbs_message_send_to_node_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_MESSAGE_SEND_TO_CHANNEL_REQUEST_CHAN] = {
		.chan = &mbs_message_send_to_channel_request_chan,
		.msg_size = sizeof(struct mbs_message_send_to_channel_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_MESSAGE_RESPONSE_CHAN] = {
		.chan = &mbs_message_response_chan,
		.msg_size = sizeof(struct mbs_message_response_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_MESSAGE_ACK_RESPONSE_CHAN] = {
		.chan = &mbs_message_ack_response_chan,
		.msg_size = sizeof(struct mbs_message_ack_response_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_MESHCORE)
	[MBS_LLEXT_ZBUS_MESHCORE_ADVERT_REQUEST_CHAN] = {
		.chan = &mbs_meshcore_advert_request_chan,
		.msg_size = sizeof(mbs_meshcore_advert_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_MESHCORE_NODE_DISCOVER_REQUEST_CHAN] = {
		.chan = &mbs_meshcore_node_discover_request_chan,
		.msg_size = sizeof(mbs_meshcore_node_discover_request_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_CONTACT)
	[MBS_LLEXT_ZBUS_CONTACT_SHARE_REQUEST_CHAN] = {
		.chan = &mbs_contact_share_request_chan,
		.msg_size = sizeof(mbs_contact_share_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_DISCOVER_PATH_REQUEST_CHAN] = {
		.chan = &mbs_contact_discover_path_request_chan,
		.msg_size = sizeof(mbs_contact_discover_path_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_TRACE_PATH_REQUEST_CHAN] = {
		.chan = &mbs_contact_trace_path_request_chan,
		.msg_size = sizeof(mbs_contact_trace_path_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_TELEMETRY_REQUEST_CHAN] = {
		.chan = &mbs_contact_telemetry_request_chan,
		.msg_size = sizeof(mbs_contact_telemetry_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_ADVERT_RESPONSE_CHAN] = {
		.chan = &mbs_contact_advert_response_chan,
		.msg_size = sizeof(mbs_contact_response_advert_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_PATH_RESPONSE_CHAN] = {
		.chan = &mbs_contact_path_response_chan,
		.msg_size = sizeof(mbs_contact_response_path_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_TRACE_PATH_RESPONSE_CHAN] = {
		.chan = &mbs_contact_trace_path_response_chan,
		.msg_size = sizeof(mbs_contact_response_trace_path_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_TELEMETRY_RESPONSE_CHAN] = {
		.chan = &mbs_contact_telemetry_response_chan,
		.msg_size = sizeof(mbs_contact_response_telemetry_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_CONTACT_DISCOVER_RESPONSE_CHAN] = {
		.chan = &mbs_contact_discover_response_chan,
		.msg_size = sizeof(mbs_contact_response_discover_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_POWER)
	[MBS_LLEXT_ZBUS_POWER_FUEL_GAUGE_DATA_CHAN] = {
		.chan = &mbs_power_fuel_gauge_data_chan,
		.msg_size = sizeof(struct mbs_power_fuel_gauge_data_event),
		.publish_allowed = true,
	},
#endif
#if defined(CONFIG_MBS_RADIO)
	[MBS_LLEXT_ZBUS_RADIO_RECEIVE_CHAN] = {
		.chan = &mbs_radio_receive_chan,
		.msg_size = sizeof(struct mbs_radio_receive_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_RADIO_PUBLISH_CHAN] = {
		.chan = &mbs_radio_publish_chan,
		.msg_size = sizeof(struct mbs_radio_publish_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_RADIO_TX_DONE_CHAN] = {
		.chan = &mbs_radio_tx_done_chan,
		.msg_size = sizeof(struct mbs_radio_tx_done_event),
		.publish_allowed = false,
	},
	[MBS_LLEXT_ZBUS_RADIO_STATE_CHAN] = {
		.chan = &mbs_radio_state_chan,
		.msg_size = sizeof(struct mbs_radio_state_event),
		.publish_allowed = false,
	},
	[MBS_LLEXT_ZBUS_RADIO_CW_CHAN] = {
		.chan = &mbs_radio_cw_chan,
		.msg_size = sizeof(struct mbs_radio_cw_request_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_RADIO_CW_DONE_CHAN] = {
		.chan = &mbs_radio_cw_done_chan,
		.msg_size = sizeof(struct mbs_radio_cw_done_event),
		.publish_allowed = false,
	},
#endif
#if defined(CONFIG_MBS_TELEMETRY)
	[MBS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN] = {
		.chan = &mbs_telemetry_data_chan,
		.msg_size = sizeof(struct mbs_telemetry_data_event),
		.publish_allowed = false,
	},
#endif
#if defined(CONFIG_MBS_INDICATOR)
	[MBS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN] = {
		.chan = &mbs_indicator_light_play_chan,
		.msg_size = sizeof(struct mbs_indicator_light_play_event),
		.publish_allowed = true,
	},
	[MBS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN] = {
		.chan = &mbs_indicator_buzzer_play_chan,
		.msg_size = sizeof(struct mbs_indicator_buzzer_play_event),
		.publish_allowed = true,
	},
#endif
};

static struct mbs_llext_bridge_subscriber
	zbus_subscribers[MBS_LLEXT_ZBUS_MAX_SUBSCRIBERS];
static struct mbs_llext_bridge zbus_bridge;

BUILD_ASSERT(MBS_LLEXT_ZBUS_CHANNEL_COUNT <= MBS_LLEXT_BRIDGE_MAX_CHANNELS,
	     "meshbus LLEXT ZBus bridge cannot address all public channels");

static bool mbs_llext_zbus_channel_is_valid(enum mbs_llext_zbus_channel channel)
{
	return channel >= 0 && channel < MBS_LLEXT_ZBUS_CHANNEL_COUNT;
}

static uint64_t mbs_llext_zbus_available_mask(void)
{
	uint64_t mask = 0ULL;

	for (int i = 0; i < MBS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan != NULL) {
			mask |= MBS_LLEXT_ZBUS_CH_BIT(i);
		}
	}

	return mask;
}

static int mbs_llext_zbus_channel_from_chan(const struct zbus_channel *chan)
{
	for (int i = 0; i < MBS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan == chan) {
			return i;
		}
	}

	return -ENOENT;
}

static void mbs_llext_zbus_listener_cb(const struct zbus_channel *chan)
{
	int channel;

	channel = mbs_llext_zbus_channel_from_chan(chan);
	if (channel < 0) {
		return;
	}

	(void)mbs_llext_bridge_schedule(&zbus_bridge, (size_t)channel);
}

ZBUS_LISTENER_DEFINE(mbs_llext_zbus_listener, mbs_llext_zbus_listener_cb);

static int mbs_llext_zbus_subscribe_channel(const struct zbus_channel *chan,
						const struct zbus_observer *obs)
{
	int rc;

	rc = zbus_chan_add_obs(chan, obs, K_NO_WAIT);
	if (rc == 0 || rc == -EALREADY || rc == -EEXIST) {
		return 0;
	}

	return rc;
}

static int mbs_llext_zbus_init(void)
{
	int first_err = 0;

	mbs_llext_bridge_init(&zbus_bridge,
			     zbus_subscribers,
			     ARRAY_SIZE(zbus_subscribers),
			     MBS_LLEXT_ZBUS_CHANNEL_COUNT);

	for (int i = 0; i < MBS_LLEXT_ZBUS_CHANNEL_COUNT; i++) {
		if (zbus_descs[i].chan == NULL) {
			continue;
		}

		int rc = mbs_llext_zbus_subscribe_channel(zbus_descs[i].chan,
							      &mbs_llext_zbus_listener);

		if ((rc != 0) && (first_err == 0)) {
			first_err = rc;
		}
	}

	return first_err;
}

SYS_INIT(mbs_llext_zbus_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int mbs_llext_zbus_subscribe(struct k_event *evt, uint64_t channel_mask)
{
	uint64_t allowed_mask = mbs_llext_zbus_available_mask();

	if (allowed_mask == 0ULL) {
		return -ENODEV;
	}

	return mbs_llext_bridge_subscribe(&zbus_bridge, evt, channel_mask, allowed_mask);
}

int mbs_llext_zbus_unsubscribe(struct k_event *evt)
{
	return mbs_llext_bridge_unsubscribe(&zbus_bridge, evt);
}

int mbs_llext_zbus_take_pending(struct k_event *evt, uint64_t *pending_mask)
{
	int rc;

	if (evt == NULL || pending_mask == NULL) {
		return -EINVAL;
	}

	rc = mbs_llext_bridge_take_pending(&zbus_bridge, evt, pending_mask);
	if (rc != 0) {
		return rc;
	}
	*pending_mask &= mbs_llext_zbus_available_mask();

	return (*pending_mask != 0ULL) ? 0 : -ENOMSG;
}

int mbs_llext_zbus_read(enum mbs_llext_zbus_channel channel, void *msg, size_t msg_size)
{
	const struct mbs_llext_zbus_desc *desc;

	if (!mbs_llext_zbus_channel_is_valid(channel) || msg == NULL) {
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

int mbs_llext_zbus_publish(enum mbs_llext_zbus_channel channel, const void *msg,
			       size_t msg_size)
{
	const struct mbs_llext_zbus_desc *desc;

	if (!mbs_llext_zbus_channel_is_valid(channel) || msg == NULL) {
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
