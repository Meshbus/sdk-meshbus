/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "llext_bridge_common.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/sys/util.h>

static uint64_t mbs_llext_bridge_valid_mask(size_t channel_count)
{
	if (channel_count >= MBS_LLEXT_BRIDGE_MAX_CHANNELS) {
		return UINT64_MAX;
	}

	return BIT64(channel_count) - 1ULL;
}

static void mbs_llext_bridge_notify_work_handler(struct k_work *work)
{
	struct mbs_llext_bridge *bridge = CONTAINER_OF(work, struct mbs_llext_bridge, notify_work);
	uint64_t pending_mask;
	k_spinlock_key_t key;

	key = k_spin_lock(&bridge->pending_lock);
	pending_mask = bridge->pending_mask;
	bridge->pending_mask = 0ULL;
	k_spin_unlock(&bridge->pending_lock, key);

	if (pending_mask == 0ULL) {
		return;
	}

	k_mutex_lock(&bridge->mutex, K_FOREVER);
	for (size_t i = 0U; i < bridge->subscribers_len; i++) {
		uint64_t post_mask;
		struct mbs_llext_bridge_subscriber *sub = &bridge->subscribers[i];

		if (sub->evt == NULL) {
			continue;
		}

		post_mask = pending_mask & sub->channel_mask;
		if (post_mask != 0ULL) {
			sub->pending_mask |= post_mask;
			(void)k_event_post(sub->evt, MBS_LLEXT_ZBUS_EVT_PENDING);
		}
	}
	k_mutex_unlock(&bridge->mutex);
}

void mbs_llext_bridge_init(struct mbs_llext_bridge *bridge,
			  struct mbs_llext_bridge_subscriber *subscribers,
			  size_t subscribers_len,
			  size_t channel_count)
{
	bridge->subscribers = subscribers;
	bridge->subscribers_len = subscribers_len;
	bridge->channel_count = channel_count;
	k_mutex_init(&bridge->mutex);
	bridge->pending_mask = 0ULL;
	k_work_init(&bridge->notify_work, mbs_llext_bridge_notify_work_handler);
}

int mbs_llext_bridge_schedule(struct mbs_llext_bridge *bridge, size_t channel_id)
{
	int rc;
	k_spinlock_key_t key;

	if (channel_id >= bridge->channel_count || bridge->channel_count > MBS_LLEXT_BRIDGE_MAX_CHANNELS) {
		return -EINVAL;
	}

	key = k_spin_lock(&bridge->pending_lock);
	bridge->pending_mask |= BIT64(channel_id);
	k_spin_unlock(&bridge->pending_lock, key);

	rc = k_work_submit(&bridge->notify_work);
	if (rc == -EBUSY) {
		return 0;
	}

	return rc;
}

int mbs_llext_bridge_subscribe(struct mbs_llext_bridge *bridge,
			      struct k_event *evt,
			      uint64_t channel_mask,
			      uint64_t allowed_mask)
{
	uint64_t valid_mask;

	if (evt == NULL) {
		return -EINVAL;
	}

	if (bridge->channel_count == 0U || bridge->channel_count > MBS_LLEXT_BRIDGE_MAX_CHANNELS) {
		return -EINVAL;
	}

	valid_mask = mbs_llext_bridge_valid_mask(bridge->channel_count);

	if ((channel_mask == 0ULL) ||
	    ((channel_mask & ~allowed_mask) != 0ULL) ||
	    ((channel_mask & ~valid_mask) != 0ULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&bridge->mutex, K_FOREVER);

	for (size_t i = 0U; i < bridge->subscribers_len; i++) {
		if (bridge->subscribers[i].evt == evt) {
			bridge->subscribers[i].channel_mask = channel_mask;
			bridge->subscribers[i].pending_mask = 0ULL;
			k_mutex_unlock(&bridge->mutex);
			return 0;
		}
	}

	for (size_t i = 0U; i < bridge->subscribers_len; i++) {
		if (bridge->subscribers[i].evt == NULL) {
			bridge->subscribers[i].evt = evt;
			bridge->subscribers[i].channel_mask = channel_mask;
			bridge->subscribers[i].pending_mask = 0ULL;
			k_mutex_unlock(&bridge->mutex);
			return 0;
		}
	}

	k_mutex_unlock(&bridge->mutex);
	return -ENOMEM;
}

int mbs_llext_bridge_unsubscribe(struct mbs_llext_bridge *bridge, struct k_event *evt)
{
	if (evt == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&bridge->mutex, K_FOREVER);
	for (size_t i = 0U; i < bridge->subscribers_len; i++) {
		if (bridge->subscribers[i].evt == evt) {
			bridge->subscribers[i].evt = NULL;
			bridge->subscribers[i].channel_mask = 0ULL;
			bridge->subscribers[i].pending_mask = 0ULL;
			k_mutex_unlock(&bridge->mutex);
			return 0;
		}
	}
	k_mutex_unlock(&bridge->mutex);

	return -ENOENT;
}

int mbs_llext_bridge_take_pending(struct mbs_llext_bridge *bridge,
				 struct k_event *evt,
				 uint64_t *pending_mask)
{
	int rc = -ENOENT;

	if (evt == NULL || pending_mask == NULL) {
		return -EINVAL;
	}
	*pending_mask = 0ULL;

	k_mutex_lock(&bridge->mutex, K_FOREVER);
	for (size_t i = 0U; i < bridge->subscribers_len; i++) {
		struct mbs_llext_bridge_subscriber *sub = &bridge->subscribers[i];

		if (sub->evt != evt) {
			continue;
		}

		*pending_mask = sub->pending_mask;
		sub->pending_mask = 0ULL;
		rc = *pending_mask != 0ULL ? 0 : -ENOMSG;
		break;
	}
	k_mutex_unlock(&bridge->mutex);

	return rc;
}
