/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/sys/util.h>

#include "common/mgmt.h"
#include "meshbus/message.pb.h"

#include <zephyr/meshbus/message.h>

LOG_MODULE_REGISTER(meshbus_message_mgmt, CONFIG_MESHBUS_MESSAGE_LOG_LEVEL);

#define MESSAGE_NODE_PREFIX_BYTES CONFIG_MESHBUS_CONTACT_PREFIX_BYTES
#define MESHBUS_MESSAGE_MGMT_PROTO_RSP_MAX_SIZE \
	MAX(MAX(meshbus_MessageNextResponse_size, meshbus_MessageSendNodeResponse_size), \
	    meshbus_MessageSendChannelResponse_size)

#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
static int meshbus_message_mgmt_translate_error_code(uint16_t err)
{
	return (int)err;
}
#endif

static void meshbus_message_mgmt_copy_runtime_to_proto(meshbus_MessageContent *dst,
						       const meshbus_message_content *src)
{
	size_t target_len;
	size_t payload_len;

	*dst = (meshbus_MessageContent)meshbus_MessageContent_init_zero;
	if (src == NULL) {
		return;
	}

	target_len = MIN((size_t)src->target.size, sizeof(dst->target.bytes));
	dst->target.size = target_len;
	if (target_len > 0U) {
		memcpy(dst->target.bytes, src->target.bytes, target_len);
	}

	dst->type = (meshbus_MessageContent_MessageType)src->type;
	dst->route = (meshbus_MessageContent_MessageRoute)src->route;
	(void)strncpy(dst->sender_name, src->sender_name, sizeof(dst->sender_name) - 1U);
	dst->sender_name[sizeof(dst->sender_name) - 1U] = '\0';

	payload_len = MIN((size_t)src->payload.size, sizeof(dst->payload.bytes));
	dst->payload.size = payload_len;
	if (payload_len > 0U) {
		memcpy(dst->payload.bytes, src->payload.bytes, payload_len);
	}

	dst->timestamp = src->timestamp;
	dst->sender_timestamp = src->sender_timestamp;
	dst->has_rx_snr = src->has_rx_snr;
	if (src->has_rx_snr) {
		dst->rx_snr = src->rx_snr;
	}
}

static int meshbus_message_mgmt_decode_key_prefix(const pb_bytes_array_t *prefix,
						  uint8_t out[MESSAGE_NODE_PREFIX_BYTES])
{
	if (prefix == NULL || prefix->size != MESSAGE_NODE_PREFIX_BYTES) {
		return -EINVAL;
	}

	memcpy(out, prefix->bytes, MESSAGE_NODE_PREFIX_BYTES);
	return 0;
}

static int meshbus_message_mgmt_decode_text_payload(
	const char *message, uint8_t out[CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN], size_t *out_len)
{
	size_t len;

	if (message == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	len = strnlen(message, CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN + 1U);
	if (len == 0U || len > CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN) {
		return -EINVAL;
	}

	memcpy(out, message, len);
	*out_len = len;
	return 0;
}

static int meshbus_message_mgmt_next(struct smp_streamer *ctxt)
{
	meshbus_MessageNextRequest req = meshbus_MessageNextRequest_init_zero;
	meshbus_MessageNextResponse rsp = meshbus_MessageNextResponse_init_zero;
	meshbus_message_content message = meshbus_MessageContent_init_zero;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_MessageNextRequest_fields,
				      true);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_message_next(&message);
	if (rc != 0) {
		return rc;
	}

	rsp.has_message = true;
	meshbus_message_mgmt_copy_runtime_to_proto(&rsp.message, &message);
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_MessageNextResponse_fields,
				    MESHBUS_MESSAGE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_message_mgmt_send_node(struct smp_streamer *ctxt)
{
	meshbus_MessageSendNodeRequest req = meshbus_MessageSendNodeRequest_init_zero;
	meshbus_MessageSendNodeResponse rsp = meshbus_MessageSendNodeResponse_init_zero;
	uint8_t prefix_bytes[MESSAGE_NODE_PREFIX_BYTES];
	uint8_t message_buf[CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN];
	uint64_t ack_token = 0U;
	size_t message_len = 0U;
	bool flood = false;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req), meshbus_MessageSendNodeRequest_fields,
				      false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_message_mgmt_decode_key_prefix((const pb_bytes_array_t *)&req.key_prefix,
						    prefix_bytes);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_message_mgmt_decode_text_payload(req.message, message_buf, &message_len);
	if (rc != 0) {
		return rc;
	}

	if (req.has_flood) {
		flood = req.flood;
	}

	if (!req.has_attempt) {
		return -EINVAL;
	}

	rc = meshbus_message_send_to_node(prefix_bytes, message_buf, message_len, flood,
					  (uint8_t)req.attempt, &ack_token);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	rsp.ack_token = ack_token;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_MessageSendNodeResponse_fields,
				    MESHBUS_MESSAGE_MGMT_PROTO_RSP_MAX_SIZE);
}

static int meshbus_message_mgmt_send_channel(struct smp_streamer *ctxt)
{
	meshbus_MessageSendChannelRequest req = meshbus_MessageSendChannelRequest_init_zero;
	meshbus_MessageSendChannelResponse rsp = meshbus_MessageSendChannelResponse_init_zero;
	uint8_t message_buf[CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN];
	size_t message_len = 0U;
	int rc = mb_mgmt_decode_proto(ctxt, &req, sizeof(req),
				      meshbus_MessageSendChannelRequest_fields, false);

	if (rc != 0) {
		return rc;
	}

	rc = meshbus_message_mgmt_decode_text_payload(req.message, message_buf, &message_len);
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_message_send_to_channel((size_t)req.channel_index, message_buf, message_len);
	if (rc != 0) {
		return rc;
	}

	rsp.accepted = true;
	return mb_mgmt_encode_proto(ctxt, &rsp, meshbus_MessageSendChannelResponse_fields,
				    MESHBUS_MESSAGE_MGMT_PROTO_RSP_MAX_SIZE);
}

static const struct mgmt_handler meshbus_message_mgmt_group_handlers[] = {
	[meshbus_MessageMgmtCommandId_MESSAGE_MGMT_COMMAND_ID_SEND_NODE] =
		{ NULL, meshbus_message_mgmt_send_node },
	[meshbus_MessageMgmtCommandId_MESSAGE_MGMT_COMMAND_ID_SEND_CHANNEL] =
		{ NULL, meshbus_message_mgmt_send_channel },
	[meshbus_MessageMgmtCommandId_MESSAGE_MGMT_COMMAND_ID_NEXT] =
		{ meshbus_message_mgmt_next, NULL },
};

static struct mgmt_group meshbus_message_mgmt_group = {
	.mg_handlers = meshbus_message_mgmt_group_handlers,
	.mg_handlers_count = ARRAY_SIZE(meshbus_message_mgmt_group_handlers),
	.mg_group_id = (uint16_t)meshbus_MessageMgmtGroupId_MESSAGE_MGMT_GROUP_ID_MESHBUS_MESSAGE,
#ifdef CONFIG_MCUMGR_SMP_SUPPORT_ORIGINAL_PROTOCOL
	.mg_translate_error = meshbus_message_mgmt_translate_error_code,
#endif
#ifdef CONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME
	.mg_group_name = "meshbus message mgmt",
#endif
};

static int meshbus_message_mgmt_init(void)
{
	mgmt_register_group(&meshbus_message_mgmt_group);
	return 0;
}

SYS_INIT(meshbus_message_mgmt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
