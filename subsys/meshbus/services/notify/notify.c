/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pb_encode.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(meshbus_notify, CONFIG_MESHBUS_NOTIFY_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define NOTIFY_NODE_PEER_PREFIX_BYTES MESHBUS_CONTACT_PREFIX_BYTES

BUILD_ASSERT(MESHBUS_NOTIFY_PAYLOAD_MAX_LEN <= UINT16_MAX);

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */

static bool notify_validator(const void *msg, size_t msg_size);
static bool notify_payload_is_valid(meshbus_notify_type type, const meshbus_notify *payload);
#if defined(CONFIG_MESHBUS_NOTIFY_TRANSPORT_SERIAL)
bool meshbus_notify_serial_ready(void);
int meshbus_notify_serial_write_frame(const uint8_t *payload, size_t len);
static void notify_publish_serial(const meshbus_notify_event *event);
#endif

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

ZBUS_CHAN_DEFINE(meshbus_notify_chan, meshbus_notify_event,
		 notify_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool notify_type_is_valid(meshbus_notify_type type)
{
	return type >= meshbus_NotifyType_NOTIFY_TYPE_NODES_CHANGED &&
	       type <= _meshbus_NotifyType_MAX;
}

static pb_size_t notify_payload_tag_for_type(meshbus_notify_type type)
{
	switch (type) {
	case MESHBUS_NOTIFY_TYPE_NODES_CHANGED:
		return MESHBUS_NOTIFY_TAG_NODE;
	case MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH:
		return MESHBUS_NOTIFY_TAG_NODE_DISCOVER_PATH;
	case MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH:
		return MESHBUS_NOTIFY_TAG_NODE_TRACE_PATH;
	case MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY:
		return MESHBUS_NOTIFY_TAG_NODE_TELEMETRY;
	case MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED:
		return MESHBUS_NOTIFY_TAG_MESSAGE;
	case MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED:
		return MESHBUS_NOTIFY_TAG_CHANNEL;
	case MESHBUS_NOTIFY_TYPE_NODE_ADVERT:
		return MESHBUS_NOTIFY_TAG_NODE_ADVERT;
	case MESHBUS_NOTIFY_TYPE_NODE_DISCOVER:
		return MESHBUS_NOTIFY_TAG_NODE_DISCOVER;
	case MESHBUS_NOTIFY_TYPE_MANAGEMENT_SMP:
		return MESHBUS_NOTIFY_TAG_MANAGEMENT_SMP;
	case MESHBUS_NOTIFY_TYPE_MESHCORE_TRACE:
		return MESHBUS_NOTIFY_TAG_MESHCORE_TRACE;
	default:
		return 0U;
	}
}

static bool notify_node_change_is_valid(const meshbus_Notify_NotifyNodeChange *node)
{
	if (node == NULL) {
		return false;
	}
	if (!node->has_public_key_prefix) {
		return node->public_key_prefix.size == 0U;
	}

	return node->public_key_prefix.size == NOTIFY_NODE_PEER_PREFIX_BYTES;
}

static bool notify_message_payload_is_valid(const meshbus_Notify_NotifyMessageChange *message)
{
	if (message == NULL) {
		return false;
	}
	if (message->event < MESHBUS_NOTIFY_MESSAGE_EVENT_RECV ||
	    message->event > MESHBUS_NOTIFY_MESSAGE_EVENT_ACK) {
		return false;
	}

	switch (message->event) {
	case MESHBUS_NOTIFY_MESSAGE_EVENT_RECV:
		return !message->has_ack_token;
	case MESHBUS_NOTIFY_MESSAGE_EVENT_ACK:
		return message->has_ack_token && message->ack_token != 0U;
	default:
		return false;
	}
}

static bool notify_channel_change_is_valid(const meshbus_Notify_NotifyChannelChange *channel)
{
	if (channel == NULL) {
		return false;
	}
	if (!channel->has_secret_prefix) {
		return channel->secret_prefix.size == 0U;
	}

	return channel->secret_prefix.size > 0U &&
	       channel->secret_prefix.size <= ARRAY_SIZE(channel->secret_prefix.bytes);
}

static bool notify_node_advert_is_valid(const meshbus_Notify_NotifyNodeAdvert *advert)
{
	if (advert == NULL) {
		return false;
	}
	if (advert->public_key.size != ARRAY_SIZE(advert->public_key.bytes)) {
		return false;
	}
	if (!advert->has_position && (advert->latitude != 0 || advert->longitude != 0)) {
		return false;
	}
	if (!advert->has_out_path &&
	    (advert->out_path.size != 0U || advert->path_hash_size != 0U)) {
		return false;
	}
	if (advert->has_out_path &&
	    (advert->path_hash_size == 0U ||
	     advert->path_hash_size > MESHBUS_CONTACT_PATH_HASH_SIZE_MAX ||
	     (advert->out_path.size % advert->path_hash_size) != 0U)) {
		return false;
	}

	return true;
}

static bool notify_node_discover_is_valid(
	const meshbus_Notify_NotifyNodeDiscover *discover)
{
	if (discover == NULL) {
		return false;
	}
	if (discover->public_key.size != ARRAY_SIZE(discover->public_key.bytes)) {
		return false;
	}
	if (discover->role < MESHBUS_CONTACT_ROLE_CHAT ||
	    discover->role > MESHBUS_CONTACT_ROLE_SENSOR) {
		return false;
	}
	if (discover->path.size > ARRAY_SIZE(discover->path.bytes)) {
		return false;
	}

	return true;
}

static bool notify_management_smp_is_valid(
	const meshbus_Notify_NotifyManagementSmp *smp)
{
	if (smp == NULL) {
		return false;
	}
	if (smp->tag == 0U || smp->response.size == 0U ||
	    smp->response.size > ARRAY_SIZE(smp->response.bytes)) {
		return false;
	}

	return true;
}

static bool notify_meshcore_trace_is_valid(
	const meshbus_Notify_NotifyMeshcoreTrace *trace)
{
	if (trace == NULL || trace->tag == 0U) {
		return false;
	}
	if (trace->out_path_snr_count >
	    ARRAY_SIZE(trace->out_path_snr)) {
		return false;
	}
	if (trace->return_path_snr_count >
	    ARRAY_SIZE(trace->return_path_snr)) {
		return false;
	}

	return true;
}

static bool notify_validator(const void *msg, size_t msg_size)
{
	const meshbus_notify_event *event = msg;

	if (msg == NULL || msg_size != sizeof(meshbus_notify_event)) {
		return false;
	}
	if (!notify_type_is_valid(event->type)) {
		return false;
	}
	if (event->payload_len == 0U ||
	    event->payload_len > MESHBUS_NOTIFY_PAYLOAD_MAX_LEN) {
		return false;
	}

	return true;
}

static bool notify_payload_is_valid(meshbus_notify_type type, const meshbus_notify *payload)
{
	pb_size_t expected_tag;

	if (payload == NULL || !notify_type_is_valid(type)) {
		return false;
	}

	expected_tag = notify_payload_tag_for_type(type);
	if (expected_tag == 0U || payload->which_payload_variant != expected_tag) {
		return false;
	}

	switch (type) {
	case MESHBUS_NOTIFY_TYPE_NODES_CHANGED:
		if (!notify_node_change_is_valid(&payload->payload_variant.node)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH:
		if (payload->payload_variant.node_discover_path.public_key_prefix.size !=
		    NOTIFY_NODE_PEER_PREFIX_BYTES ||
		    payload->payload_variant.node_discover_path.out_path.size >
		    ARRAY_SIZE(payload->payload_variant.node_discover_path.out_path.bytes) ||
		    payload->payload_variant.node_discover_path.out_path_snr_count >
		    ARRAY_SIZE(payload->payload_variant.node_discover_path.out_path_snr) ||
		    payload->payload_variant.node_discover_path.return_path_snr_count >
		    ARRAY_SIZE(payload->payload_variant.node_discover_path.return_path_snr)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH:
		if (payload->payload_variant.node_trace_path.public_key_prefix.size !=
		    NOTIFY_NODE_PEER_PREFIX_BYTES ||
		    payload->payload_variant.node_trace_path.out_path_snr_count >
		    ARRAY_SIZE(payload->payload_variant.node_trace_path.out_path_snr) ||
		    payload->payload_variant.node_trace_path.return_path_snr_count >
		    ARRAY_SIZE(payload->payload_variant.node_trace_path.return_path_snr)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY:
		if (payload->payload_variant.node_telemetry.public_key_prefix.size !=
		    NOTIFY_NODE_PEER_PREFIX_BYTES) {
			return false;
		}
		if (payload->payload_variant.node_telemetry.payload.size >
		    ARRAY_SIZE(payload->payload_variant.node_telemetry.payload.bytes)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED:
		if (!notify_message_payload_is_valid(&payload->payload_variant.message)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED:
		if (!notify_channel_change_is_valid(&payload->payload_variant.channel)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_NODE_ADVERT:
		if (!notify_node_advert_is_valid(&payload->payload_variant.node_advert)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_NODE_DISCOVER:
		if (!notify_node_discover_is_valid(
			    &payload->payload_variant.node_discover)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_MANAGEMENT_SMP:
		if (!notify_management_smp_is_valid(
			    &payload->payload_variant.management_smp)) {
			return false;
		}
		break;
	case MESHBUS_NOTIFY_TYPE_MESHCORE_TRACE:
		if (!notify_meshcore_trace_is_valid(
			    &payload->payload_variant.meshcore_trace)) {
			return false;
		}
		break;
	default:
		return false;
	}

	return true;
}

#if defined(CONFIG_MESHBUS_NOTIFY_TRANSPORT_SERIAL)
static void notify_publish_serial(const meshbus_notify_event *event)
{
	int rc;

	if (event == NULL) {
		return;
	}
	if (event->payload_len == 0U) {
		return;
	}
	if (!meshbus_notify_serial_ready()) {
		LOG_DBG("Notify serial endpoint unavailable: type=%u",
			(unsigned int)event->type);
		return;
	}

	rc = meshbus_notify_serial_write_frame(event->payload, event->payload_len);
	if (rc != 0) {
		LOG_DBG("Notify serial publish failed: type=%u len=%zu rc=%d",
			(unsigned int)event->type, (size_t)event->payload_len, rc);
		return;
	}
}

static void notify_serial_init(void)
{
	if (!meshbus_notify_serial_ready()) {
		LOG_WRN("Notify serial waiting for zephyr,uart-mcumgr");
	} else {
		LOG_INF("Notify serial ready");
	}
}
#endif

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_notify_publish(meshbus_notify_type type, const meshbus_notify *payload)
{
	meshbus_notify_event *event;
	pb_ostream_t stream;
	int rc;

	if (payload == NULL) {
		return -EINVAL;
	}
	if (!notify_type_is_valid(type)) {
		return -EINVAL;
	}
	if (!notify_payload_is_valid(type, payload)) {
		return -EINVAL;
	}

	/* The encoded event is intentionally heap-backed: keeping this large
	 * transport buffer alongside a protobuf payload on a service or MCUmgr
	 * caller's stack can exceed otherwise adequate worker stacks.
	 */
	event = k_calloc(1U, sizeof(*event));
	if (event == NULL) {
		return -ENOMEM;
	}

	event->type = type;
	stream = pb_ostream_from_buffer(event->payload, sizeof(event->payload));
	if (!pb_encode(&stream, meshbus_Notify_fields, payload)) {
		LOG_DBG("Notify payload too large: type=%u", (unsigned int)type);
		rc = -EMSGSIZE;
		goto out;
	}
	if (stream.bytes_written == 0U || stream.bytes_written > UINT16_MAX) {
		rc = -EMSGSIZE;
		goto out;
	}
	event->payload_len = (uint16_t)stream.bytes_written;

	rc = zbus_chan_pub(&meshbus_notify_chan, event, K_NO_WAIT);
	if (rc != 0) {
		LOG_WRN("Notify publish failed: type=%u rc=%d", (unsigned int)type, rc);
		goto out;
	}

#if defined(CONFIG_MESHBUS_NOTIFY_TRANSPORT_SERIAL)
	notify_publish_serial(event);
#endif

	LOG_DBG("Notify published: type=%u len=%u", (unsigned int)type,
		event->payload_len);
out:
	memset(event, 0, sizeof(*event));
	k_free(event);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_notify_init(void)
{
#if defined(CONFIG_MESHBUS_NOTIFY_TRANSPORT_SERIAL)
	notify_serial_init();
#endif
	LOG_INF("Notify service ready");
	return 0;
}

SYS_INIT(meshbus_notify_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
