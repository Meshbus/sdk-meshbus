/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <message/message.h>

#include "mbs_shell_internal.h"

#define MESSAGE_NODE_PREFIX_BYTES CONFIG_MBS_CONTACT_PREFIX_BYTES
#define MESSAGE_HELP_ROOT SHELL_HELP("Message module control and configuration", NULL)
#define MESSAGE_HELP_NEXT SHELL_HELP("Read and remove the next queued inbound message", NULL)
#define MESSAGE_HELP_SEND_NODE SHELL_HELP("Send a text message to a node", NULL)
#define MESSAGE_HELP_SEND_CHAN SHELL_HELP("Send a text message to a channel", NULL)

static bool message_payload_is_text(const mbs_message_content *message)
{
	for (size_t i = 0U; i < message->payload.size; i++) {
		uint8_t ch = message->payload.bytes[i];

		if (!isprint(ch) && !isspace(ch)) {
			return false;
		}
	}

	return true;
}

static int parse_key_prefix(const struct shell *sh, const char *arg,
			    uint8_t out[MESSAGE_NODE_PREFIX_BYTES])
{
	uint8_t decoded[32];
	size_t arg_len;
	int parsed;

	if (arg == NULL) {
		shell_print(sh, "missing key_prefix");
		return -EINVAL;
	}

	arg_len = strlen(arg);
	if ((arg_len == 0U) || ((arg_len % 2U) != 0U)) {
		shell_print(sh, "hex decode failed");
		return -EINVAL;
	}

	parsed = hex2bin(arg, arg_len, decoded, sizeof(decoded));
	if (parsed < 0 || (size_t)parsed < MESSAGE_NODE_PREFIX_BYTES) {
		shell_print(sh, "hex decode failed");
		return -EINVAL;
	}
	memcpy(out, decoded, sizeof(decoded[0]) * MESSAGE_NODE_PREFIX_BYTES);

	return 0;
}

static void shell_print_message(const struct shell *sh, const mbs_message_content *message)
{
	shell_print(sh, "type: %u", (unsigned int)message->type);
	shell_print(sh, "routeType: %u", (unsigned int)message->route);
	shell_print(sh, "target:");
	shell_hexdump(sh, message->target.bytes, message->target.size);
	if (message->sender_name[0] != 0) {
		shell_print(sh, "sender_name: %s", message->sender_name);
	}

	if (message->type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL) {
		shell_print(sh, "scope: channel");
	} else {
		shell_print(sh, "scope: node");
	}

	shell_print(sh, "payload_len: %u", (unsigned int)message->payload.size);
	if (message_payload_is_text(message)) {
		shell_print(sh, "payload_text: %.*s", (int)message->payload.size,
			    message->payload.bytes);
	} else {
		shell_print(sh, "payload_hex:");
		shell_hexdump(sh, message->payload.bytes, message->payload.size);
	}

	shell_print(sh, "timestamp_ms: %llu", (unsigned long long)message->timestamp);
	shell_print(sh, "sender_timestamp_ms: %llu",
		    (unsigned long long)message->sender_timestamp);
	if (message->has_rx_snr) {
		shell_print(sh, "rx_snr: %.2f dB", (double)message->rx_snr);
	}
}

static int cmd_message_next(const struct shell *sh, size_t argc, char **argv)
{
	mbs_message_content message = meshbus_MessageContent_init_zero;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = mbs_message_next(&message);
	if (rc != 0) {
		mbs_shell_error(sh, (uint16_t)(-rc));
		return rc;
	}

	shell_print_message(sh, &message);
	return 0;
}

static int cmd_message_send_node(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t key_prefix[MESSAGE_NODE_PREFIX_BYTES];
	uint64_t ack_token = 0U;
	uint32_t attempt = 0U;
	size_t msg_len;
	bool flood = false;
	int rc;

	if (argc != 4 && argc != 5) {
		shell_print(sh,
			    "Usage: meshbus message send_to_node <key_prefix> <attempt> <message> [flood:0|1]");
		return -EINVAL;
	}

	rc = parse_key_prefix(sh, argv[1], key_prefix);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_shell_parse_u32_arg(argv[2], &attempt);
	if (rc != 0 || attempt > UINT8_MAX) {
		shell_print(sh, "invalid attempt");
		return -EINVAL;
	}

	msg_len = strlen(argv[3]);
	if (msg_len == 0U || msg_len > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
		shell_print(sh, "message length out of range");
		return -EINVAL;
	}

	if (argc == 5) {
		rc = mbs_shell_parse_bool_arg(argv[4], &flood);
		if (rc != 0) {
			shell_print(sh, "invalid flood arg");
			return rc;
		}
	}

	rc = mbs_message_send_to_node(key_prefix, (const uint8_t *)argv[3], msg_len, flood,
					  (uint8_t)attempt, &ack_token);
	if (rc != 0) {
		mbs_shell_error(sh, (uint16_t)(-rc));
		return rc;
	}

	shell_print(sh, "ack_token: %llu", (unsigned long long)ack_token);
	return 0;
}

static int cmd_message_send_channel(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t channel_index = 0U;
	size_t msg_len;
	int rc;

	if (argc != 3) {
		shell_print(sh, "Usage: meshbus message send_to_channel <channel_index> <message>");
		return -EINVAL;
	}

	rc = mbs_shell_parse_u32_arg(argv[1], &channel_index);
	if (rc != 0 || channel_index > UINT8_MAX) {
		shell_print(sh, "invalid channel_index");
		return -EINVAL;
	}

	msg_len = strlen(argv[2]);
	if (msg_len == 0U || msg_len > CONFIG_MBS_MESSAGE_TX_MAX_LEN) {
		shell_print(sh, "message length out of range");
		return -EINVAL;
	}

	rc = mbs_message_send_to_channel((size_t)channel_index, (const uint8_t *)argv[2],
					     msg_len);
	if (rc != 0) {
		mbs_shell_error(sh, (uint16_t)(-rc));
		return rc;
	}

	return 0;
}

SHELL_SUBCMD_SET_CREATE(mbs_message_subcmds, (meshbus, message));

SHELL_SUBCMD_ADD((meshbus, message), next, NULL, MESSAGE_HELP_NEXT, cmd_message_next, 1, 0);
SHELL_SUBCMD_ADD((meshbus, message), send_to_node, NULL, MESSAGE_HELP_SEND_NODE,
		 cmd_message_send_node, 4, 1);
SHELL_SUBCMD_ADD((meshbus, message), send_to_channel, NULL, MESSAGE_HELP_SEND_CHAN,
		 cmd_message_send_channel, 3, 0);

SHELL_SUBCMD_ADD((meshbus), message, &mbs_message_subcmds, MESSAGE_HELP_ROOT, NULL, 0, 0);
