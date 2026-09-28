/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <message/message.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESSAGES_CONTENT_MAX 160U

enum zui_messages_route_mode {
	MESSAGES_ROUTE_FLOOD = 0,
	MESSAGES_ROUTE_DIRECT = 1,
};

enum zui_messages_target_kind {
	MESSAGES_TARGET_KIND_CHANNEL = 0,
	MESSAGES_TARGET_KIND_NODE = 1,
};

struct zui_messages_target_entry {
	enum zui_messages_target_kind kind;
	uint8_t channel_idx;
	uint8_t channel_secret_prefix[MBS_MESSAGE_CHANNEL_SECRET_PREFIX_BYTES];
	uint8_t node_key_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	char name[24];
};

#ifdef __cplusplus
}
#endif
