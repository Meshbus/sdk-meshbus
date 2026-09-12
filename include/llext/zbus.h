/* SPDX-License-Identifier: Apache-2.0 */

/* Copyright (c) 2026 FoBE Studio */

/**
 * @file
 * @brief Meshbus LLEXT ZBus subscriptions and message access
 */

#ifndef MESHBUS_INCLUDE_LLEXT_ZBUS_H_
#define MESHBUS_INCLUDE_LLEXT_ZBUS_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LLEXT bridge wake event bit posted to subscriber @ref k_event. */
#define MBS_LLEXT_ZBUS_EVT_PENDING (1U << 31)

/** @brief Unified Meshbus ZBus channel IDs for LLEXT bridge APIs. */
enum mbs_llext_zbus_channel {
	MBS_LLEXT_ZBUS_INPUT_KEY_CHAN = 0,
	MBS_LLEXT_ZBUS_INPUT_ACTION_CHAN = 1,
	MBS_LLEXT_ZBUS_NOTIFY_CHAN = 2,
	MBS_LLEXT_ZBUS_BLUETOOTH_PAIRING_CHAN = 3,
	MBS_LLEXT_ZBUS_BLUETOOTH_STATE_CHAN = 4,
	MBS_LLEXT_ZBUS_DISPLAY_STATE_CHAN = 5,
	MBS_LLEXT_ZBUS_GNSS_DATA_CHAN = 6,
	MBS_LLEXT_ZBUS_MESSAGE_SEND_TO_NODE_REQUEST_CHAN = 8,
	MBS_LLEXT_ZBUS_MESSAGE_SEND_TO_CHANNEL_REQUEST_CHAN = 9,
	MBS_LLEXT_ZBUS_MESSAGE_RESPONSE_CHAN = 10,
	MBS_LLEXT_ZBUS_MESSAGE_ACK_RESPONSE_CHAN = 11,
	MBS_LLEXT_ZBUS_MESSAGE_RECEIVED_CHAN = 12,
	MBS_LLEXT_ZBUS_MESHCORE_ADVERT_REQUEST_CHAN = 14,
	MBS_LLEXT_ZBUS_CONTACT_SHARE_REQUEST_CHAN = 15,
	MBS_LLEXT_ZBUS_CONTACT_DISCOVER_PATH_REQUEST_CHAN = 16,
	MBS_LLEXT_ZBUS_CONTACT_TRACE_PATH_REQUEST_CHAN = 17,
	MBS_LLEXT_ZBUS_CONTACT_TELEMETRY_REQUEST_CHAN = 18,
	MBS_LLEXT_ZBUS_CONTACT_ADVERT_RESPONSE_CHAN = 19,
	MBS_LLEXT_ZBUS_CONTACT_PATH_RESPONSE_CHAN = 20,
	MBS_LLEXT_ZBUS_CONTACT_TRACE_PATH_RESPONSE_CHAN = 21,
	MBS_LLEXT_ZBUS_CONTACT_TELEMETRY_RESPONSE_CHAN = 22,
	MBS_LLEXT_ZBUS_POWER_FUEL_GAUGE_DATA_CHAN = 23,
	MBS_LLEXT_ZBUS_RADIO_RECEIVE_CHAN = 24,
	MBS_LLEXT_ZBUS_RADIO_PUBLISH_CHAN = 25,
	MBS_LLEXT_ZBUS_TELEMETRY_DATA_CHAN = 26,
	MBS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN = 27,
	MBS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN = 28,
	MBS_LLEXT_ZBUS_RADIO_TX_DONE_CHAN = 29,
	MBS_LLEXT_ZBUS_RADIO_STATE_CHAN = 30,
	MBS_LLEXT_ZBUS_MESHCORE_NODE_DISCOVER_REQUEST_CHAN = 31,
	MBS_LLEXT_ZBUS_CONTACT_DISCOVER_RESPONSE_CHAN = 32,
	MBS_LLEXT_ZBUS_RADIO_CW_CHAN = 33,
	MBS_LLEXT_ZBUS_RADIO_CW_DONE_CHAN = 34,
	MBS_LLEXT_ZBUS_CHANNEL_COUNT,
};

BUILD_ASSERT(MBS_LLEXT_ZBUS_CHANNEL_COUNT <= 64,
	     "meshbus LLEXT ZBus bridge bit masks support at most 64 channels");

/** @brief Convert one LLEXT ZBus channel ID to a subscription/pending bit. */
#define MBS_LLEXT_ZBUS_CH_BIT(channel) (UINT64_C(1) << (channel))

/**
 * @brief Subscribe a service event object to Meshbus ZBus bridge channels.
 *
 * The caller owns @p evt and must keep it valid until unsubscribed or until the
 * service exits. @p channel_mask uses @ref MBS_LLEXT_ZBUS_CH_BIT.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments or channel bits are invalid.
 * @retval -ENODEV if no bridge channels are available.
 * @retval -ENOMEM if the subscriber table is full.
 */
int mbs_llext_zbus_subscribe(struct k_event *evt, uint64_t channel_mask);

/**
 * @brief Remove a previous LLEXT ZBus bridge subscription.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p evt is NULL.
 * @retval -ENOENT if @p evt is not subscribed.
 */
int mbs_llext_zbus_unsubscribe(struct k_event *evt);

/**
 * @brief Take pending bridge channels for a subscribed event object.
 *
 * The returned mask is cleared from the subscription. A subscriber should call
 * this after waiting for @ref MBS_LLEXT_ZBUS_EVT_PENDING on @p evt.
 *
 * @param evt Subscribed event object.
 * @param pending_mask Output channel bit mask.
 *
 * @retval 0 if one or more channel bits were returned.
 * @retval -EINVAL if arguments are invalid.
 * @retval -ENOENT if @p evt is not subscribed.
 * @retval -ENOMSG if no channel is pending.
 */
int mbs_llext_zbus_take_pending(struct k_event *evt, uint64_t *pending_mask);

/**
 * @brief Read the latest message from a bridged Meshbus ZBus channel.
 *
 * @param channel LLEXT bridge channel id.
 * @param msg Output message buffer.
 * @param msg_size Size of @p msg. Must exactly match the channel payload type.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid or @p msg_size does not match.
 * @retval -ENODEV if the channel is not available in this firmware build.
 * @retval negative errno from zbus_chan_read().
 */
int mbs_llext_zbus_read(enum mbs_llext_zbus_channel channel, void *msg, size_t msg_size);

/**
 * @brief Publish a message to a bridged Meshbus ZBus channel.
 *
 * Applications can publish to the channels permitted by the bridge.
 * @p msg_size must exactly match the channel payload type.
 *
 * @retval 0 on success.
 * @retval -EINVAL if arguments are invalid or @p msg_size does not match.
 * @retval -ENODEV if the channel is not available in this firmware build.
 * @retval -ENOTSUP if publishing is not allowed for @p channel.
 * @retval negative errno from zbus_chan_pub().
 */
int mbs_llext_zbus_publish(enum mbs_llext_zbus_channel channel, const void *msg,
			       size_t msg_size);


#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_LLEXT_ZBUS_H_ */
