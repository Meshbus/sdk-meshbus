/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Message API
 *
 * This module owns message send request/response channels and the local
 * message receive queue.
 */

#ifndef MESHBUS_INCLUDE_MESSAGE_H_
#define MESHBUS_INCLUDE_MESSAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "meshbus/message.pb.h"
#include <contact/contact.h>

#if defined(CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES)
#define MBS_MESSAGE_CHANNEL_SECRET_PREFIX_BYTES \
	CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES
#else
#define MBS_MESSAGE_CHANNEL_SECRET_PREFIX_BYTES 3U
#endif

#define MBS_MESSAGE_TARGET_PREFIX_BYTES 4U
#define MBS_MESSAGE_SENDER_NAME_MAX_LEN 32U

#if defined(CONFIG_MBS_MESSAGE_TX_MAX_LEN)
#define MBS_MESSAGE_TX_MAX_LEN CONFIG_MBS_MESSAGE_TX_MAX_LEN
#else
#define MBS_MESSAGE_TX_MAX_LEN 160U
#endif

typedef meshbus_MessageContent_MessageType mbs_message_type;
typedef meshbus_MessageContent_MessageRoute mbs_message_route;
typedef meshbus_MessageContent mbs_message_content;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Send text message to node request event payload. */
struct mbs_message_send_to_node_request_event {
	uint8_t key_prefix[MBS_CONTACT_PREFIX_BYTES];
	uint8_t attempt;
	bool flood;
	uint16_t payload_len;
	uint8_t payload[MBS_MESSAGE_TX_MAX_LEN];
};

/** @brief Send text message to channel request event payload. */
struct mbs_message_send_to_channel_request_event {
	/** Stable local channel slot index. */
	uint8_t channel_index;
	uint16_t payload_len;
	uint8_t payload[MBS_MESSAGE_TX_MAX_LEN];
};

/** @brief Message receive response event payload. */
struct mbs_message_response_event {
	/** Message direction and address kind: RECEIVE_NODE or RECEIVE_CHANNEL. */
	mbs_message_type type;
	/** Received packet route. Transport routes are normalized to FLOOD or DIRECT. */
	mbs_message_route route;
	uint8_t target[MBS_MESSAGE_TARGET_PREFIX_BYTES];
	uint16_t payload_len;
	uint8_t payload[MBS_MESSAGE_TX_MAX_LEN];
	char sender_name[MBS_MESSAGE_SENDER_NAME_MAX_LEN];
	uint64_t sender_timestamp;
	bool has_rx_snr;
	float rx_snr;
};

/** @brief Message ACK response event payload. */
struct mbs_message_ack_response_event {
	uint8_t target[MBS_MESSAGE_TARGET_PREFIX_BYTES];
	uint8_t attempt;
};

ZBUS_CHAN_DECLARE(mbs_message_send_to_node_request_chan);
ZBUS_CHAN_DECLARE(mbs_message_send_to_channel_request_chan);
ZBUS_CHAN_DECLARE(mbs_message_response_chan);
ZBUS_CHAN_DECLARE(mbs_message_ack_response_chan);

/**
 * @brief Request sending a text message to a node (async).
 *
 * @param public_key_prefix Recipient public key prefix bytes.
 * @param payload Message payload bytes.
 * @param payload_len Payload length, limited by CONFIG_MBS_MESSAGE_TX_MAX_LEN.
 * Attempts above 3 reserve two MeshCore text bytes and accept at most 158 bytes.
 * @param flood true to force flood route; false to use automatic route.
 * @param attempt Client-provided ACK correlation token. Any uint8_t value is
 * accepted, but duplicate pending attempts are rejected.
 * @param[out] out_ack_token Optional local ACK token generated from the current
 * meshbus session and sequence. Set to 0 on failure when provided.
 * @return 0 on acceptance; -EMSGSIZE when an extended attempt exceeds its
 * MeshCore text budget; another negative errno on failure.
 */
int mbs_message_send_to_node(const uint8_t *public_key_prefix,
				 const uint8_t *payload, size_t payload_len,
				 bool flood, uint8_t attempt,
				 uint64_t *out_ack_token);

/**
 * @brief Request sending a text message to a channel (async).
 *
 * @param channel_index Channel slot index.
 * @param payload Message payload bytes.
 * @param payload_len Payload length. The maximum accepted channel payload is
 * CONFIG_MBS_MESSAGE_TX_MAX_LEN minus the current MeshCore node-name length
 * and the two-byte ": " sender separator.
 * @return 0 on success; -EMSGSIZE when the sender prefix and payload exceed the
 * MeshCore text limit; another negative errno on failure.
 */
int mbs_message_send_to_channel(size_t channel_index, const uint8_t *payload,
				    size_t payload_len);

/**
 * @brief Read the next queued inbound message and remove it from the queue.
 *
 * @param[out] message Message buffer.
 * @return 0 on success; -ENOENT when the queue is empty; negative errno on
 * failure.
 */
int mbs_message_next(mbs_message_content *message);

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_MESSAGE_H_ */
