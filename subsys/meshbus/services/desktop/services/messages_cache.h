/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/meshbus/message.h>

#ifdef __cplusplus
extern "C" {
#endif

struct desktop_messages_cache_key {
	meshbus_message_type type;
	meshbus_message_route route;
	uint64_t timestamp;
	uint8_t target[MESHBUS_MESSAGE_TARGET_PREFIX_BYTES];
	uint16_t payload_len;
	uint8_t payload[CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN];
	char sender_name[MESHBUS_MESSAGE_SENDER_NAME_MAX_LEN];
};

struct desktop_messages_cache_entry {
	struct desktop_messages_cache_key key;
	meshbus_message_content message;
	bool unread;
	bool timestamp_realtime;
};

bool desktop_messages_cache_key_is_valid(const struct desktop_messages_cache_key *key);
bool desktop_messages_cache_key_equal(const struct desktop_messages_cache_key *a,
				      const struct desktop_messages_cache_key *b);
bool desktop_messages_cache_copy_received(uint32_t newest_position,
					  struct desktop_messages_cache_entry *entry_out);
bool desktop_messages_cache_copy_unread_received(
	uint32_t newest_position, struct desktop_messages_cache_entry *entry_out);
uint32_t desktop_messages_cache_received_count(void);
uint32_t desktop_messages_cache_unread_received_count(void);
uint32_t desktop_messages_cache_update_seq(void);
void desktop_messages_cache_mark_read(const struct desktop_messages_cache_key *key);
void desktop_messages_cache_mark_unread(const struct desktop_messages_cache_key *key);

#ifdef __cplusplus
}
#endif
