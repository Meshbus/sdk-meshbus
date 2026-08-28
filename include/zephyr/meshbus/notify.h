/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshbus Notify API
 *
 * This module only exposes a unified publish API for external notifications.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_NOTIFY_H_
#define ZEPHYR_INCLUDE_MESHBUS_NOTIFY_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "meshbus/notify.pb.h"

/** @brief Notify payload shared by runtime publishers and transports. */
typedef meshbus_Notify meshbus_notify;
typedef meshbus_NotifyType meshbus_notify_type;
typedef meshbus_Notify_NotifyMessageChange_MessageStateEvent meshbus_notify_message_event;

/** @brief Maximum encoded @ref meshbus_notify payload length. */
#define MESHBUS_NOTIFY_PAYLOAD_MAX_LEN meshbus_Notify_size

#define MESHBUS_NOTIFY_TYPE_NODES_CHANGED \
	meshbus_NotifyType_NOTIFY_TYPE_NODES_CHANGED
#define MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH meshbus_NotifyType_NOTIFY_TYPE_NODE_DISCOVER_PATH
#define MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH meshbus_NotifyType_NOTIFY_TYPE_NODE_TRACE_PATH
#define MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY meshbus_NotifyType_NOTIFY_TYPE_NODE_TELEMETRY
#define MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED \
	meshbus_NotifyType_NOTIFY_TYPE_MESSAGES_CHANGED
#define MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED \
	meshbus_NotifyType_NOTIFY_TYPE_CHANNELS_CHANGED
#define MESHBUS_NOTIFY_TYPE_NODE_ADVERT meshbus_NotifyType_NOTIFY_TYPE_NODE_ADVERT
#define MESHBUS_NOTIFY_TYPE_NODE_DISCOVER meshbus_NotifyType_NOTIFY_TYPE_NODE_DISCOVER
#define MESHBUS_NOTIFY_TYPE_MANAGEMENT_SMP \
	meshbus_NotifyType_NOTIFY_TYPE_MANAGEMENT_SMP
#define MESHBUS_NOTIFY_TYPE_MESHCORE_TRACE \
	meshbus_NotifyType_NOTIFY_TYPE_MESHCORE_TRACE

#define MESHBUS_NOTIFY_MESSAGE_EVENT_RECV \
	meshbus_Notify_NotifyMessageChange_MessageStateEvent_MESSAGE_STATE_EVENT_RECV
#define MESHBUS_NOTIFY_MESSAGE_EVENT_ACK \
	meshbus_Notify_NotifyMessageChange_MessageStateEvent_MESSAGE_STATE_EVENT_ACK

#define MESHBUS_NOTIFY_TAG_NODE meshbus_Notify_node_tag
#define MESHBUS_NOTIFY_TAG_NODE_DISCOVER_PATH meshbus_Notify_node_discover_path_tag
#define MESHBUS_NOTIFY_TAG_NODE_TRACE_PATH meshbus_Notify_node_trace_path_tag
#define MESHBUS_NOTIFY_TAG_NODE_TELEMETRY meshbus_Notify_node_telemetry_tag
#define MESHBUS_NOTIFY_TAG_MESSAGE meshbus_Notify_message_tag
#define MESHBUS_NOTIFY_TAG_CHANNEL meshbus_Notify_channel_tag
#define MESHBUS_NOTIFY_TAG_NODE_ADVERT meshbus_Notify_node_advert_tag
#define MESHBUS_NOTIFY_TAG_NODE_DISCOVER meshbus_Notify_node_discover_tag
#define MESHBUS_NOTIFY_TAG_MANAGEMENT_SMP meshbus_Notify_management_smp_tag
#define MESHBUS_NOTIFY_TAG_MESHCORE_TRACE meshbus_Notify_meshcore_trace_tag

/** @brief Encoded runtime notify event delivered over zbus. */
typedef struct meshbus_notify_event {
	/** Notify type matching the encoded payload variant. */
	meshbus_notify_type type;
	/** Valid bytes in @ref payload. */
	uint16_t payload_len;
	/** Encoded @ref meshbus_notify protobuf bytes. */
	uint8_t payload[MESHBUS_NOTIFY_PAYLOAD_MAX_LEN];
} meshbus_notify_event;

#define MESHBUS_NOTIFY_EVENT_INIT_ZERO \
	{                                  \
		.type = (meshbus_notify_type)0,    \
		.payload_len = 0U,                 \
		.payload = { 0 },                  \
	}

#ifdef __cplusplus
extern "C" {
#endif

ZBUS_CHAN_DECLARE(meshbus_notify_chan);

/**
 * @brief Publish a runtime notify payload.
 *
 * The decoded payload is validated and encoded once before it is published to
 * @ref meshbus_notify_chan.
 *
 * @param type Notify type matching @p payload.
 * @param payload Decoded notify payload.
 * @return 0 on success, negative errno on failure.
 */
int meshbus_notify_publish(meshbus_notify_type type, const meshbus_notify *payload);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_NOTIFY_H_ */
