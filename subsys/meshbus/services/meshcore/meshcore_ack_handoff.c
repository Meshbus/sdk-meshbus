/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "meshcore_prvi.h"

#ifndef CONFIG_MESHBUS_MESHCORE_LOG_LEVEL
#define CONFIG_MESHBUS_MESHCORE_LOG_LEVEL LOG_LEVEL_INF
#endif

LOG_MODULE_REGISTER(meshbus_meshcore_ack_handoff,
		    CONFIG_MESHBUS_MESHCORE_LOG_LEVEL);

#define ACK_HANDOFF_RETRY_DELAY K_MSEC(1)

static struct k_spinlock ack_handoff_lock;
static struct meshbus_message_ack_response_event
	ack_handoff_queue[MESHBUS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH];
static uint8_t ack_handoff_head;
static uint8_t ack_handoff_count;

static bool meshbus_meshcore_ack_handoff_peek(
	struct meshbus_message_ack_response_event *event)
{
	k_spinlock_key_t key;
	bool present;

	key = k_spin_lock(&ack_handoff_lock);
	present = ack_handoff_count > 0U;
	if (present) {
		*event = ack_handoff_queue[ack_handoff_head];
	}
	k_spin_unlock(&ack_handoff_lock, key);

	return present;
}

static void meshbus_meshcore_ack_handoff_pop(void)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&ack_handoff_lock);
	if (ack_handoff_count > 0U) {
		ack_handoff_head =
			(uint8_t)((ack_handoff_head + 1U) %
				  MESHBUS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH);
		ack_handoff_count--;
	}
	k_spin_unlock(&ack_handoff_lock, key);
}

static int meshbus_meshcore_ack_handoff_enqueue(
	const struct meshbus_message_ack_response_event *event)
{
	k_spinlock_key_t key;
	uint8_t tail;
	int rc = 0;

	key = k_spin_lock(&ack_handoff_lock);
	if (ack_handoff_count >= MESHBUS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH) {
		rc = -ENOBUFS;
	} else {
		tail = (uint8_t)((ack_handoff_head + ack_handoff_count) %
				 MESHBUS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH);
		ack_handoff_queue[tail] = *event;
		ack_handoff_count++;
	}
	k_spin_unlock(&ack_handoff_lock, key);

	return rc;
}

static bool meshbus_meshcore_ack_handoff_has_queued(void)
{
	k_spinlock_key_t key;
	bool queued;

	key = k_spin_lock(&ack_handoff_lock);
	queued = ack_handoff_count > 0U;
	k_spin_unlock(&ack_handoff_lock, key);

	return queued;
}

static void meshbus_meshcore_ack_handoff_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(ack_handoff_work,
			meshbus_meshcore_ack_handoff_work_handler);

static int meshbus_meshcore_ack_handoff_schedule(k_timeout_t delay)
{
	int rc;

	rc = k_work_reschedule(&ack_handoff_work, delay);
	if (rc < 0) {
		LOG_ERR("ACK handoff schedule failed: %d", rc);
	}

	return rc < 0 ? rc : 0;
}

static void meshbus_meshcore_ack_handoff_work_handler(struct k_work *work)
{
	struct meshbus_message_ack_response_event event;
	int rc;

	ARG_UNUSED(work);

	while (!meshbus_meshcore_activation_pending() &&
	       meshbus_meshcore_ack_handoff_peek(&event)) {
		rc = zbus_chan_pub(&meshbus_message_ack_response_chan, &event,
				   K_NO_WAIT);
		if (rc != 0) {
			LOG_DBG("ACK handoff publish busy: %d", rc);
			(void)meshbus_meshcore_ack_handoff_schedule(
				ACK_HANDOFF_RETRY_DELAY);
			return;
		}
		meshbus_meshcore_ack_handoff_pop();
	}
}

int meshbus_meshcore_ack_handoff_publish(
	const struct meshbus_message_ack_response_event *event)
{
	int rc;

	if (event == NULL) {
		return -EINVAL;
	}

	if (!meshbus_meshcore_ack_handoff_has_queued()) {
		rc = zbus_chan_pub(&meshbus_message_ack_response_chan, event,
				   K_NO_WAIT);
		if (rc == 0) {
			return 0;
		}
	}

	rc = meshbus_meshcore_ack_handoff_enqueue(event);
	if (rc != 0) {
		LOG_ERR("ACK handoff queue full");
		return rc;
	}

	return meshbus_meshcore_ack_handoff_schedule(K_NO_WAIT);
}

void meshbus_meshcore_ack_handoff_reset(void)
{
	struct k_work_sync sync;

	/* Called on meshcore_wq, never on the system queue owning this work. */
	(void)k_work_cancel_delayable_sync(&ack_handoff_work, &sync);
	k_spinlock_key_t key = k_spin_lock(&ack_handoff_lock);

	ack_handoff_head = 0;
	ack_handoff_count = 0;
	k_spin_unlock(&ack_handoff_lock, key);
}
