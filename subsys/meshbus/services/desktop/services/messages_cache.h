/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/meshbus/message.h>

#ifdef __cplusplus
extern "C" {
#endif

struct desktop_messages_cache_entry {
	uint64_t entry_id;
	meshbus_message_content message;
	bool unread;
	bool timestamp_realtime;
};

bool desktop_messages_cache_copy_received(uint32_t newest_position,
					  struct desktop_messages_cache_entry *entry_out);
bool desktop_messages_cache_copy_unread_received(
	uint32_t newest_position, struct desktop_messages_cache_entry *entry_out);
uint32_t desktop_messages_cache_received_count(void);
uint32_t desktop_messages_cache_unread_received_count(void);
uint32_t desktop_messages_cache_update_seq(void);
void desktop_messages_cache_mark_read(uint64_t entry_id);
void desktop_messages_cache_mark_unread(uint64_t entry_id);

#ifdef __cplusplus
}
#endif
