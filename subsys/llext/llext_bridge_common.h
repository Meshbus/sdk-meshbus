/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBS_LLEXT_BRIDGE_COMMON_H_
#define MBS_LLEXT_BRIDGE_COMMON_H_

#include <llext/zbus.h>

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define MBS_LLEXT_BRIDGE_MAX_CHANNELS 64U

struct mbs_llext_bridge_subscriber {
	struct k_event *evt;
	uint64_t channel_mask;
	uint64_t pending_mask;
};

struct mbs_llext_bridge {
	struct mbs_llext_bridge_subscriber *subscribers;
	size_t subscribers_len;
	size_t channel_count;
	struct k_mutex mutex;
	struct k_spinlock pending_lock;
	uint64_t pending_mask;
	struct k_work notify_work;
};

void mbs_llext_bridge_init(struct mbs_llext_bridge *bridge,
			  struct mbs_llext_bridge_subscriber *subscribers,
			  size_t subscribers_len,
			  size_t channel_count);

int mbs_llext_bridge_schedule(struct mbs_llext_bridge *bridge, size_t channel_id);

int mbs_llext_bridge_subscribe(struct mbs_llext_bridge *bridge,
			      struct k_event *evt,
			      uint64_t channel_mask,
			      uint64_t allowed_mask);

int mbs_llext_bridge_unsubscribe(struct mbs_llext_bridge *bridge, struct k_event *evt);

int mbs_llext_bridge_take_pending(struct mbs_llext_bridge *bridge,
				 struct k_event *evt,
				 uint64_t *pending_mask);

#endif /* MBS_LLEXT_BRIDGE_COMMON_H_ */
