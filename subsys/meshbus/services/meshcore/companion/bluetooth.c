/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/bluetooth.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "protocol.h"

LOG_MODULE_DECLARE(meshcore_companion_adapter, CONFIG_MESHBUS_MESHCORE_LOG_LEVEL);

#define MESHBUS_MESHCORE_COMPANION_BLUETOOTH_TX_RETRY_DELAY_MS 20U

#if defined(CONFIG_ZTEST)
#define COMPANION_BLUETOOTH_TEST_VISIBLE
#else
#define COMPANION_BLUETOOTH_TEST_VISIBLE static
#endif

BUILD_ASSERT(CONFIG_BT_L2CAP_TX_MTU >= (MESHCORE_COMPANION_MAX_FRAME_SIZE + 3U),
	     "MeshCore Companion Bluetooth requires ATT MTU large enough for max frames");

struct companion_bluetooth_rx_frame {
	size_t len;
	uint8_t data[MESHCORE_COMPANION_MAX_FRAME_SIZE];
};

struct companion_bluetooth_tx_frame {
	size_t len;
	uint8_t data[MESHCORE_COMPANION_MAX_FRAME_SIZE];
};

static atomic_t companion_bluetooth_connected = ATOMIC_INIT(0);
COMPANION_BLUETOOTH_TEST_VISIBLE atomic_t companion_bluetooth_drop_count = ATOMIC_INIT(0);
COMPANION_BLUETOOTH_TEST_VISIBLE atomic_t companion_bluetooth_rx_count = ATOMIC_INIT(0);
COMPANION_BLUETOOTH_TEST_VISIBLE atomic_t companion_bluetooth_tx_count = ATOMIC_INIT(0);
static struct k_work companion_bluetooth_rx_work;
static struct k_work_delayable companion_bluetooth_tx_work;
COMPANION_BLUETOOTH_TEST_VISIBLE struct k_work_q companion_bluetooth_workq;
static const struct k_work_queue_config companion_bluetooth_workq_config = {
	.name = "meshcore_bt_wq",
};
COMPANION_BLUETOOTH_TEST_VISIBLE bool companion_bluetooth_workq_started;
static struct bt_nus_cb companion_bluetooth_cb;
K_THREAD_STACK_DEFINE(companion_bluetooth_workq_stack,
		      CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH_WORKQUEUE_STACK_SIZE);
K_MSGQ_DEFINE(companion_bluetooth_rx_msgq, sizeof(struct companion_bluetooth_rx_frame),
	      CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH_RX_QUEUE_DEPTH, 4);
K_MSGQ_DEFINE(companion_bluetooth_tx_msgq, sizeof(struct companion_bluetooth_tx_frame),
	      CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH_TX_QUEUE_DEPTH, 4);

static void companion_bluetooth_rx_work_handler(struct k_work *work);
static void companion_bluetooth_tx_work_handler(struct k_work *work);
static void companion_bluetooth_received(struct bt_conn *conn, const void *data,
					 uint16_t len, void *ctx);
static void companion_bluetooth_notif_enabled(bool enabled, void *ctx);
static void companion_bluetooth_state_listener_cb(const struct zbus_channel *chan);

static bool companion_bluetooth_ready(void)
{
	return atomic_get(&companion_bluetooth_connected) != 0 &&
	       meshbus_bluetooth_meshcore_companion_enabled();
}

static int companion_bluetooth_schedule_tx(k_timeout_t delay)
{
	int rc;

	rc = k_work_schedule_for_queue(&companion_bluetooth_workq, &companion_bluetooth_tx_work,
				       delay);
	if (rc < 0 && rc != -EBUSY) {
		LOG_DBG("Companion Bluetooth TX work schedule failed: rc=%d", rc);
		return rc;
	}

	return 0;
}

static void companion_bluetooth_set_connected(bool connected)
{
	if (connected) {
		if (atomic_cas(&companion_bluetooth_connected, 0, 1)) {
			meshcore_companion_adapter_connected();
		}
		return;
	}

	if (!atomic_cas(&companion_bluetooth_connected, 1, 0)) {
		return;
	}

	k_msgq_purge(&companion_bluetooth_rx_msgq);
	k_msgq_purge(&companion_bluetooth_tx_msgq);
	(void)k_work_cancel(&companion_bluetooth_rx_work);
	(void)k_work_cancel_delayable(&companion_bluetooth_tx_work);
	meshcore_companion_adapter_disconnected();
}

static int companion_bluetooth_notify_frame(const uint8_t *frame, size_t len)
{
	int rc;

	if (frame == NULL || len == 0U || len > MESHCORE_COMPANION_MAX_FRAME_SIZE) {
		return -EINVAL;
	}

	if (!companion_bluetooth_ready()) {
		return -ENOTCONN;
	}

	rc = bt_nus_send(NULL, frame, (uint16_t)len);
	if (rc == 0) {
		atomic_inc(&companion_bluetooth_tx_count);
	}
	return rc;
}

static int companion_bluetooth_transport_send(const uint8_t *frame, size_t len,
					      void *user_data)
{
	struct companion_bluetooth_tx_frame queued = {0};
	int rc;

	ARG_UNUSED(user_data);

	if (frame == NULL || len == 0U || len > MESHCORE_COMPANION_MAX_FRAME_SIZE) {
		return -EINVAL;
	}

	if (!companion_bluetooth_ready()) {
		return -ENOTCONN;
	}

	queued.len = len;
	memcpy(queued.data, frame, len);

	rc = k_msgq_put(&companion_bluetooth_tx_msgq, &queued, K_NO_WAIT);
	if (rc != 0) {
		atomic_inc(&companion_bluetooth_drop_count);
		LOG_DBG("Companion Bluetooth TX queue full: len=%u rc=%d",
			(unsigned int)len, rc);
		return rc == -ENOMSG ? -ENOSPC : rc;
	}

	rc = companion_bluetooth_schedule_tx(K_NO_WAIT);
	if (rc != 0) {
		atomic_inc(&companion_bluetooth_drop_count);
		return rc;
	}

	return 0;
}

static void companion_bluetooth_notif_enabled(bool enabled, void *ctx)
{
	ARG_UNUSED(ctx);

	LOG_DBG("Companion Bluetooth TX CCC %s", enabled ? "enabled" : "disabled");
	if (enabled && companion_bluetooth_ready()) {
		(void)companion_bluetooth_schedule_tx(K_NO_WAIT);
	}
}

static void companion_bluetooth_received(struct bt_conn *conn, const void *data,
					 uint16_t len, void *ctx)
{
	struct companion_bluetooth_rx_frame frame = {0};
	int rc;

	ARG_UNUSED(ctx);

	if (!companion_bluetooth_ready() ||
	    !meshbus_bluetooth_connection_is_authorized(conn)) {
		atomic_inc(&companion_bluetooth_drop_count);
		return;
	}

	if (data == NULL || len == 0U || len > MESHCORE_COMPANION_MAX_FRAME_SIZE) {
		atomic_inc(&companion_bluetooth_drop_count);
		return;
	}

	frame.len = len;
	memcpy(frame.data, data, len);

	rc = k_msgq_put(&companion_bluetooth_rx_msgq, &frame, K_NO_WAIT);
	if (rc != 0) {
		atomic_inc(&companion_bluetooth_drop_count);
		return;
	}

	rc = k_work_submit_to_queue(&companion_bluetooth_workq, &companion_bluetooth_rx_work);
	if (rc < 0 && rc != -EBUSY) {
		atomic_inc(&companion_bluetooth_drop_count);
	}
}

static void companion_bluetooth_rx_work_handler(struct k_work *work)
{
	struct companion_bluetooth_rx_frame frame = {0};

	ARG_UNUSED(work);

	while (k_msgq_get(&companion_bluetooth_rx_msgq, &frame, K_NO_WAIT) == 0) {
		if (!companion_bluetooth_ready()) {
			atomic_inc(&companion_bluetooth_drop_count);
			continue;
		}

		atomic_inc(&companion_bluetooth_rx_count);
		(void)meshcore_companion_adapter_rx_frame(frame.data, frame.len);
	}
}

static void companion_bluetooth_tx_work_handler(struct k_work *work)
{
	struct companion_bluetooth_tx_frame frame = {0};

	ARG_UNUSED(work);

	while (k_msgq_peek(&companion_bluetooth_tx_msgq, &frame) == 0) {
		int rc;

		if (!companion_bluetooth_ready()) {
			return;
		}

		rc = companion_bluetooth_notify_frame(frame.data, frame.len);
		if (rc == 0) {
			(void)k_msgq_get(&companion_bluetooth_tx_msgq, &frame, K_NO_WAIT);
			continue;
		}

		switch (rc) {
		case -ENOTCONN:
			return;
		case -EAGAIN:
		case -EBUSY:
		case -ENOMEM:
		case -ENOBUFS:
			(void)companion_bluetooth_schedule_tx(
				K_MSEC(MESHBUS_MESHCORE_COMPANION_BLUETOOTH_TX_RETRY_DELAY_MS));
			return;
		default:
			atomic_inc(&companion_bluetooth_drop_count);
			LOG_DBG("Companion Bluetooth TX drop: len=%u rc=%d",
				(unsigned int)frame.len, rc);
			(void)k_msgq_get(&companion_bluetooth_tx_msgq, &frame, K_NO_WAIT);
			break;
		}
	}
}

static void companion_bluetooth_state_listener_cb(const struct zbus_channel *chan)
{
	const struct meshbus_bluetooth_state_event *event;

	if (chan != &meshbus_bluetooth_state_chan) {
		return;
	}

	event = zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

	companion_bluetooth_set_connected(event->state == MESHBUS_BLUETOOTH_STATE_CONNECTED &&
					  meshbus_bluetooth_meshcore_companion_enabled());
}

ZBUS_LISTENER_DEFINE(meshbus_meshcore_companion_bluetooth_state_listener,
		     companion_bluetooth_state_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_bluetooth_state_chan,
		  meshbus_meshcore_companion_bluetooth_state_listener, 3);

static int meshbus_meshcore_companion_bluetooth_init(void)
{
	const struct meshcore_companion_transport transport = {
		.send = companion_bluetooth_transport_send,
		.user_data = NULL,
	};
	int rc;

	if (!companion_bluetooth_workq_started) {
		k_work_queue_start(&companion_bluetooth_workq, companion_bluetooth_workq_stack,
				   K_THREAD_STACK_SIZEOF(companion_bluetooth_workq_stack),
				   CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH_WORKQUEUE_PRIORITY,
				   &companion_bluetooth_workq_config);
		companion_bluetooth_workq_started = true;
	}

	k_work_init(&companion_bluetooth_rx_work, companion_bluetooth_rx_work_handler);
	k_work_init_delayable(&companion_bluetooth_tx_work, companion_bluetooth_tx_work_handler);
	k_msgq_purge(&companion_bluetooth_rx_msgq);
	k_msgq_purge(&companion_bluetooth_tx_msgq);
	atomic_set(&companion_bluetooth_connected, 0);
	atomic_set(&companion_bluetooth_drop_count, 0);
	atomic_set(&companion_bluetooth_rx_count, 0);
	atomic_set(&companion_bluetooth_tx_count, 0);

	companion_bluetooth_cb.notif_enabled = companion_bluetooth_notif_enabled;
	companion_bluetooth_cb.received = companion_bluetooth_received;
	rc = bt_nus_cb_register(&companion_bluetooth_cb, NULL);
	if (rc != 0) {
		return rc;
	}

	return meshcore_companion_adapter_init(&transport);
}
SYS_INIT(meshbus_meshcore_companion_bluetooth_init, APPLICATION,
	 CONFIG_MESHBUS_MESHCORE_COMPANION_BLUETOOTH_INIT_PRIORITY);
