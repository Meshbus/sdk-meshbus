/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/channel.h>
#include <zephyr/meshbus/message.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/time.h>
#if defined(CONFIG_MESHBUS_RADIO)
#include <zephyr/meshbus/radio.h>
#endif
#if defined(CONFIG_MESHBUS_NOTIFY)
#include <zephyr/meshbus/notify.h>
#endif
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "meshcore/types.h"

LOG_MODULE_REGISTER(meshbus_message, CONFIG_MESHBUS_MESSAGE_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS)
#define MESHBUS_MESSAGE_ATTEMPT_TIMEOUT_MS CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS
#else
#define MESHBUS_MESSAGE_ATTEMPT_TIMEOUT_MS 30000U
#endif

BUILD_ASSERT(CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN > 0U,
	     "CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN must be > 0");
BUILD_ASSERT(CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN <= MESHCORE_MAX_MESSAGE_TX_LEN,
	     "Meshbus message TX length must fit the MeshCore text limit");
BUILD_ASSERT(CONFIG_MESHBUS_MESSAGE_MAX_STORE_COUNT > 0U,
	     "CONFIG_MESHBUS_MESSAGE_MAX_STORE_COUNT must be > 0");
BUILD_ASSERT(CONFIG_MESHBUS_MESSAGE_PENDING_SEND_COUNT > 0U,
	     "CONFIG_MESHBUS_MESSAGE_PENDING_SEND_COUNT must be > 0");
BUILD_ASSERT(CONFIG_MESHBUS_MESSAGE_PENDING_SEND_COUNT <= (UINT8_MAX + 1U),
	     "CONFIG_MESHBUS_MESSAGE_PENDING_SEND_COUNT must be <= 256");
BUILD_ASSERT(MESHBUS_MESSAGE_TARGET_PREFIX_BYTES > 0U,
	     "MESHBUS_MESSAGE_TARGET_PREFIX_BYTES must be > 0");
BUILD_ASSERT(CONFIG_MESHBUS_CONTACT_PREFIX_BYTES <= MESHBUS_MESSAGE_TARGET_PREFIX_BYTES,
	     "CONFIG_MESHBUS_CONTACT_PREFIX_BYTES must be <= target prefix bytes");

#if defined(CONFIG_MESHBUS_NOTIFY)
#define MESSAGE_EVENT_RECV  MESHBUS_NOTIFY_MESSAGE_EVENT_RECV
#define MESSAGE_EVENT_ACK   MESHBUS_NOTIFY_MESSAGE_EVENT_ACK
#else
#define MESSAGE_EVENT_RECV  1U
#define MESSAGE_EVENT_ACK   2U
#endif

struct message_queue_entry {
	void *fifo_reserved;
	uint16_t payload_len;
	uint8_t type;
	uint8_t route;
	bool has_rx_snr;
	uint64_t timestamp;
	uint64_t sender_timestamp;
	float rx_snr;
	uint8_t target[MESHBUS_MESSAGE_TARGET_PREFIX_BYTES];
	char sender_name[MESHBUS_MESSAGE_SENDER_NAME_MAX_LEN];
	uint8_t payload[CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN];
};

struct message_pending_send_entry {
	int64_t expires_at;
	uint8_t attempt;
	bool ack_pending;
	uint64_t ack_token;
};

static struct k_spinlock message_state_lock;
static struct k_spinlock message_pending_lock;
K_FIFO_DEFINE(message_fifo);
K_MEM_SLAB_DEFINE(message_entry_slab, sizeof(struct message_queue_entry),
		  CONFIG_MESHBUS_MESSAGE_MAX_STORE_COUNT, 4);

static size_t message_pending_recv_notifies;
static uint64_t message_last_assigned_timestamp_ms;
static struct message_pending_send_entry
	pending_sends[CONFIG_MESHBUS_MESSAGE_PENDING_SEND_COUNT];
static uint32_t message_session_id = 1U;
static uint32_t message_next_seq = 1U;
static bool message_response_work_active;
static bool message_ack_response_work_active;

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */

static void message_response_work_handler(struct k_work *work);
static void message_ack_response_work_handler(struct k_work *work);
static int message_subscribe_channel(const struct zbus_channel *chan,
				     const struct zbus_observer *obs,
				     const char *name);

K_WORK_DEFINE(message_response_work, message_response_work_handler);
K_WORK_DEFINE(message_ack_response_work, message_ack_response_work_handler);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool message_payload_is_valid(const uint8_t *payload, size_t payload_len)
{
	if (payload == NULL || payload_len == 0U ||
	    payload_len > CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN) {
		return false;
	}

	return memchr(payload, 0, payload_len) == NULL;
}

static int message_channel_payload_validate(const uint8_t *payload, size_t payload_len)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	const size_t name_capacity = sizeof(cfg.name);
	size_t prefix_len;
	size_t name_len;
	int rc;

	if (payload_len > CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN) {
		return -EMSGSIZE;
	}
	if (!message_payload_is_valid(payload, payload_len)) {
		return -EINVAL;
	}

	rc = meshbus_meshcore_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}
	name_len = strnlen(cfg.name, name_capacity);
	memset(&cfg, 0, sizeof(cfg));
	if (name_len >= name_capacity) {
		return -EINVAL;
	}

	prefix_len = name_len + 2U;
	if (prefix_len >= CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN ||
	    payload_len > CONFIG_MESHBUS_MESSAGE_TX_MAX_LEN - prefix_len) {
		return -EMSGSIZE;
	}

	return 0;
}

static int message_radio_tx_ready(void)
{
#if defined(CONFIG_MESHBUS_MESSAGE_TX_TEST_ASSUME_RADIO_READY)
	return 0;
#elif defined(CONFIG_MESHBUS_RADIO)
	meshbus_radio_config cfg;
	int rc;

	rc = meshbus_radio_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}
	if (!cfg.enabled) {
		return -ENODEV;
	}
	if (cfg.receive_only) {
		return -EACCES;
	}

	return 0;
#else
	return -ENODEV;
#endif
}

static uint64_t message_monotonic_now_ms(void)
{
	uint64_t now_ms = (uint64_t)k_uptime_get();

	return now_ms == 0U ? 1U : now_ms;
}

static uint64_t message_record_timestamp_now_ms(void)
{
	uint64_t now_ms;

	if (meshbus_time_timestamp_ms_get(&now_ms) != 0 || now_ms == 0U) {
		return message_monotonic_now_ms();
	}

	return now_ms;
}

static bool message_type_is_receive(meshbus_message_type type)
{
	return type == meshbus_MessageContent_MessageType_RECEIVE_NODE ||
	       type == meshbus_MessageContent_MessageType_RECEIVE_CHANNEL;
}

static bool message_route_is_valid(meshbus_message_route route)
{
	return route == meshbus_MessageContent_MessageRoute_ROUTE_UNSPECIFIED ||
	       route == meshbus_MessageContent_MessageRoute_ROUTE_FLOOD ||
	       route == meshbus_MessageContent_MessageRoute_ROUTE_DIRECT;
}

static int message_response_event_validate(const struct meshbus_message_response_event *event,
					   bool log_error)
{
	if (event == NULL) {
		return -EINVAL;
	}

	if (!message_type_is_receive(event->type)) {
		if (log_error) {
			LOG_ERR("Invalid message type: %d", (int)event->type);
		}
		return -EINVAL;
	}

	if (!message_route_is_valid(event->route)) {
		if (log_error) {
			LOG_ERR("Invalid message route: %d", (int)event->route);
		}
		return -EINVAL;
	}

	if (event->sender_name[0] == 0) {
		if (log_error) {
			LOG_ERR("Sender name is required");
		}
		return -EINVAL;
	}

	if (!message_payload_is_valid(event->payload, event->payload_len)) {
		if (log_error) {
			LOG_ERR("Invalid payload size: %u", (unsigned int)event->payload_len);
		}
		return -EINVAL;
	}

	return 0;
}

static void message_queue_entry_set_from_response(
	struct message_queue_entry *entry,
	const struct meshbus_message_response_event *event,
	uint64_t timestamp)
{
	entry->type = (uint8_t)event->type;
	entry->route = (uint8_t)event->route;
	entry->payload_len = event->payload_len;
	entry->timestamp = timestamp;
	entry->sender_timestamp = event->sender_timestamp;
	entry->has_rx_snr = event->has_rx_snr;
	entry->rx_snr = event->has_rx_snr ? event->rx_snr : 0.0f;
	memcpy(entry->target, event->target, sizeof(entry->target));
	memcpy(entry->payload, event->payload, entry->payload_len);
	memcpy(entry->sender_name, event->sender_name, sizeof(entry->sender_name));
	entry->sender_name[sizeof(entry->sender_name) - 1U] = 0;
}

static void message_content_from_queue_entry(const struct message_queue_entry *entry,
					     meshbus_message_content *message)
{
	*message = (meshbus_message_content)meshbus_MessageContent_init_zero;
	message->type = (meshbus_message_type)entry->type;
	message->route = (meshbus_message_route)entry->route;
	message->target.size = sizeof(entry->target);
	memcpy(message->target.bytes, entry->target, message->target.size);
	memcpy(message->sender_name, entry->sender_name, sizeof(message->sender_name));
	message->sender_name[sizeof(message->sender_name) - 1U] = 0;
	message->payload.size = entry->payload_len;
	memcpy(message->payload.bytes, entry->payload, message->payload.size);
	message->timestamp = entry->timestamp;
	message->sender_timestamp = entry->sender_timestamp;
	message->has_rx_snr = entry->has_rx_snr;
	if (entry->has_rx_snr) {
		message->rx_snr = entry->rx_snr;
	}
}

static bool message_send_to_node_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_message_send_to_node_request_event *event = msg;

	if (msg == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return message_payload_is_valid(event->payload, event->payload_len);
}

static bool message_send_to_channel_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_message_send_to_channel_request_event *event = msg;

	if (msg == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return message_payload_is_valid(event->payload, event->payload_len);
}

static bool message_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_message_response_event *event = msg;

	if (msg == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return message_response_event_validate(event, false) == 0;
}

static bool message_ack_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_message_ack_response_event *event = msg;

	if (msg == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return true;
}

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

ZBUS_CHAN_DEFINE(meshbus_message_send_to_node_request_chan,
		 struct meshbus_message_send_to_node_request_event,
		 message_send_to_node_request_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_message_send_to_channel_request_chan,
		 struct meshbus_message_send_to_channel_request_event,
		 message_send_to_channel_request_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_message_response_chan, struct meshbus_message_response_event,
		 message_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_message_ack_response_chan, struct meshbus_message_ack_response_event,
		 message_ack_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Queue And Publish Helpers                                                  */
/* -------------------------------------------------------------------------- */

static uint64_t message_local_timestamp_reserve_locked(uint64_t timestamp_ms)
{
	if (timestamp_ms <= message_last_assigned_timestamp_ms) {
		timestamp_ms = message_last_assigned_timestamp_ms + 1U;
	}
	message_last_assigned_timestamp_ms = timestamp_ms;

	return timestamp_ms;
}

static void message_notify_publish(uint8_t event, uint64_t ack_token)
{
#if defined(CONFIG_MESHBUS_NOTIFY)
	meshbus_notify notify = meshbus_Notify_init_zero;
	int rc;

	notify.which_payload_variant = MESHBUS_NOTIFY_TAG_MESSAGE;
	notify.payload_variant.message.event = (meshbus_notify_message_event)event;
	if (event == MESSAGE_EVENT_ACK) {
		notify.payload_variant.message.has_ack_token = true;
		notify.payload_variant.message.ack_token = ack_token;
	}

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESSAGES_CHANGED, &notify);
	if (rc != 0) {
		LOG_WRN("Message notify publish failed: event=%u ack_token=%llu rc=%d",
			(unsigned int)event, (unsigned long long)ack_token, rc);
	}
#else
	ARG_UNUSED(event);
	ARG_UNUSED(ack_token);
#endif
}

static uint64_t message_ack_token_next_locked(void)
{
	uint64_t ack_token;

	if (message_next_seq == 0U) {
		message_session_id++;
		if (message_session_id == 0U) {
			message_session_id = 1U;
		}
		message_next_seq = 1U;
	}

	ack_token = ((uint64_t)message_session_id << 32) | (uint64_t)message_next_seq;
	message_next_seq++;
	return ack_token;
}

static void message_pending_clear_entry(struct message_pending_send_entry *entry)
{
	if (entry == NULL) {
		return;
	}

	entry->expires_at = 0;
	entry->attempt = 0U;
	entry->ack_pending = false;
	entry->ack_token = 0U;
}

static struct message_pending_send_entry *message_pending_find_by_attempt_locked(
	uint8_t attempt)
{
	for (size_t i = 0U; i < ARRAY_SIZE(pending_sends); i++) {
		if (pending_sends[i].expires_at != 0 &&
		    pending_sends[i].attempt == attempt) {
			return &pending_sends[i];
		}
	}

	return NULL;
}

static struct message_pending_send_entry *message_pending_find_free_locked(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(pending_sends); i++) {
		if (pending_sends[i].expires_at == 0) {
			return &pending_sends[i];
		}
	}

	return NULL;
}

static void message_pending_cleanup_expired_locked(int64_t now_ms)
{
	for (size_t i = 0U; i < ARRAY_SIZE(pending_sends); i++) {
		if (pending_sends[i].expires_at != 0 &&
		    !pending_sends[i].ack_pending &&
		    pending_sends[i].expires_at <= now_ms) {
			message_pending_clear_entry(&pending_sends[i]);
		}
	}
}

static int message_pending_acquire_locked(uint8_t attempt, int64_t now_ms,
					  uint64_t *ack_token_out)
{
	struct message_pending_send_entry *entry;

	if (ack_token_out == NULL) {
		return -EINVAL;
	}

	message_pending_cleanup_expired_locked(now_ms);
	if (message_pending_find_by_attempt_locked(attempt) != NULL) {
		return -EALREADY;
	}

	entry = message_pending_find_free_locked();
	if (entry == NULL) {
		return -ENOSPC;
	}

	entry->expires_at = now_ms + (int64_t)MESHBUS_MESSAGE_ATTEMPT_TIMEOUT_MS;
	entry->attempt = attempt;
	entry->ack_pending = false;
	entry->ack_token = message_ack_token_next_locked();
	*ack_token_out = entry->ack_token;
	return 0;
}

static bool message_pending_mark_ack_locked(uint8_t attempt, int64_t now_ms,
					    uint64_t *ack_token_out)
{
	struct message_pending_send_entry *entry;

	message_pending_cleanup_expired_locked(now_ms);
	entry = message_pending_find_by_attempt_locked(attempt);
	if (entry == NULL || entry->ack_pending) {
		return false;
	}

	entry->ack_pending = true;
	if (ack_token_out != NULL) {
		*ack_token_out = entry->ack_token;
	}
	return true;
}

static bool message_pending_cancel_locked(uint8_t attempt, int64_t now_ms)
{
	struct message_pending_send_entry *entry;

	message_pending_cleanup_expired_locked(now_ms);
	entry = message_pending_find_by_attempt_locked(attempt);
	if (entry == NULL) {
		return false;
	}

	message_pending_clear_entry(entry);
	return true;
}

static bool message_pending_take_ack_notify_locked(uint64_t *ack_token_out)
{
	for (size_t i = 0U; i < ARRAY_SIZE(pending_sends); i++) {
		if (pending_sends[i].expires_at != 0 && pending_sends[i].ack_pending) {
			if (ack_token_out != NULL) {
				*ack_token_out = pending_sends[i].ack_token;
			}
			message_pending_clear_entry(&pending_sends[i]);
			return true;
		}
	}

	return false;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int meshbus_message_send_to_node(const uint8_t *public_key_prefix, const uint8_t *payload,
				 size_t payload_len, bool flood, uint8_t attempt,
				 uint64_t *out_ack_token)
{
	struct meshbus_message_send_to_node_request_event event = { 0 };
	meshbus_contact peer = meshbus_Contact_init_zero;
	uint64_t ack_token = 0U;
	k_spinlock_key_t key;
	int64_t now_ms;
	int rc;

	if (out_ack_token != NULL) {
		*out_ack_token = 0U;
	}

	if (public_key_prefix == NULL ||
	    !message_payload_is_valid(payload, payload_len)) {
		return -EINVAL;
	}
	rc = message_radio_tx_ready();
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_contact_find_by_prefix(public_key_prefix, &peer);
	if (rc != 0) {
		LOG_DBG("Node send request rejected: peer lookup failed rc=%d", rc);
		return rc;
	}

	now_ms = (int64_t)message_monotonic_now_ms();
	key = k_spin_lock(&message_pending_lock);
	rc = message_pending_acquire_locked(attempt, now_ms, &ack_token);
	k_spin_unlock(&message_pending_lock, key);
	if (rc != 0) {
		return rc;
	}

	event.attempt = attempt;
	event.flood = flood;
	event.payload_len = (uint16_t)payload_len;
	memcpy(event.key_prefix, public_key_prefix, sizeof(event.key_prefix));
	memcpy(event.payload, payload, payload_len);

	LOG_DBG("Queueing node send request: peer=%s attempt=%u ack_token=%llu payload_len=%u flood=%u",
		peer.name, (unsigned int)attempt, (unsigned long long)ack_token,
		(unsigned int)payload_len, flood ? 1U : 0U);
	rc = zbus_chan_pub(&meshbus_message_send_to_node_request_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		key = k_spin_lock(&message_pending_lock);
		(void)message_pending_cancel_locked(attempt, now_ms);
		k_spin_unlock(&message_pending_lock, key);
		LOG_WRN("Failed to publish node send-message request: rc=%d", rc);
		return rc;
	}

	if (out_ack_token != NULL) {
		*out_ack_token = ack_token;
	}

	LOG_DBG("Node send request published: peer=%s attempt=%u ack_token=%llu",
		peer.name, (unsigned int)event.attempt, (unsigned long long)ack_token);
	return 0;
}

int meshbus_message_send_to_channel(size_t channel_index, const uint8_t *payload,
				    size_t payload_len)
{
	struct meshbus_message_send_to_channel_request_event event = { 0 };
	meshbus_channel channel = meshbus_Channel_init_zero;
	int rc;

	rc = message_channel_payload_validate(payload, payload_len);
	if (rc != 0) {
		return rc;
	}
	if (channel_index > UINT8_MAX) {
		return -EINVAL;
	}
	rc = message_radio_tx_ready();
	if (rc != 0) {
		return rc;
	}

	rc = meshbus_channel_get(channel_index, &channel);
	if (rc != 0) {
		LOG_DBG("Channel send request rejected: index=%u rc=%d",
			(unsigned int)channel_index, rc);
		return rc;
	}

	event.channel_index = (uint8_t)channel_index;
	event.payload_len = (uint16_t)payload_len;
	memcpy(event.payload, payload, payload_len);

	rc = zbus_chan_pub(&meshbus_message_send_to_channel_request_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_WRN("Failed to publish channel send-message request: index=%u rc=%d",
			(unsigned int)event.channel_index, rc);
		return rc;
	}

	LOG_DBG("Channel send request published: index=%u name=%s payload_len=%u",
		(unsigned int)event.channel_index, channel.name,
		(unsigned int)payload_len);
	return 0;
}

static int message_queue_push_response(
	const struct meshbus_message_response_event *event,
	bool *evicted_out,
	bool *submit_work_out)
{
	struct message_queue_entry *entry = NULL;
	struct message_queue_entry *evicted = NULL;
	k_spinlock_key_t key;
	uint64_t timestamp = 0U;
	int rc = 0;
	bool evicted_oldest = false;

	if (evicted_out != NULL) {
		*evicted_out = false;
	}
	if (submit_work_out != NULL) {
		*submit_work_out = false;
	}

	rc = message_response_event_validate(event, true);
	if (rc != 0) {
		return rc;
	}

	timestamp = message_record_timestamp_now_ms();
	rc = k_mem_slab_alloc(&message_entry_slab, (void **)&entry, K_NO_WAIT);
	if (rc != 0) {
		evicted = (struct message_queue_entry *)k_fifo_get(&message_fifo, K_NO_WAIT);
		if (evicted != NULL) {
			k_mem_slab_free(&message_entry_slab, (void *)evicted);
			evicted_oldest = true;
			rc = k_mem_slab_alloc(&message_entry_slab, (void **)&entry, K_NO_WAIT);
		} else {
			rc = k_mem_slab_alloc(&message_entry_slab, (void **)&entry, K_NO_WAIT);
		}
	}
	if (rc == 0 && entry != NULL) {
		key = k_spin_lock(&message_state_lock);
		timestamp = message_local_timestamp_reserve_locked(timestamp);
		k_spin_unlock(&message_state_lock, key);
		message_queue_entry_set_from_response(entry, event, timestamp);
		k_fifo_put(&message_fifo, entry);

		key = k_spin_lock(&message_state_lock);
		message_pending_recv_notifies++;
		if (!message_response_work_active) {
			message_response_work_active = true;
			if (submit_work_out != NULL) {
				*submit_work_out = true;
			}
		}
		k_spin_unlock(&message_state_lock, key);
	}

	if (evicted_oldest) {
		if (evicted_out != NULL) {
			*evicted_out = true;
		}
		LOG_WRN("Message FIFO full (%u), dropped oldest message",
			(unsigned int)CONFIG_MESHBUS_MESSAGE_MAX_STORE_COUNT);
	}

	return rc;
}

int meshbus_message_next(meshbus_message_content *message)
{
	struct message_queue_entry *entry;

	if (message == NULL) {
		return -EINVAL;
	}

	entry = (struct message_queue_entry *)k_fifo_get(&message_fifo, K_NO_WAIT);
	if (entry == NULL) {
		*message = (meshbus_message_content)meshbus_MessageContent_init_zero;
		return -ENOENT;
	}

	message_content_from_queue_entry(entry, message);
	k_mem_slab_free(&message_entry_slab, (void *)entry);

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Callbacks And Work                                                         */
/* -------------------------------------------------------------------------- */

static void meshbus_message_response_listener_cb(const struct zbus_channel *chan)
{
	const struct meshbus_message_response_event *event = zbus_chan_const_msg(chan);
	bool evicted_oldest = false;
	bool submit_work = false;
	int rc;

	if (chan != &meshbus_message_response_chan || event == NULL) {
		return;
	}

	rc = message_queue_push_response(event, &evicted_oldest, &submit_work);
	if (rc != 0) {
		LOG_WRN("Message response enqueue failed: rc=%d", rc);
		return;
	}

	LOG_DBG("Queued RX message: type=%d sender=%s payload_len=%u sender_ts=%llu",
		(int)event->type, event->sender_name, (unsigned int)event->payload_len,
		(unsigned long long)event->sender_timestamp);

	if (evicted_oldest) {
		LOG_DBG("Message queue overflow has no dedicated notify in current proto");
	}

	if (submit_work) {
		rc = k_work_submit(&message_response_work);
		if (rc < 0 && rc != -EBUSY) {
			k_spinlock_key_t key = k_spin_lock(&message_state_lock);

			message_response_work_active = false;
			k_spin_unlock(&message_state_lock, key);
			LOG_WRN("Schedule message response work failed: rc=%d", rc);
		}
	}
}

static void message_response_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (;;) {
		size_t notify_count;

		k_spinlock_key_t key = k_spin_lock(&message_state_lock);

		notify_count = message_pending_recv_notifies;
		message_pending_recv_notifies = 0U;
		if (notify_count == 0U) {
			message_response_work_active = false;
			k_spin_unlock(&message_state_lock, key);
			return;
		}
		k_spin_unlock(&message_state_lock, key);

		while (notify_count > 0U) {
			message_notify_publish(MESSAGE_EVENT_RECV, 0U);
			notify_count--;
		}
	}
}

static void meshbus_message_ack_response_listener_cb(const struct zbus_channel *chan)
{
	const struct meshbus_message_ack_response_event *event =
		zbus_chan_const_msg(chan);
	bool submit_work = false;
	bool ack_pending = false;
	uint64_t ack_token = 0U;
	k_spinlock_key_t key;
	int64_t now_ms;
	int rc;

	if (chan != &meshbus_message_ack_response_chan || event == NULL) {
		return;
	}

	now_ms = (int64_t)message_monotonic_now_ms();
	key = k_spin_lock(&message_pending_lock);
	ack_pending = message_pending_mark_ack_locked(event->attempt, now_ms, &ack_token);
	if (ack_pending && !message_ack_response_work_active) {
		message_ack_response_work_active = true;
		submit_work = true;
	}
	k_spin_unlock(&message_pending_lock, key);

	if (!ack_pending) {
		LOG_DBG("Ignoring ACK for inactive attempt=%u", (unsigned int)event->attempt);
		return;
	}

	LOG_DBG("Message ACK matched: attempt=%u ack_token=%llu",
		(unsigned int)event->attempt, (unsigned long long)ack_token);

	if (submit_work) {
		rc = k_work_submit(&message_ack_response_work);
		if (rc < 0 && rc != -EBUSY) {
			key = k_spin_lock(&message_pending_lock);
			if (message_ack_response_work_active) {
				message_ack_response_work_active = false;
			}
			k_spin_unlock(&message_pending_lock, key);
			LOG_WRN("Schedule message ACK work failed: rc=%d", rc);
		}
	}
}

static void message_ack_response_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (;;) {
		uint64_t ack_token = 0U;
		k_spinlock_key_t key;
		bool has_ack;

		key = k_spin_lock(&message_pending_lock);
		has_ack = message_pending_take_ack_notify_locked(&ack_token);
		if (!has_ack) {
			message_ack_response_work_active = false;
			k_spin_unlock(&message_pending_lock, key);
			return;
		}
		k_spin_unlock(&message_pending_lock, key);

		message_notify_publish(MESSAGE_EVENT_ACK, ack_token);
	}
}

ZBUS_LISTENER_DEFINE(meshbus_message_response_listener, meshbus_message_response_listener_cb);
ZBUS_LISTENER_DEFINE(meshbus_message_ack_response_listener,
		     meshbus_message_ack_response_listener_cb);

static int message_subscribe_channel(const struct zbus_channel *chan,
				     const struct zbus_observer *obs,
				     const char *name)
{
	int rc;

	rc = zbus_chan_add_obs(chan, obs, K_NO_WAIT);
	if (rc == 0 || rc == -EALREADY || rc == -EEXIST) {
		LOG_DBG("Subscribed message observer: %s", name);
		return 0;
	}

	LOG_WRN("Message channel subscribe failed: %s rc=%d", name, rc);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int meshbus_message_init(void)
{
	message_session_id = k_cycle_get_32() ^ k_uptime_get_32();
	if (message_session_id == 0U) {
		message_session_id = 1U;
	}

	(void)message_subscribe_channel(&meshbus_message_response_chan,
					 &meshbus_message_response_listener,
					 "response");
	(void)message_subscribe_channel(&meshbus_message_ack_response_chan,
					 &meshbus_message_ack_response_listener,
					 "ack_response");

	LOG_INF("Message service ready: store_count=%u timeout_ms=%u session_id=%u",
		(unsigned int)CONFIG_MESHBUS_MESSAGE_MAX_STORE_COUNT,
		(unsigned int)MESHBUS_MESSAGE_ATTEMPT_TIMEOUT_MS,
		(unsigned int)message_session_id);

	return 0;
}

SYS_INIT(meshbus_message_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
