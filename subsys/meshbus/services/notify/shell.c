/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "common/shell.h"

#define NOTIFY_HELP_ROOT \
	SHELL_HELP("Notify debug publish commands", NULL)
#define NOTIFY_HELP_NODES_CHANGED \
	SHELL_HELP("Publish nodes-changed notify", "[public_key_prefix_hex]")
#define NOTIFY_HELP_DISCOVER_PATH \
	SHELL_HELP("Publish node discover-path notify", "<public_key_prefix_hex> <out_path_hex>")
#define NOTIFY_HELP_TRACE_PATH \
	SHELL_HELP("Publish node trace-path notify", "<public_key_prefix_hex> <state>")
#define NOTIFY_HELP_TELEMETRY \
	SHELL_HELP("Publish node telemetry notify", "<public_key_prefix_hex> <payload_hex>")
#define NOTIFY_HELP_NODE_ADVERT                                                                     \
	SHELL_HELP("Publish node-advert notify",                                                     \
		   "<public_key_hex> <response_snr> <has_position> [latitude longitude]")
#define NOTIFY_HELP_MESSAGE \
	SHELL_HELP("Publish messages-changed notify", "<recv|ack> [ack_token]")
#define NOTIFY_HELP_CHANNELS_CHANGED \
	SHELL_HELP("Publish channels-changed notify", "[secret_prefix_hex]")

#define NOTIFY_PATH_MAX_LEN 64U

#define NOTIFY_NODE_PEER_PREFIX_BYTES MESHBUS_CONTACT_PREFIX_BYTES

static void notify_bytes_set(pb_bytes_array_t *dst, size_t len)
{
	if (dst == NULL) {
		return;
	}

	dst->size = (pb_size_t)len;
}

static int parse_hex_arg(const char *arg, uint8_t *out, size_t out_size, size_t *out_len)
{
	size_t arg_len;
	int parsed;

	if (arg == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	arg_len = strlen(arg);
	if (arg_len == 0U || (arg_len % 2U) != 0U) {
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, out, out_size);
	if (parsed < 0) {
		return parsed;
	}

	*out_len = (size_t)parsed;
	return 0;
}

static int notify_publish_with_log(const struct shell *sh, meshbus_notify_type type,
				   const meshbus_notify *payload)
{
	int rc;

	if (payload == NULL) {
		return -EINVAL;
	}

	rc = meshbus_notify_publish(type, payload);
	if (rc != 0) {
		mb_shell_error(sh, rc);
		return rc;
	}

	shell_print(sh, "published: type=%u", (unsigned int)type);
	return 0;
}

static int cmd_notify_nodes_changed(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	size_t prefix_len = 0U;
	int rc;

	if (argc == 2U) {
		rc = parse_hex_arg(argv[1], payload.payload_variant.node.public_key_prefix.bytes,
				   sizeof(payload.payload_variant.node.public_key_prefix.bytes),
				   &prefix_len);
		if (rc != 0 || prefix_len != NOTIFY_NODE_PEER_PREFIX_BYTES) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		payload.payload_variant.node.has_public_key_prefix = true;
		notify_bytes_set((pb_bytes_array_t *)&payload.payload_variant.node.public_key_prefix,
				 prefix_len);
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE;
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_NODES_CHANGED, &payload);
}

static int cmd_notify_discover_path(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	pb_bytes_array_t *prefix =
		(pb_bytes_array_t *)&payload.payload_variant.node_discover_path.public_key_prefix;
	size_t prefix_len = 0U;
	size_t out_path_len = 0U;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_arg(argv[1], prefix->bytes, sizeof(prefix->bytes), &prefix_len);
	if (rc != 0 || prefix_len != NOTIFY_NODE_PEER_PREFIX_BYTES) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = parse_hex_arg(argv[2], payload.payload_variant.node_discover_path.out_path.bytes,
			   sizeof(payload.payload_variant.node_discover_path.out_path.bytes),
			   &out_path_len);
	if (rc != 0 || out_path_len > NOTIFY_PATH_MAX_LEN) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_DISCOVER_PATH;
	notify_bytes_set(prefix, prefix_len);
	notify_bytes_set((pb_bytes_array_t *)&payload.payload_variant.node_discover_path.out_path,
			 out_path_len);
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH, &payload);
}

static int cmd_notify_trace_path(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	pb_bytes_array_t *prefix =
		(pb_bytes_array_t *)&payload.payload_variant.node_trace_path.public_key_prefix;
	size_t prefix_len = 0U;
	uint32_t state;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_arg(argv[1], prefix->bytes, sizeof(prefix->bytes), &prefix_len);
	if (rc != 0 || prefix_len != NOTIFY_NODE_PEER_PREFIX_BYTES) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = mb_shell_parse_u32_arg(argv[2], &state);
	if (rc != 0 || state > UINT8_MAX) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_TRACE_PATH;
	notify_bytes_set(prefix, prefix_len);
	payload.payload_variant.node_trace_path.state = (uint8_t)state;
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH, &payload);
}

static int cmd_notify_telemetry(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	size_t prefix_len = 0U;
	size_t payload_len = 0U;
	int rc;

	ARG_UNUSED(argc);

	rc = parse_hex_arg(argv[1], payload.payload_variant.node_telemetry.public_key_prefix.bytes,
			   sizeof(payload.payload_variant.node_telemetry.public_key_prefix.bytes),
			   &prefix_len);
	if (rc != 0 || prefix_len != NOTIFY_NODE_PEER_PREFIX_BYTES) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = parse_hex_arg(argv[2], payload.payload_variant.node_telemetry.payload.bytes,
			   sizeof(payload.payload_variant.node_telemetry.payload.bytes),
			   &payload_len);
	if (rc != 0) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_TELEMETRY;
	notify_bytes_set((pb_bytes_array_t *)&payload.payload_variant.node_telemetry.public_key_prefix,
			 prefix_len);
	notify_bytes_set((pb_bytes_array_t *)&payload.payload_variant.node_telemetry.payload,
			 payload_len);
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY, &payload);
}

static int cmd_notify_node_advert(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	pb_bytes_array_t *public_key =
		(pb_bytes_array_t *)&payload.payload_variant.node_advert.public_key;
	size_t public_key_len = 0U;
	int32_t response_snr;
	size_t next_arg;
	bool has_position;
	int rc;

	rc = parse_hex_arg(argv[1], public_key->bytes, sizeof(public_key->bytes), &public_key_len);
	rc |= mb_shell_parse_i32_arg(argv[2], &response_snr);
	rc |= mb_shell_parse_bool_arg(argv[3], &has_position);
	if (rc != 0 || response_snr < INT8_MIN || response_snr > INT8_MAX) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}
	if (public_key_len != sizeof(public_key->bytes)) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	next_arg = 4U;
	if (has_position) {
		if (argc < next_arg + 2U) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		rc = mb_shell_parse_i32_arg(argv[next_arg],
					    &payload.payload_variant.node_advert.latitude);
		rc |= mb_shell_parse_i32_arg(argv[next_arg + 1U],
					     &payload.payload_variant.node_advert.longitude);
		if (rc != 0) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		next_arg += 2U;
	}

	if (argc != next_arg) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_NODE_ADVERT;
	notify_bytes_set(public_key, public_key_len);
	payload.payload_variant.node_advert.response_snr = response_snr;
	payload.payload_variant.node_advert.has_position = has_position;
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_NODE_ADVERT, &payload);
}

static int cmd_notify_message(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	uint64_t ack_token = 0U;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;

	if (strcmp(argv[1], "recv") == 0) {
		if (argc != 2U) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_RECV;
		return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
	}

	if (strcmp(argv[1], "ack") != 0 || argc != 3U) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	rc = mb_shell_parse_u64_arg(argv[2], &ack_token);
	if (rc != 0 || ack_token == 0U) {
		mb_shell_invalid(sh);
		return -EINVAL;
	}

	payload.payload_variant.message.event = MESHBUS_NOTIFY_MESSAGE_EVENT_ACK;
	payload.payload_variant.message.has_ack_token = true;
	payload.payload_variant.message.ack_token = ack_token;
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &payload);
}

static int cmd_notify_channels_changed(const struct shell *sh, size_t argc, char **argv)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	size_t prefix_len = 0U;
	int rc;

	if (argc == 2U) {
		rc = parse_hex_arg(argv[1], payload.payload_variant.channel.secret_prefix.bytes,
				   sizeof(payload.payload_variant.channel.secret_prefix.bytes),
				   &prefix_len);
		if (rc != 0 || prefix_len == 0U) {
			mb_shell_invalid(sh);
			return -EINVAL;
		}
		payload.payload_variant.channel.has_secret_prefix = true;
		notify_bytes_set(
			(pb_bytes_array_t *)&payload.payload_variant.channel.secret_prefix,
			prefix_len);
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;
	return notify_publish_with_log(sh, MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
}

SHELL_SUBCMD_SET_CREATE(meshbus_notify_subcmds, (meshbus, notify));

SHELL_SUBCMD_ADD((meshbus, notify), nodes_changed, NULL, NOTIFY_HELP_NODES_CHANGED,
		 cmd_notify_nodes_changed, 1, 1);
SHELL_SUBCMD_ADD((meshbus, notify), discover_path, NULL, NOTIFY_HELP_DISCOVER_PATH,
		 cmd_notify_discover_path, 3, 0);
SHELL_SUBCMD_ADD((meshbus, notify), trace_path, NULL, NOTIFY_HELP_TRACE_PATH,
		 cmd_notify_trace_path, 3, 0);
SHELL_SUBCMD_ADD((meshbus, notify), telemetry, NULL, NOTIFY_HELP_TELEMETRY,
		 cmd_notify_telemetry, 3, 0);
SHELL_SUBCMD_ADD((meshbus, notify), node_advert, NULL, NOTIFY_HELP_NODE_ADVERT,
		 cmd_notify_node_advert, 4, 2);
SHELL_SUBCMD_ADD((meshbus, notify), message, NULL, NOTIFY_HELP_MESSAGE,
		 cmd_notify_message, 2, 1);
SHELL_SUBCMD_ADD((meshbus, notify), channels_changed, NULL, NOTIFY_HELP_CHANNELS_CHANGED,
		 cmd_notify_channels_changed, 1, 1);

SHELL_SUBCMD_ADD((meshbus), notify, &meshbus_notify_subcmds, NOTIFY_HELP_ROOT, NULL, 0, 0);
