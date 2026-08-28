/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MESHBUS_LLEXT_BRIDGE_COMMON_H_
#define MESHBUS_LLEXT_BRIDGE_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/sys/util.h>

#define MB_LLEXT_BRIDGE_MAX_CHANNELS 64U

struct mb_llext_bridge_subscriber {
	struct k_event *evt;
	uint64_t channel_mask;
	uint64_t pending_mask;
};

struct mb_llext_bridge {
	struct mb_llext_bridge_subscriber *subscribers;
	size_t subscribers_len;
	size_t channel_count;
	struct k_mutex mutex;
	struct k_spinlock pending_lock;
	uint64_t pending_mask;
	struct k_work notify_work;
};

void mb_llext_bridge_init(struct mb_llext_bridge *bridge,
			  struct mb_llext_bridge_subscriber *subscribers,
			  size_t subscribers_len,
			  size_t channel_count);

int mb_llext_bridge_schedule(struct mb_llext_bridge *bridge, size_t channel_id);

int mb_llext_bridge_subscribe(struct mb_llext_bridge *bridge,
			      struct k_event *evt,
			      uint64_t channel_mask,
			      uint64_t allowed_mask);

int mb_llext_bridge_unsubscribe(struct mb_llext_bridge *bridge, struct k_event *evt);

int mb_llext_bridge_take_pending(struct mb_llext_bridge *bridge,
				 struct k_event *evt,
				 size_t *channel_id);

#endif /* MESHBUS_LLEXT_BRIDGE_COMMON_H_ */
