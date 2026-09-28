/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <meshcore/meshcore.h>
#include <contact/contact.h>
#if defined(CONFIG_MBS_POWER)
#include <power/power.h>
#endif
#include <radio/radio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

#if defined(CONFIG_MBS_MESHCORE_CLIENT)
#include <channel/channel.h>
#include <message/message.h>
#endif

#include "meshcore/platform.h"
#include "meshcore/runtime.h"
#include "meshcore/types.h"
#include "meshcore_identity.h"
#include "meshcore_prvi.h"

#ifndef CONFIG_MBS_MESHCORE_LOG_LEVEL
#define CONFIG_MBS_MESHCORE_LOG_LEVEL LOG_LEVEL_INF
#endif

#define MESHCORE_RUNTIME_EVENT_BUDGET 32U
#define MESHCORE_REQUEST_WORK_BUDGET 32U
#define MESHCORE_RADIO_RX_WORK_BUDGET 16U
#define MESHCORE_COMPANION_REQ_TYPE_GET_STATUS 0x01U
#define MESHCORE_COMPANION_STATUS_RESPONSE_LEN 56U

LOG_MODULE_REGISTER(mbs_meshcore_runtime, CONFIG_MBS_MESHCORE_LOG_LEVEL);

static void meshcore_companion_status_put_u16(uint8_t *out, size_t *idx, uint16_t value)
{
	sys_put_le16(value, &out[*idx]);
	*idx += sizeof(value);
}

static void meshcore_companion_status_put_i16(uint8_t *out, size_t *idx, int16_t value)
{
	meshcore_companion_status_put_u16(out, idx, (uint16_t)value);
}

static void meshcore_companion_status_put_u32(uint8_t *out, size_t *idx, uint32_t value)
{
	sys_put_le32(value, &out[*idx]);
	*idx += sizeof(value);
}

static uint8_t meshcore_companion_status_response_build(uint8_t *out, size_t cap)
{
	uint16_t battery_mv = 0U;
	size_t idx = 0U;

	if (out == NULL || cap < MESHCORE_COMPANION_STATUS_RESPONSE_LEN) {
		return 0U;
	}

#if defined(CONFIG_MBS_POWER)
	if (mbs_power_fuel_gauge_get(&battery_mv, NULL, NULL) != 0) {
		battery_mv = 0U;
	}
#endif

	meshcore_companion_status_put_u16(out, &idx, battery_mv);
	meshcore_companion_status_put_u16(out, &idx, 0U);
	meshcore_companion_status_put_i16(out, &idx, mbs_radio_noise_floor());
	meshcore_companion_status_put_i16(out, &idx, mbs_radio_last_rssi());
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, k_uptime_get_32() / MSEC_PER_SEC);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u16(out, &idx, 0U);
	meshcore_companion_status_put_i16(out, &idx, mbs_radio_last_snr());
	meshcore_companion_status_put_u16(out, &idx, 0U);
	meshcore_companion_status_put_u16(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);
	meshcore_companion_status_put_u32(out, &idx, 0U);

	return (uint8_t)idx;
}

static void mbs_meshcore_runtime_wake(void);
static bool mbs_meshcore_request_queue_process(uint32_t budget,
						  bool *made_progress);
static void mbs_meshcore_radio_state_apply(
	const struct mbs_radio_state_event *event);
static bool mbs_meshcore_radio_is_paused(void);
static bool mbs_meshcore_radio_tx_done_process(bool *made_progress);
static bool mbs_meshcore_radio_tx_done_is_pending(void);
static bool mbs_meshcore_radio_rx_process(uint32_t budget,
				      bool *made_progress);
static int mbs_meshcore_backend_bootstrap_defaults(void);
static uint8_t mbs_meshcore_node_discover_filter_to_meshcore(uint8_t filter);

/* runtime/runtime.c */

static struct k_work_q meshcore_wq;
static const struct k_work_queue_config meshcore_wq_config = {
	.name = "meshcore_wq",
};
K_THREAD_STACK_DEFINE(meshcore_wq_stack, CONFIG_MBS_MESHCORE_WORKQ_STACK_SIZE);
static struct k_work_delayable meshcore_work;
static atomic_t meshcore_runtime_work_ready;
static atomic_t meshcore_radio_paused;
static atomic_t runtime_generation;
static void meshcore_restart_handler(struct k_work *work);
static K_WORK_DEFINE(meshcore_restart_work, meshcore_restart_handler);

static const mbs_meshcore_config *restart_candidate;
static int restart_result;
static bool restart_was_ready;

int mbs_meshcore_runtime_apply(const mbs_meshcore_config *cfg)
{
	struct k_work_sync sync;
	int rc;

	/* Teardown must never run recursively inside a protocol callback. */
	if (k_current_get() == k_work_queue_thread_get(&meshcore_wq)) {
		rc = -EWOULDBLOCK;
		goto out;
	}
	restart_candidate = cfg;
	restart_was_ready = atomic_set(&meshcore_runtime_work_ready, 0) != 0;
	rc = k_work_submit_to_queue(&meshcore_wq, &meshcore_restart_work);
	if (rc < 0) {
		atomic_set(&meshcore_runtime_work_ready, restart_was_ready);
		goto out;
	}
	(void)k_work_flush(&meshcore_restart_work, &sync);
	rc = restart_result;
out:
	mbs_meshcore_activation_complete(mbs_meshcore_runtime_is_ready() ? 0 : -EIO);
	if (mbs_meshcore_runtime_is_ready()) {
		mbs_meshcore_runtime_wake();
	}
	return rc;
}

static bool meshcore_timer_due(uint32_t now_ms, uint32_t deadline_ms)
{
	return (int32_t)(deadline_ms - now_ms) <= 0;
}

static k_timeout_t meshcore_timer_timeout(uint32_t now_ms,
					  uint32_t deadline_ms)
{
	uint32_t delay_ms;

	if (meshcore_timer_due(now_ms, deadline_ms)) {
		return K_NO_WAIT;
	}

	delay_ms = deadline_ms - now_ms;
	if (delay_ms > (uint32_t)INT_MAX) {
		delay_ms = (uint32_t)INT_MAX;
	}
	return K_MSEC(delay_ms);
}

int meshcore_platform_timer_arm(uint32_t deadline_ms)
{
	uint32_t now_ms = k_uptime_get_32();
	int rc;

	if (!atomic_get(&meshcore_runtime_work_ready) ||
	    mbs_meshcore_radio_is_paused()) {
		return 0;
	}
	rc = k_work_reschedule_for_queue(
		&meshcore_wq, &meshcore_work,
		meshcore_timer_timeout(now_ms, deadline_ms));
	return rc < 0 ? rc : 0;
}

void meshcore_platform_timer_cancel(void)
{
	(void)k_work_cancel_delayable(&meshcore_work);
}

static void mbs_meshcore_runtime_wake(void)
{
	if (!atomic_get(&meshcore_runtime_work_ready) ||
	    mbs_meshcore_radio_is_paused()) {
		return;
	}

	(void)k_work_reschedule_for_queue(&meshcore_wq, &meshcore_work,
					  K_NO_WAIT);
}

static bool mbs_meshcore_radio_is_paused(void)
{
	return atomic_get(&meshcore_radio_paused) != 0;
}

static uint8_t mbs_meshcore_node_discover_filter_to_meshcore(uint8_t filter)
{
	uint8_t out = 0U;

	if ((filter & MBS_MESHCORE_DISCOVER_FILTER_CHAT) != 0U) {
		out |= MESHCORE_NODE_DISCOVER_FILTER_CHAT;
	}
	if ((filter & MBS_MESHCORE_DISCOVER_FILTER_REPEATER) != 0U) {
		out |= MESHCORE_NODE_DISCOVER_FILTER_REPEATER;
	}
	if ((filter & MBS_MESHCORE_DISCOVER_FILTER_ROOM) != 0U) {
		out |= MESHCORE_NODE_DISCOVER_FILTER_ROOM;
	}
	if ((filter & MBS_MESHCORE_DISCOVER_FILTER_SENSOR) != 0U) {
		out |= MESHCORE_NODE_DISCOVER_FILTER_SENSOR;
	}

	return out;
}

static void mbs_meshcore_radio_state_apply(
	const struct mbs_radio_state_event *event)
{
	if (event == NULL) {
		return;
	}

	if (event->enabled) {
		if (atomic_cas(&meshcore_radio_paused, 1, 0)) {
			LOG_INF("radio enabled, resuming MeshCore runtime");
		}
		mbs_meshcore_runtime_wake();
		return;
	}

	if (atomic_cas(&meshcore_radio_paused, 0, 1)) {
		LOG_INF("radio disabled, pausing MeshCore runtime");
	}
	if (atomic_get(&meshcore_runtime_work_ready)) {
		(void)k_work_cancel_delayable(&meshcore_work);
	}
}

static void meshcore_runtime_work_handler(struct k_work *work)
{
	uint32_t passes;

	ARG_UNUSED(work);

	for (passes = 0U; passes < MESHCORE_RUNTIME_EVENT_BUDGET; passes++) {
		bool request_progress = false;
		bool request_backlog_ready;
		bool rx_progress = false;
		bool rx_backlog_ready;
		bool tx_done_progress = false;
		uint32_t now_ms = k_uptime_get_32();
		int rc;

		if (!atomic_get(&meshcore_runtime_work_ready) ||
		    mbs_meshcore_activation_pending() ||
		    mbs_meshcore_radio_is_paused()) {
			return;
		}

		(void)mbs_meshcore_radio_tx_done_process(&tx_done_progress);
		rx_backlog_ready = mbs_meshcore_radio_rx_process(
			MESHCORE_RADIO_RX_WORK_BUDGET, &rx_progress);
		request_backlog_ready =
			mbs_meshcore_request_queue_process(MESHCORE_REQUEST_WORK_BUDGET,
						       &request_progress);
		rc = meshcore_timer_fired(now_ms);
		if (rc != 0) {
			LOG_WRN("meshcore_timer_fired failed: %d", rc);
			return;
		}

		if (!request_backlog_ready && !rx_backlog_ready &&
		    !request_progress && !rx_progress && !tx_done_progress &&
		    !mbs_meshcore_radio_tx_done_is_pending()) {
			break;
		}
	}
}

static int meshcore_runtime_init(void)
{
	int rc;

	LOG_DBG("starting lib-mc backend runtime (callback model, init_prio=%d)",
		CONFIG_MBS_MESHCORE_INIT_PRIORITY);

	k_work_queue_start(&meshcore_wq, meshcore_wq_stack,
			   K_THREAD_STACK_SIZEOF(meshcore_wq_stack),
			   CONFIG_MBS_MESHCORE_WORKQ_PRIORITY, &meshcore_wq_config);

	k_work_init_delayable(&meshcore_work, meshcore_runtime_work_handler);

	/* The engine snapshots the receive identity during initialization. Prepare
	 * defaults first so a new device can receive before its first transmission.
	 */
	rc = mbs_meshcore_backend_bootstrap_defaults();
	if (rc != 0) {
		LOG_ERR("bootstrap defaults failed: %d", rc);
		return rc;
	}

	rc = meshcore_init();
	if (rc != 0) {
		LOG_ERR("meshcore_init failed: %d", rc);
		return rc;
	}

	struct mbs_radio_state_event radio_state = {0};
	if (zbus_chan_read(&mbs_radio_state_chan, &radio_state, K_NO_WAIT) == 0) {
		mbs_meshcore_radio_state_apply(&radio_state);
	}

	atomic_set(&meshcore_runtime_work_ready, 1);
	mbs_meshcore_runtime_wake();
	LOG_INF("lib-mc backend runtime started");
	return 0;
}

SYS_INIT(meshcore_runtime_init, APPLICATION, CONFIG_MBS_MESHCORE_INIT_PRIORITY);

bool mbs_meshcore_runtime_is_ready(void)
{
	return atomic_get(&meshcore_runtime_work_ready) != 0;
}

/* runtime/requests.c */

#define MESHCORE_REQUEST_QUEUE_CAP CONFIG_MBS_MESHCORE_REQUEST_QUEUE_DEPTH
#define MESHCORE_REQUEST_LARGE_QUEUE_CAP \
	CONFIG_MBS_MESHCORE_REQUEST_LARGE_QUEUE_DEPTH
#define MESHCORE_DEFAULT_TELEMETRY_PERMISSION_MASK \
	(MESHCORE_TELEM_PERM_BASE | MESHCORE_TELEM_PERM_LOCATION | \
	 MESHCORE_TELEM_PERM_ENVIRONMENT)
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
#define MESHCORE_DEFAULT_CHANNEL_NAME "Public"

#define DEFAULT_CHANNEL_SECRET_BYTES                                                  \
	{                                                                             \
		0x8B, 0x33, 0x87, 0xE9, 0xC5, 0xCD, 0xEA, 0x6A, 0xC9, 0xE5, 0xED,    \
			0xBA, 0xA1, 0x15, 0xCD, 0x72                               \
	}
#endif

enum meshcore_request_type {
	MESHCORE_REQUEST_NODE_ADVERT = 1,
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	MESHCORE_REQUEST_NODE_PEER_ADVERT,
	MESHCORE_REQUEST_MESSAGE_SEND_TO_NODE,
	MESHCORE_REQUEST_NODE_PATH_DISCOVER,
	MESHCORE_REQUEST_NODE_PATH_TRACE,
	MESHCORE_REQUEST_NODE_TELEMETRY,
	MESHCORE_REQUEST_MESSAGE_SEND_TO_CHANNEL,
#endif
	MESHCORE_REQUEST_NODE_DISCOVER,
	MESHCORE_REQUEST_NODE_TRACE,
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	MESHCORE_REQUEST_NODE_BINARY,
	MESHCORE_REQUEST_CHANNEL_DATA,
#endif
	MESHCORE_REQUEST_NODE_BINARY_RESPONSE,
	MESHCORE_REQUEST_NODE_ANON_DATA,
	MESHCORE_REQUEST_RAW_DATA,
	MESHCORE_REQUEST_CONTROL_DATA,
};

struct meshcore_request_event {
	uint8_t type;
	union {
		mbs_meshcore_advert_request_event node_advert;
		mbs_meshcore_node_discover_request_event node_discover;
		mbs_meshcore_trace_request_event node_trace;
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
		mbs_contact_share_request_event contact_share;
		struct mbs_message_send_to_node_request_event message_send_to_node;
		mbs_contact_discover_path_request_event contact_discover_path;
		mbs_contact_trace_path_request_event contact_trace_path;
		mbs_contact_telemetry_request_event contact_telemetry;
		mbs_contact_binary_request_event contact_binary;
		struct mbs_message_send_to_channel_request_event message_send_to_channel;
		struct mbs_meshcore_channel_data_send_request_event channel_data;
#endif
		struct mbs_meshcore_binary_response_send_request_event binary_response;
		struct mbs_meshcore_anon_data_send_request_event anon_data;
		struct mbs_meshcore_raw_data_send_request_event raw_data;
		struct mbs_meshcore_control_data_send_request_event control_data;
	} data;
};

enum meshcore_request_storage {
	MESHCORE_REQUEST_STORAGE_SMALL = 1,
	MESHCORE_REQUEST_STORAGE_LARGE,
};

struct meshcore_request_item_header {
	void *fifo_reserved;
	atomic_val_t generation;
	uint8_t type;
	uint8_t storage;
};

struct meshcore_request_small_item {
	struct meshcore_request_item_header header;
	union {
		mbs_meshcore_advert_request_event node_advert;
		mbs_meshcore_node_discover_request_event node_discover;
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
		mbs_contact_discover_path_request_event contact_discover_path;
		mbs_contact_trace_path_request_event contact_trace_path;
		mbs_contact_telemetry_request_event contact_telemetry;
#endif
	} data;
};

struct meshcore_request_large_item {
	struct meshcore_request_item_header header;
	union {
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
		mbs_contact_share_request_event contact_share;
		struct mbs_message_send_to_node_request_event message_send_to_node;
		struct mbs_message_send_to_channel_request_event message_send_to_channel;
		mbs_contact_binary_request_event contact_binary;
		struct mbs_meshcore_channel_data_send_request_event channel_data;
#endif
		mbs_meshcore_trace_request_event node_trace;
		struct mbs_meshcore_binary_response_send_request_event binary_response;
		struct mbs_meshcore_anon_data_send_request_event anon_data;
		struct mbs_meshcore_raw_data_send_request_event raw_data;
		struct mbs_meshcore_control_data_send_request_event control_data;
	} data;
};

BUILD_ASSERT(MESHCORE_REQUEST_LARGE_QUEUE_CAP <= MESHCORE_REQUEST_QUEUE_CAP,
	     "large request queue depth cannot exceed total request queue depth");

K_FIFO_DEFINE(meshcore_request_fifo);
K_MEM_SLAB_DEFINE_STATIC_TYPE(meshcore_request_small_slab,
			      struct meshcore_request_small_item,
			      MESHCORE_REQUEST_QUEUE_CAP);
K_MEM_SLAB_DEFINE_STATIC_TYPE(meshcore_request_large_slab,
			      struct meshcore_request_large_item,
			      MESHCORE_REQUEST_LARGE_QUEUE_CAP);
static atomic_t meshcore_request_queue_count;

static bool meshcore_request_uses_large_storage(uint8_t type)
{
	return
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	       type == MESHCORE_REQUEST_NODE_PEER_ADVERT ||
	       type == MESHCORE_REQUEST_MESSAGE_SEND_TO_NODE ||
	       type == MESHCORE_REQUEST_MESSAGE_SEND_TO_CHANNEL ||
	       type == MESHCORE_REQUEST_NODE_BINARY ||
	       type == MESHCORE_REQUEST_CHANNEL_DATA ||
#endif
	       type == MESHCORE_REQUEST_NODE_TRACE ||
	       type == MESHCORE_REQUEST_NODE_BINARY_RESPONSE ||
	       type == MESHCORE_REQUEST_NODE_ANON_DATA ||
	       type == MESHCORE_REQUEST_RAW_DATA ||
	       type == MESHCORE_REQUEST_CONTROL_DATA;
}

static int meshcore_request_queue_reserve_slot(const char *request_name, uint8_t type)
{
	atomic_val_t count;

	do {
		count = atomic_get(&meshcore_request_queue_count);
		if (count >= MESHCORE_REQUEST_QUEUE_CAP) {
			LOG_WRN("%s queue full, drop type: %u", request_name,
				(unsigned int)type);
			return -ENOBUFS;
		}
	} while (!atomic_cas(&meshcore_request_queue_count, count, count + 1));

	return 0;
}

static void meshcore_request_queue_release_slot(void)
{
	(void)atomic_dec(&meshcore_request_queue_count);
}

static void meshcore_request_item_free(struct meshcore_request_item_header *item)
{
	if (item == NULL) {
		return;
	}

	switch (item->storage) {
	case MESHCORE_REQUEST_STORAGE_SMALL:
		k_mem_slab_free(&meshcore_request_small_slab, item);
		break;
	case MESHCORE_REQUEST_STORAGE_LARGE:
		k_mem_slab_free(&meshcore_request_large_slab, item);
		break;
	default:
		break;
	}
}

static void meshcore_request_small_item_copy(
	struct meshcore_request_small_item *item,
	const struct meshcore_request_event *evt)
{
	switch (evt->type) {
	case MESHCORE_REQUEST_NODE_ADVERT:
		item->data.node_advert = evt->data.node_advert;
		break;
	case MESHCORE_REQUEST_NODE_DISCOVER:
		item->data.node_discover = evt->data.node_discover;
		break;
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	case MESHCORE_REQUEST_NODE_PATH_DISCOVER:
		item->data.contact_discover_path = evt->data.contact_discover_path;
		break;
	case MESHCORE_REQUEST_NODE_PATH_TRACE:
		item->data.contact_trace_path = evt->data.contact_trace_path;
		break;
	case MESHCORE_REQUEST_NODE_TELEMETRY:
		item->data.contact_telemetry = evt->data.contact_telemetry;
		break;
#endif
	default:
		break;
	}
}

static void meshcore_request_large_item_copy(
	struct meshcore_request_large_item *item,
	const struct meshcore_request_event *evt)
{
	switch (evt->type) {
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	case MESHCORE_REQUEST_NODE_PEER_ADVERT:
		item->data.contact_share = evt->data.contact_share;
		break;
	case MESHCORE_REQUEST_MESSAGE_SEND_TO_NODE:
		item->data.message_send_to_node = evt->data.message_send_to_node;
		break;
	case MESHCORE_REQUEST_MESSAGE_SEND_TO_CHANNEL:
		item->data.message_send_to_channel =
			evt->data.message_send_to_channel;
		break;
	case MESHCORE_REQUEST_NODE_BINARY:
		item->data.contact_binary = evt->data.contact_binary;
		break;
	case MESHCORE_REQUEST_CHANNEL_DATA:
		item->data.channel_data = evt->data.channel_data;
		break;
#endif
	case MESHCORE_REQUEST_NODE_TRACE:
		item->data.node_trace = evt->data.node_trace;
		break;
	case MESHCORE_REQUEST_NODE_BINARY_RESPONSE:
		item->data.binary_response = evt->data.binary_response;
		break;
	case MESHCORE_REQUEST_NODE_ANON_DATA:
		item->data.anon_data = evt->data.anon_data;
		break;
	case MESHCORE_REQUEST_RAW_DATA:
		item->data.raw_data = evt->data.raw_data;
		break;
	case MESHCORE_REQUEST_CONTROL_DATA:
		item->data.control_data = evt->data.control_data;
		break;
	default:
		break;
	}
}

static int meshcore_request_work_queue_append(
	const struct meshcore_request_event *evt, const char *request_name)
{
	atomic_val_t generation = atomic_get(&runtime_generation);
	const char *req_name = request_name;
	void *mem = NULL;
	int rc;

	if (evt == NULL) {
		return -EINVAL;
	}
	if (!mbs_meshcore_runtime_is_ready() || mbs_meshcore_activation_pending()) {
		return -EBUSY;
	}
	if (req_name == NULL || req_name[0] == '\0') {
		req_name = "MeshCore request";
	}

	rc = meshcore_request_queue_reserve_slot(req_name, evt->type);
	if (rc != 0) {
		return rc;
	}

	if (meshcore_request_uses_large_storage(evt->type)) {
		struct meshcore_request_large_item *item;

		rc = k_mem_slab_alloc(&meshcore_request_large_slab, &mem, K_NO_WAIT);
		if (rc != 0) {
			meshcore_request_queue_release_slot();
			LOG_WRN("%s large queue full, drop type: %u", req_name,
				(unsigned int)evt->type);
			return -ENOBUFS;
		}

		item = (struct meshcore_request_large_item *)mem;
		memset(item, 0, sizeof(*item));
		item->header.generation = generation;
		item->header.type = evt->type;
		item->header.storage = MESHCORE_REQUEST_STORAGE_LARGE;
		meshcore_request_large_item_copy(item, evt);
		k_fifo_put(&meshcore_request_fifo, item);
	} else {
		struct meshcore_request_small_item *item;

		rc = k_mem_slab_alloc(&meshcore_request_small_slab, &mem, K_NO_WAIT);
		if (rc != 0) {
			meshcore_request_queue_release_slot();
			LOG_WRN("%s small queue full, drop type: %u", req_name,
				(unsigned int)evt->type);
			return -ENOBUFS;
		}

		item = (struct meshcore_request_small_item *)mem;
		memset(item, 0, sizeof(*item));
		item->header.generation = generation;
		item->header.type = evt->type;
		item->header.storage = MESHCORE_REQUEST_STORAGE_SMALL;
		meshcore_request_small_item_copy(item, evt);
		k_fifo_put(&meshcore_request_fifo, item);
	}

	mbs_meshcore_runtime_wake();
	return 0;
}

static int meshcore_request_work_queue_accept(
	const struct zbus_channel *chan,
	const struct meshcore_request_event *evt, const char *request_name)
{
	int rc = meshcore_request_work_queue_append(evt, request_name);

	mbs_meshcore_request_acceptance_report(chan, rc);
	return rc;
}

#if defined(CONFIG_MBS_MESHCORE_CLIENT)
static int meshcore_backend_resolve_contact_public_key(
	const uint8_t *key_prefix, uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE])
{
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (key_prefix == NULL || public_key == NULL) {
		return -EINVAL;
	}

	rc = mbs_contact_find_by_prefix(key_prefix, &contact);
	if (rc != 0) {
		return rc;
	}
	if (contact.public_key.size != MESHCORE_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}

	memcpy(public_key, contact.public_key.bytes, MESHCORE_PUBLIC_KEY_SIZE);
	return 0;
}

static int meshcore_backend_resolve_contact_public_key_checked(
	const uint8_t *key_prefix,
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE], const char *request_name)
{
	int rc = meshcore_backend_resolve_contact_public_key(key_prefix, public_key);

	if (rc != 0) {
		LOG_WRN("%s contact lookup failed: %d", request_name, rc);
	}
	return rc;
}

static int meshcore_backend_build_contact_trace_path(
	const mbs_contact *contact, uint8_t *path, uint8_t *path_len,
	uint8_t *path_hash_size)
{
	uint8_t hash_size;
	uint8_t out_len;
	uint8_t trace_len;
	uint8_t hop;

	if (contact == NULL || path == NULL || path_len == NULL ||
	    path_hash_size == NULL) {
		return -EINVAL;
	}
	if (contact->public_key.size != MESHCORE_PUBLIC_KEY_SIZE ||
	    contact->out_path.size == 0U) {
		return -EINVAL;
	}

	hash_size = contact->path_hash_size;
	out_len = (uint8_t)contact->out_path.size;
	if (hash_size == 0U || hash_size > MBS_MESHCORE_PATH_HASH_SIZE_MAX ||
	    (out_len % hash_size) != 0U) {
		return -EINVAL;
	}

	trace_len = out_len;
	if ((size_t)trace_len > MBS_MESHCORE_PATH_MAX_LEN) {
		return -EINVAL;
	}
	memcpy(path, contact->out_path.bytes, out_len);

	for (hop = out_len; hop >= (uint8_t)(2U * hash_size);
	     hop = (uint8_t)(hop - hash_size)) {
		if ((size_t)trace_len + hash_size > MBS_MESHCORE_PATH_MAX_LEN) {
			return -EINVAL;
		}
		memcpy(&path[trace_len],
		       &contact->out_path.bytes[hop - (uint8_t)(2U * hash_size)],
		       hash_size);
		trace_len = (uint8_t)(trace_len + hash_size);
	}

	*path_len = trace_len;
	*path_hash_size = hash_size;
	return 0;
}

static int meshcore_backend_resolve_channel_secret(
	uint8_t channel_index, uint8_t secret[MESHCORE_CHANNEL_SECRET_MAX_LEN],
	size_t *secret_len)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	int rc;

	if (secret == NULL || secret_len == NULL) {
		return -EINVAL;
	}

	rc = mbs_channel_get(channel_index, &channel);
	if (rc != 0) {
		return rc;
	}
	if (channel.secret.size != MESHCORE_CHANNEL_SECRET_LEN_16 &&
	    channel.secret.size != MESHCORE_CHANNEL_SECRET_LEN_32) {
		return -EINVAL;
	}

	*secret_len = channel.secret.size;
	memcpy(secret, channel.secret.bytes, *secret_len);
	return 0;
}

static int meshcore_backend_resolve_channel_secret_checked(
	uint8_t channel_index, uint8_t secret[MESHCORE_CHANNEL_SECRET_MAX_LEN],
	size_t *secret_len, const char *request_name)
{
	int rc = meshcore_backend_resolve_channel_secret(channel_index, secret,
							 secret_len);

	if (rc != 0) {
		LOG_WRN("%s secret lookup failed: %d", request_name, rc);
	}
	return rc;
}

static int meshcore_backend_default_channel_slot(const uint8_t *secret, size_t secret_len,
						 size_t *slot_out, bool *exists_out)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	bool free_slot_found = false;

	if (secret == NULL || slot_out == NULL || exists_out == NULL) {
		return -EINVAL;
	}

	*exists_out = false;
	for (size_t i = 0U; i < mbs_channel_store_size(); i++) {
		int rc = mbs_channel_get(i, &channel);

		if (rc == -ENOENT) {
			if (!free_slot_found) {
				*slot_out = i;
				free_slot_found = true;
			}
			continue;
		}
		if (rc != 0) {
			continue;
		}
		if (channel.secret.size == secret_len &&
		    memcmp(channel.secret.bytes, secret, secret_len) == 0) {
			*slot_out = i;
			*exists_out = true;
			return 0;
		}
	}

	return free_slot_found ? 0 : -ENOSPC;
}

static int mbs_meshcore_backend_bootstrap_defaults(void)
{
	const uint8_t default_secret[] = DEFAULT_CHANNEL_SECRET_BYTES;
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	size_t default_channel_idx = 0U;
	bool default_channel_exists = false;
	int rc;

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		LOG_WRN("meshcore_config_get failed during bootstrap: %d", rc);
		return rc;
	}

	if (cfg.public_key.size != MESHCORE_PUBLIC_KEY_SIZE ||
	    cfg.private_key.size != MESHCORE_PRIVATE_KEY_SIZE) {
		mbs_meshcore_config_init_identity(&cfg);

		rc = mbs_meshcore_config_set(&cfg);
		if (rc != 0) {
			LOG_WRN("meshcore_config_set failed during bootstrap: %d", rc);
			return rc;
		}
		LOG_INF("Generated new local identity and applied default MeshCore config");
	}

	rc = meshcore_backend_default_channel_slot(default_secret, sizeof(default_secret),
						  &default_channel_idx, &default_channel_exists);
	/* A full user-owned table, including a single custom channel, is valid. */
	if (rc == -ENOSPC) {
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("default channel slot lookup failed: %d", rc);
		return rc;
	}
	if (default_channel_exists) {
		return 0;
	}

	rc = mbs_channel_set(default_channel_idx, default_secret, sizeof(default_secret),
				 MESHCORE_DEFAULT_CHANNEL_NAME);
	if (rc == -EADDRINUSE || rc == -EEXIST) {
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("default channel set failed: %d", rc);
		return rc;
	}

	LOG_INF("Set default Public channel idx=%u", (unsigned int)default_channel_idx);
	return 0;
}
#else
static int mbs_meshcore_backend_bootstrap_defaults(void)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int rc;

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		LOG_WRN("meshcore_config_get failed during bootstrap: %d", rc);
		return rc;
	}

	if (cfg.public_key.size == MESHCORE_PUBLIC_KEY_SIZE &&
	    cfg.private_key.size == MESHCORE_PRIVATE_KEY_SIZE) {
		return 0;
	}

	mbs_meshcore_config_init_identity(&cfg);

	rc = mbs_meshcore_config_set(&cfg);
	if (rc != 0) {
		LOG_WRN("meshcore_config_set failed during bootstrap: %d", rc);
		return rc;
	}

	LOG_INF("Generated new local identity and applied default MeshCore config");
	return 0;
}
#endif

static int meshcore_request_item_execute(
	const struct meshcore_request_item_header *item)
{
	const struct meshcore_request_small_item *small_item = NULL;
	const struct meshcore_request_large_item *large_item = NULL;
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t secret[MESHCORE_CHANNEL_SECRET_MAX_LEN];
	size_t secret_len = 0U;
	int rc;
#endif
	uint32_t request_tag;
	uint8_t request_filter;

	if (item == NULL) {
		return -EINVAL;
	}

	if (item->storage == MESHCORE_REQUEST_STORAGE_SMALL) {
		small_item = (const struct meshcore_request_small_item *)item;
	} else if (item->storage == MESHCORE_REQUEST_STORAGE_LARGE) {
		large_item = (const struct meshcore_request_large_item *)item;
	}

	switch (item->type) {
	case MESHCORE_REQUEST_NODE_ADVERT:
		if (small_item == NULL) {
			return -EINVAL;
		}
		return meshcore_node_advert_request(small_item->data.node_advert.flood);

	case MESHCORE_REQUEST_NODE_DISCOVER:
		if (small_item == NULL || small_item->data.node_discover.filter == 0U) {
			return -EINVAL;
		}
		request_filter = mbs_meshcore_node_discover_filter_to_meshcore(
			small_item->data.node_discover.filter);
		if (request_filter == 0U) {
			return -EINVAL;
		}
		request_tag = small_item->data.node_discover.tag;
		return meshcore_node_discover_request(
			request_filter,
			false, small_item->data.node_discover.since, &request_tag);

	case MESHCORE_REQUEST_NODE_TRACE:
		if (large_item == NULL) {
			return -EINVAL;
		}
		request_tag = large_item->data.node_trace.tag;
		return meshcore_node_trace_request(
			large_item->data.node_trace.path,
			large_item->data.node_trace.path_len,
			large_item->data.node_trace.path_hash_size,
			&request_tag);

#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	case MESHCORE_REQUEST_NODE_PEER_ADVERT:
		if (large_item == NULL ||
		    large_item->data.contact_share.raw_advert_len == 0U) {
			return -EINVAL;
		}
		return meshcore_node_peer_advert_request(
			large_item->data.contact_share.raw_advert,
			large_item->data.contact_share.raw_advert_len);

	case MESHCORE_REQUEST_MESSAGE_SEND_TO_NODE:
		if (large_item == NULL ||
		    large_item->data.message_send_to_node.payload_len == 0U) {
			return -EINVAL;
		}
		rc = meshcore_backend_resolve_contact_public_key_checked(
			large_item->data.message_send_to_node.key_prefix,
			public_key, "send-to-node");
		if (rc != 0) {
			return rc;
		}
		return meshcore_message_send_to_node(
			public_key, large_item->data.message_send_to_node.flood,
			large_item->data.message_send_to_node.attempt,
			large_item->data.message_send_to_node.payload,
			large_item->data.message_send_to_node.payload_len);

	case MESHCORE_REQUEST_MESSAGE_SEND_TO_CHANNEL:
		if (large_item == NULL ||
		    large_item->data.message_send_to_channel.payload_len == 0U) {
			return -EINVAL;
		}
		rc = meshcore_backend_resolve_channel_secret_checked(
			large_item->data.message_send_to_channel.channel_index,
			secret, &secret_len, "send-to-channel");
		if (rc != 0) {
			return rc;
		}
		rc = meshcore_message_send_to_channel(
			secret, secret_len,
			large_item->data.message_send_to_channel.payload,
			large_item->data.message_send_to_channel.payload_len);
		memset(secret, 0, sizeof(secret));
		return rc;
	case MESHCORE_REQUEST_NODE_PATH_DISCOVER:
		if (small_item == NULL) {
			return -EINVAL;
		}
		request_tag = small_item->data.contact_discover_path.tag;

		rc = meshcore_backend_resolve_contact_public_key_checked(
			small_item->data.contact_discover_path.key_prefix,
			public_key, "discover-path");
		if (rc != 0) {
			return rc;
		}
		return meshcore_node_discover_path_request(public_key, &request_tag);

	case MESHCORE_REQUEST_NODE_PATH_TRACE:
		if (small_item == NULL) {
			return -EINVAL;
		}
	{
		mbs_contact contact = meshbus_Contact_init_zero;
		uint8_t trace_path[MBS_MESHCORE_PATH_MAX_LEN] = {0};
		uint8_t trace_len = 0U;
		uint8_t trace_hash_size = 0U;

		request_tag = small_item->data.contact_trace_path.tag;

		rc = mbs_contact_find_by_prefix(
			small_item->data.contact_trace_path.key_prefix, &contact);
		if (rc != 0) {
			LOG_WRN("trace-path contact lookup failed: %d", rc);
			return rc;
		}
		rc = meshcore_backend_build_contact_trace_path(
			&contact, trace_path, &trace_len, &trace_hash_size);
		if (rc != 0) {
			LOG_WRN("trace-path route build failed: %d", rc);
			return rc;
		}
		rc = meshcore_node_trace_request(trace_path, trace_len,
						 trace_hash_size, &request_tag);
		if (rc == 0) {
			mbs_meshcore_contact_trace_pending_register(
				request_tag,
				small_item->data.contact_trace_path.key_prefix);
		}
		return rc;
	}

	case MESHCORE_REQUEST_NODE_TELEMETRY:
		if (small_item == NULL) {
			return -EINVAL;
		}
		request_tag = small_item->data.contact_telemetry.tag;
		LOG_DBG("MeshCore contact telemetry execute: prefix=%02x%02x%02x tag=%u",
			small_item->data.contact_telemetry.key_prefix[0],
			small_item->data.contact_telemetry.key_prefix[1],
			small_item->data.contact_telemetry.key_prefix[2],
			(unsigned int)request_tag);

		rc = meshcore_backend_resolve_contact_public_key_checked(
			small_item->data.contact_telemetry.key_prefix,
			public_key, "telemetry");
		if (rc != 0) {
			return rc;
		}
		rc = meshcore_node_telemetry_request(
			public_key, MESHCORE_DEFAULT_TELEMETRY_PERMISSION_MASK,
			&request_tag);
		LOG_DBG("MeshCore contact telemetry sent: rc=%d tag=%u perm=0x%02x",
			rc, (unsigned int)request_tag,
			(unsigned int)MESHCORE_DEFAULT_TELEMETRY_PERMISSION_MASK);
		return rc;

	case MESHCORE_REQUEST_NODE_BINARY:
		if (large_item == NULL || large_item->data.contact_binary.payload_len == 0U) {
			return -EINVAL;
		}
		rc = meshcore_backend_resolve_contact_public_key_checked(
			large_item->data.contact_binary.key_prefix, public_key, "binary-request");
		if (rc != 0) {
			return rc;
		}
		return meshcore_node_binary_request_with_tag(
			public_key, large_item->data.contact_binary.payload,
			large_item->data.contact_binary.payload_len,
			large_item->data.contact_binary.tag);

	case MESHCORE_REQUEST_CHANNEL_DATA:
		if (large_item == NULL) {
			return -EINVAL;
		}
		rc = meshcore_backend_resolve_channel_secret_checked(
			large_item->data.channel_data.channel_index, secret, &secret_len,
			"channel-data");
		if (rc != 0) {
			return rc;
		}
		rc = meshcore_channel_data_send(
			secret, secret_len, large_item->data.channel_data.path,
			large_item->data.channel_data.path_len,
			large_item->data.channel_data.data_type,
			large_item->data.channel_data.payload,
			large_item->data.channel_data.payload_len);
		memset(secret, 0, sizeof(secret));
		return rc;
#endif

	case MESHCORE_REQUEST_NODE_BINARY_RESPONSE:
		if (large_item == NULL) {
			return -EINVAL;
		}
	{
		meshcore_common_binary_request_event_t request = {0};
		size_t path_bytes = 0U;

		request.route = (meshcore_common_message_route_t)
			large_item->data.binary_response.route;
		memcpy(request.public_key, large_item->data.binary_response.public_key,
		       sizeof(request.public_key));
		memcpy(request.key_prefix, large_item->data.binary_response.public_key,
		       sizeof(request.key_prefix));
		request.tag = large_item->data.binary_response.tag;
		request.path_len = large_item->data.binary_response.path_len;
		if (mbs_meshcore_path_len_to_bytes(request.path_len, &path_bytes,
						       NULL) && path_bytes > 0U) {
			memcpy(request.path, large_item->data.binary_response.path,
			       path_bytes);
		}
		return meshcore_node_binary_response(
			&request, large_item->data.binary_response.payload,
			large_item->data.binary_response.payload_len);
	}

	case MESHCORE_REQUEST_NODE_ANON_DATA:
		if (large_item == NULL || large_item->data.anon_data.payload_len == 0U) {
			return -EINVAL;
		}
		LOG_DBG("MeshCore anon-data send: target=%02x%02x%02x%02x len=%u delay=%u",
			large_item->data.anon_data.public_key[0],
			large_item->data.anon_data.public_key[1],
			large_item->data.anon_data.public_key[2],
			large_item->data.anon_data.public_key[3],
			(unsigned int)large_item->data.anon_data.payload_len,
			(unsigned int)large_item->data.anon_data.delay_ms);
		if (large_item->data.anon_data.direct_only) {
			if (large_item->data.anon_data.has_explicit_path) {
				return meshcore_node_anon_data_send_via_path_delayed(
					large_item->data.anon_data.public_key,
					large_item->data.anon_data.payload,
					large_item->data.anon_data.payload_len,
					large_item->data.anon_data.path,
					large_item->data.anon_data.path_byte_len,
					large_item->data.anon_data.path_hash_size,
					large_item->data.anon_data.delay_ms);
			}
			return meshcore_node_anon_data_send_direct_delayed(
				large_item->data.anon_data.public_key,
				large_item->data.anon_data.payload,
				large_item->data.anon_data.payload_len,
				large_item->data.anon_data.delay_ms);
		}
		return meshcore_node_anon_data_send_delayed(
			large_item->data.anon_data.public_key,
			large_item->data.anon_data.payload,
			large_item->data.anon_data.payload_len,
			large_item->data.anon_data.delay_ms);

	case MESHCORE_REQUEST_RAW_DATA:
		if (large_item == NULL || large_item->data.raw_data.payload_len == 0U) {
			return -EINVAL;
		}
		return meshcore_raw_data_send(large_item->data.raw_data.path,
					      large_item->data.raw_data.path_len,
					      large_item->data.raw_data.payload,
					      large_item->data.raw_data.payload_len);

	case MESHCORE_REQUEST_CONTROL_DATA:
		if (large_item == NULL || large_item->data.control_data.payload_len == 0U) {
			return -EINVAL;
		}
		return meshcore_control_data_send(large_item->data.control_data.payload,
						  large_item->data.control_data.payload_len);

	default:
		return -EINVAL;
	}
}

static bool mbs_meshcore_request_queue_process(uint32_t budget, bool *made_progress)
{
	uint32_t handled = 0U;

	while (handled < MESHCORE_REQUEST_WORK_BUDGET && handled < budget) {
		struct meshcore_request_item_header *item =
			k_fifo_peek_head(&meshcore_request_fifo);
		uint8_t type;
		int rc;

		if (item == NULL) {
			break;
		}
		type = item->type;

		rc = 0;
		if (item->generation == atomic_get(&runtime_generation) &&
		    !mbs_meshcore_activation_pending()) {
			rc = meshcore_request_item_execute(item);
		}
		if (rc == -ENOBUFS) {
			break;
		}
		(void)k_fifo_get(&meshcore_request_fifo, K_NO_WAIT);
		meshcore_request_item_free(item);
		meshcore_request_queue_release_slot();
		handled++;

		if (rc != 0) {
			LOG_WRN("MeshCore request type %u failed: %d",
				(unsigned int)type, rc);
		}
	}

	if (made_progress != NULL) {
		*made_progress = handled > 0U;
	}

	return atomic_get(&meshcore_request_queue_count) > 0 && handled >= budget;
}

static void mbs_meshcore_advert_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_meshcore_advert_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_meshcore_advert_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_ADVERT;
	evt.data.node_advert = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore advert request");
}

static void mbs_meshcore_node_discover_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_meshcore_node_discover_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_meshcore_node_discover_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_DISCOVER;
	evt.data.node_discover = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore node-discover request");
}

static void mbs_meshcore_trace_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_meshcore_trace_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_meshcore_trace_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_TRACE;
	evt.data.node_trace = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore trace request");
}

#if defined(CONFIG_MBS_MESHCORE_CLIENT)
static void mbs_contact_share_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_share_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_contact_share_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_PEER_ADVERT;
	evt.data.contact_share = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Contact share request");
}

static void mbs_contact_discover_path_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_discover_path_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_contact_discover_path_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_PATH_DISCOVER;
	evt.data.contact_discover_path = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Contact discover-path request");
}

static void mbs_contact_trace_path_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_trace_path_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_contact_trace_path_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_PATH_TRACE;
	evt.data.contact_trace_path = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Contact trace-path request");
}

static void mbs_contact_request_telemetry_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_telemetry_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_contact_telemetry_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_TELEMETRY;
	evt.data.contact_telemetry = *req;
	LOG_DBG("MeshCore contact telemetry queued: prefix=%02x%02x%02x tag=%u",
		req->key_prefix[0], req->key_prefix[1], req->key_prefix[2],
		(unsigned int)req->tag);
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Contact telemetry request");
}

static void mbs_contact_binary_request_listener_cb(const struct zbus_channel *chan)
{
	const mbs_contact_binary_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const mbs_contact_binary_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_BINARY;
	evt.data.contact_binary = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Contact binary request");
}

static void mbs_message_send_to_node_request_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_message_send_to_node_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_message_send_to_node_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_MESSAGE_SEND_TO_NODE;
	evt.data.message_send_to_node = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Message send-to-node request");
}

static void mbs_message_send_to_channel_request_listener_cb(
	const struct zbus_channel *chan)
{
	const struct mbs_message_send_to_channel_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_message_send_to_channel_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_MESSAGE_SEND_TO_CHANNEL;
	evt.data.message_send_to_channel = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "Message send-to-channel request");
}

static void mbs_meshcore_channel_data_request_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_channel_data_send_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_meshcore_channel_data_send_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_CHANNEL_DATA;
	evt.data.channel_data = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore channel-data request");
}
#endif

static void mbs_meshcore_binary_response_request_listener_cb(
	const struct zbus_channel *chan)
{
	const struct mbs_meshcore_binary_response_send_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_meshcore_binary_response_send_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_NODE_BINARY_RESPONSE;
	evt.data.binary_response = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore binary response");
}

static void mbs_meshcore_binary_request_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_binary_request_event *req;
	struct meshcore_request_event evt = {0};
	int rc;

	req = (const struct mbs_meshcore_binary_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL || req->payload_len == 0U ||
	    req->payload[0] != MESHCORE_COMPANION_REQ_TYPE_GET_STATUS) {
		return;
	}

	LOG_DBG("MeshCore companion status request received: prefix=%02x%02x%02x%02x "
		"tag=%u route=%u payload_len=%u",
		req->public_key[0], req->public_key[1], req->public_key[2],
		req->public_key[3], (unsigned int)req->tag, (unsigned int)req->route,
		(unsigned int)req->payload_len);

	evt.type = MESHCORE_REQUEST_NODE_BINARY_RESPONSE;
	evt.data.binary_response.route = req->route;
	memcpy(evt.data.binary_response.public_key, req->public_key,
	       sizeof(evt.data.binary_response.public_key));
	evt.data.binary_response.tag = req->tag;
	evt.data.binary_response.path_len = req->path_len;
	memcpy(evt.data.binary_response.path, req->path,
	       sizeof(evt.data.binary_response.path));
	evt.data.binary_response.payload_len = meshcore_companion_status_response_build(
		evt.data.binary_response.payload,
		sizeof(evt.data.binary_response.payload));
	if (evt.data.binary_response.payload_len == 0U) {
		return;
	}

	rc = meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore companion status response");
	if (rc != 0) {
		LOG_WRN("MeshCore companion status response queue failed: tag=%u rc=%d",
			(unsigned int)req->tag, rc);
		return;
	}
	LOG_DBG("MeshCore companion status response queued: tag=%u len=%u",
		(unsigned int)req->tag, (unsigned int)evt.data.binary_response.payload_len);
}

static void mbs_meshcore_anon_data_request_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_anon_data_send_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_meshcore_anon_data_send_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	LOG_DBG("MeshCore anon-data request queued: target=%02x%02x%02x%02x len=%u",
		req->public_key[0], req->public_key[1], req->public_key[2],
		req->public_key[3], (unsigned int)req->payload_len);

	evt.type = MESHCORE_REQUEST_NODE_ANON_DATA;
	evt.data.anon_data = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore anon-data request");
}

static void mbs_meshcore_raw_data_request_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_meshcore_raw_data_send_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_meshcore_raw_data_send_request_event *)zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_RAW_DATA;
	evt.data.raw_data = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore raw-data request");
}

static void mbs_meshcore_control_data_request_listener_cb(
	const struct zbus_channel *chan)
{
	const struct mbs_meshcore_control_data_send_request_event *req;
	struct meshcore_request_event evt = {0};

	req = (const struct mbs_meshcore_control_data_send_request_event *)
		zbus_chan_const_msg(chan);
	if (req == NULL) {
		return;
	}

	evt.type = MESHCORE_REQUEST_CONTROL_DATA;
	evt.data.control_data = *req;
	(void)meshcore_request_work_queue_accept(
		chan, &evt, "MeshCore control-data request");
}

ZBUS_LISTENER_DEFINE(mbs_meshcore_advert_request_listener,
		     mbs_meshcore_advert_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_advert_request_chan,
		  mbs_meshcore_advert_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_node_discover_request_listener,
		     mbs_meshcore_node_discover_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_node_discover_request_chan,
		  mbs_meshcore_node_discover_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_trace_request_listener,
		     mbs_meshcore_trace_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_trace_request_chan,
		  mbs_meshcore_trace_request_listener, 0);
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
ZBUS_LISTENER_DEFINE(mbs_contact_share_request_listener,
		     mbs_contact_share_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_share_request_chan,
		  mbs_contact_share_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_message_send_to_node_request_listener,
		     mbs_message_send_to_node_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_send_to_node_request_chan,
		  mbs_message_send_to_node_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_contact_discover_path_request_listener,
		     mbs_contact_discover_path_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_discover_path_request_chan,
		  mbs_contact_discover_path_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_contact_trace_path_request_listener,
		     mbs_contact_trace_path_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_trace_path_request_chan,
		  mbs_contact_trace_path_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_contact_request_telemetry_listener,
		     mbs_contact_request_telemetry_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_telemetry_request_chan,
		  mbs_contact_request_telemetry_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_contact_binary_request_listener,
		     mbs_contact_binary_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_contact_binary_request_chan,
		  mbs_contact_binary_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_message_send_to_channel_request_listener,
		     mbs_message_send_to_channel_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_message_send_to_channel_request_chan,
		  mbs_message_send_to_channel_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_channel_data_request_listener,
		     mbs_meshcore_channel_data_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_channel_data_send_request_chan,
		  mbs_meshcore_channel_data_request_listener, 0);
#endif
ZBUS_LISTENER_DEFINE(mbs_meshcore_binary_response_request_listener,
		     mbs_meshcore_binary_response_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_binary_response_send_request_chan,
		  mbs_meshcore_binary_response_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_binary_request_listener,
		     mbs_meshcore_binary_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_binary_request_chan,
		  mbs_meshcore_binary_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_anon_data_request_listener,
		     mbs_meshcore_anon_data_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_anon_data_send_request_chan,
		  mbs_meshcore_anon_data_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_raw_data_request_listener,
		     mbs_meshcore_raw_data_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_raw_data_send_request_chan,
		  mbs_meshcore_raw_data_request_listener, 0);
ZBUS_LISTENER_DEFINE(mbs_meshcore_control_data_request_listener,
		     mbs_meshcore_control_data_request_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_control_data_send_request_chan,
		  mbs_meshcore_control_data_request_listener, 0);

/* runtime/radio.c */

static void mbs_radio_state_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_state_event *event;

	if (chan != &mbs_radio_state_chan) {
		return;
	}

	event = (const struct mbs_radio_state_event *)zbus_chan_const_msg(chan);
	mbs_meshcore_radio_state_apply(event);
}

ZBUS_LISTENER_DEFINE(mbs_radio_state_listener,
		     mbs_radio_state_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_state_chan, mbs_radio_state_listener, 0);

static atomic_t meshcore_radio_tx_done_pending;
static int meshcore_radio_tx_done_status;
static struct k_spinlock radio_tx_lock;
static bool radio_tx_outstanding;
static atomic_val_t radio_tx_generation;

bool mbs_meshcore_radio_tx_begin(void)
{
	k_spinlock_key_t key = k_spin_lock(&radio_tx_lock);
	bool accepted = !radio_tx_outstanding &&
		!mbs_meshcore_activation_pending();

	if (accepted) {
		radio_tx_outstanding = true;
		radio_tx_generation = atomic_get(&runtime_generation);
	}
	k_spin_unlock(&radio_tx_lock, key);
	return accepted;
}

void mbs_meshcore_radio_tx_abort(void)
{
	k_spinlock_key_t key = k_spin_lock(&radio_tx_lock);

	radio_tx_outstanding = false;
	k_spin_unlock(&radio_tx_lock, key);
}


#if !defined(CONFIG_MBS_MESHCORE_RADIO_RX_ZBUS_SUBSCRIBER)
struct meshcore_radio_rx_item {
	void *fifo_reserved;
	atomic_val_t generation;
	struct mbs_radio_receive_event event;
};

K_FIFO_DEFINE(meshcore_radio_rx_fifo);
K_MEM_SLAB_DEFINE_STATIC_TYPE(meshcore_radio_rx_slab,
			      struct meshcore_radio_rx_item,
			      CONFIG_MBS_MESHCORE_RADIO_RX_QUEUE_DEPTH);
static atomic_t meshcore_radio_rx_queue_count;
#endif

static bool mbs_meshcore_radio_tx_done_process(bool *made_progress)
{
	int status;

	if (!atomic_cas(&meshcore_radio_tx_done_pending, 1, 0)) {
		if (made_progress != NULL) {
			*made_progress = false;
		}
		return false;
	}

	status = meshcore_radio_tx_done_status;
	(void)meshcore_radio_tx_done(k_uptime_get_32(), status == 0);
	if (made_progress != NULL) {
		*made_progress = true;
	}
	return false;
}

static void mbs_radio_tx_done_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_tx_done_event *evt;

	evt = (const struct mbs_radio_tx_done_event *)zbus_chan_const_msg(chan);
	if (evt == NULL) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&radio_tx_lock);

	if (radio_tx_outstanding &&
	    radio_tx_generation == atomic_get(&runtime_generation) &&
	    !mbs_meshcore_activation_pending()) {
		meshcore_radio_tx_done_status = evt->status;
		atomic_set(&meshcore_radio_tx_done_pending, 1);
	}
	radio_tx_outstanding = false;
	k_spin_unlock(&radio_tx_lock, key);
	mbs_meshcore_runtime_wake();
}

ZBUS_LISTENER_DEFINE(mbs_radio_tx_done_listener,
		     mbs_radio_tx_done_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_tx_done_chan, mbs_radio_tx_done_listener, 0);

#if defined(CONFIG_MBS_MESHCORE_RADIO_RX_ZBUS_SUBSCRIBER)
ZBUS_MSG_SUBSCRIBER_DEFINE(meshcore_radio_rx_subscriber);
ZBUS_CHAN_ADD_OBS(mbs_radio_receive_chan, meshcore_radio_rx_subscriber, 0);

static void mbs_radio_receive_wake_listener_cb(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);

	mbs_meshcore_runtime_wake();
}

ZBUS_LISTENER_DEFINE(mbs_radio_receive_wake_listener,
		     mbs_radio_receive_wake_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_receive_chan,
		  mbs_radio_receive_wake_listener, 1);

static bool mbs_meshcore_radio_rx_process(uint32_t budget, bool *made_progress)
{
	const struct zbus_channel *chan = NULL;
	struct mbs_radio_receive_event evt = { 0 };
	uint32_t handled = 0U;

	while (handled < budget &&
	       zbus_sub_wait_msg(&meshcore_radio_rx_subscriber, &chan, &evt,
				 K_NO_WAIT) == 0) {
		if (chan == &mbs_radio_receive_chan && evt.len > 0U &&
		    !mbs_meshcore_activation_pending()) {
			(void)meshcore_radio_rx_inject(evt.data, evt.len, evt.rssi,
						       evt.snr, k_uptime_get_32());
		}
		handled++;
	}

	if (made_progress != NULL) {
		*made_progress = handled > 0U;
	}
	return handled >= budget;
}
#else
static void mbs_radio_receive_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_receive_event *evt;
	struct meshcore_radio_rx_item *item;
	atomic_val_t generation = atomic_get(&runtime_generation);
	void *mem = NULL;
	atomic_val_t count;
	int rc;

	evt = (const struct mbs_radio_receive_event *)zbus_chan_const_msg(chan);
	if (evt == NULL || evt->len == 0U ||
	    mbs_meshcore_activation_pending()) {
		return;
	}

	do {
		count = atomic_get(&meshcore_radio_rx_queue_count);
		if (count >= CONFIG_MBS_MESHCORE_RADIO_RX_QUEUE_DEPTH) {
			LOG_WRN("MeshCore radio RX queue full");
			return;
		}
	} while (!atomic_cas(&meshcore_radio_rx_queue_count, count, count + 1));

	rc = k_mem_slab_alloc(&meshcore_radio_rx_slab, &mem, K_NO_WAIT);
	if (rc != 0) {
		(void)atomic_dec(&meshcore_radio_rx_queue_count);
		LOG_WRN("MeshCore radio RX slab full");
		return;
	}

	item = (struct meshcore_radio_rx_item *)mem;
	memset(item, 0, sizeof(*item));
	item->generation = generation;
	item->event = *evt;
	k_fifo_put(&meshcore_radio_rx_fifo, item);
	mbs_meshcore_runtime_wake();
}

ZBUS_LISTENER_DEFINE(mbs_radio_receive_listener,
		     mbs_radio_receive_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_receive_chan, mbs_radio_receive_listener, 0);

static bool mbs_meshcore_radio_rx_process(uint32_t budget, bool *made_progress)
{
	uint32_t handled = 0U;

	while (handled < budget) {
		struct meshcore_radio_rx_item *item =
			k_fifo_get(&meshcore_radio_rx_fifo, K_NO_WAIT);

		if (item == NULL) {
			break;
		}
		if (item->event.len > 0U &&
		    item->generation == atomic_get(&runtime_generation) &&
		    !mbs_meshcore_activation_pending()) {
			(void)meshcore_radio_rx_inject(
				item->event.data, item->event.len, item->event.rssi,
				item->event.snr, k_uptime_get_32());
		}
		k_mem_slab_free(&meshcore_radio_rx_slab, item);
		(void)atomic_dec(&meshcore_radio_rx_queue_count);
		handled++;
	}

	if (made_progress != NULL) {
		*made_progress = handled > 0U;
	}
	return atomic_get(&meshcore_radio_rx_queue_count) > 0 && handled >= budget;
}
#endif

static bool mbs_meshcore_radio_tx_done_is_pending(void)
{
	return atomic_get(&meshcore_radio_tx_done_pending) != 0;
}

/* Runs after any executing protocol callback on the same owning queue. */
static void meshcore_restart_handler(struct k_work *work)
{
	int rc;
	int restore_rc;

	ARG_UNUSED(work);
	/* Exclude in-flight publishers while discarding RX and reopening admission. */
	rc = zbus_chan_claim(&mbs_radio_receive_chan, K_FOREVER);
	if (rc != 0) {
		atomic_set(&meshcore_runtime_work_ready, restart_was_ready);
		restart_result = rc;
		return;
	}
	(void)k_work_cancel_delayable(&meshcore_work);
	atomic_inc(&runtime_generation);
	atomic_clear(&meshcore_radio_tx_done_pending);
#if defined(CONFIG_MBS_MESHCORE_CLIENT)
	mbs_meshcore_ack_handoff_reset();
#endif
#if defined(CONFIG_MBS_CONTACT)
	mbs_meshcore_contact_trace_pending_clear();
#endif
	meshcore_deinit();
	while (mbs_meshcore_request_queue_process(MESHCORE_REQUEST_WORK_BUDGET, NULL)) {
	}
	while (mbs_meshcore_radio_rx_process(MESHCORE_RADIO_RX_WORK_BUDGET, NULL)) {
	}
	mbs_meshcore_config_activate(restart_candidate);
	rc = meshcore_init();
	if (rc == 0) {
		rc = mbs_meshcore_config_commit(restart_candidate, true);
	}
	if (rc == 0) {
		atomic_set(&meshcore_runtime_work_ready, 1);
		LOG_INF("MeshCore configuration applied without reboot: %u",
			(unsigned int)restart_candidate->role);
	} else {
		/* Discard partial initialization and recreate the last committed configuration. */
		meshcore_deinit();
		mbs_meshcore_config_activate(NULL);
		restore_rc = meshcore_init();
		atomic_set(&meshcore_runtime_work_ready, restore_rc == 0);
		if (restore_rc != 0) {
			LOG_ERR("MeshCore apply failed: %d; restoring runtime failed: %d", rc, restore_rc);
		} else {
			LOG_WRN("MeshCore apply failed: %d; previous configuration restored", rc);
		}
	}
	(void)zbus_chan_finish(&mbs_radio_receive_chan);
	restart_result = rc;
}
