/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#if defined(CONFIG_MESHBUS_CONTACT)
#include <zephyr/meshbus/contact.h>
#endif
#include <zephyr/meshbus/meshcore.h>
#if defined(CONFIG_MESHBUS_NOTIFY)
#include <zephyr/meshbus/notify.h>
#endif
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "meshcore_prvi.h"

struct meshcore_request_acceptance_context {
	k_tid_t owner;
	const struct zbus_channel *chan;
	int result;
	bool reported;
};

#ifdef CONFIG_MESHBUS_MESHCORE_STATS
STATS_SECT_DECL(meshbus_meshcore_stats) meshbus_meshcore_stats;
static struct k_spinlock meshcore_request_stats_lock;
#endif

static K_MUTEX_DEFINE(meshcore_request_submit_mutex);
static struct k_spinlock meshcore_request_acceptance_lock;
static struct meshcore_request_acceptance_context meshcore_request_acceptance;

static void meshcore_request_stats_record(int result)
{
#ifdef CONFIG_MESHBUS_MESHCORE_STATS
	k_spinlock_key_t key = k_spin_lock(&meshcore_request_stats_lock);

	if (result == 0) {
		STATS_INC(meshbus_meshcore_stats, requests_accepted);
	} else if (result == -ENOBUFS) {
		STATS_INC(meshbus_meshcore_stats, requests_queue_full);
	} else if (result == -ENODEV) {
		STATS_INC(meshbus_meshcore_stats, requests_no_consumer);
	} else {
		STATS_INC(meshbus_meshcore_stats, requests_submit_errors);
	}

	k_spin_unlock(&meshcore_request_stats_lock, key);
#else
	ARG_UNUSED(result);
#endif
}

void meshbus_meshcore_request_acceptance_report(
	const struct zbus_channel *chan, int result)
{
	k_spinlock_key_t key;

	meshcore_request_stats_record(result);

	key = k_spin_lock(&meshcore_request_acceptance_lock);
	if (meshcore_request_acceptance.owner == k_current_get() &&
	    meshcore_request_acceptance.chan == chan) {
		meshcore_request_acceptance.result = result;
		meshcore_request_acceptance.reported = true;
	}
	k_spin_unlock(&meshcore_request_acceptance_lock, key);
}

int meshbus_meshcore_request_publish_accepted(
	const struct zbus_channel *chan, const void *msg)
{
	k_spinlock_key_t key;
	bool reported;
	int result;
	int rc;

	if (chan == NULL || msg == NULL) {
		meshcore_request_stats_record(-EINVAL);
		return -EINVAL;
	}
	if (k_is_in_isr()) {
		meshcore_request_stats_record(-EWOULDBLOCK);
		return -EWOULDBLOCK;
	}

	k_mutex_lock(&meshcore_request_submit_mutex, K_FOREVER);
	key = k_spin_lock(&meshcore_request_acceptance_lock);
	if (meshcore_request_acceptance.owner == k_current_get()) {
		k_spin_unlock(&meshcore_request_acceptance_lock, key);
		k_mutex_unlock(&meshcore_request_submit_mutex);
		meshcore_request_stats_record(-EDEADLK);
		return -EDEADLK;
	}
	meshcore_request_acceptance.owner = k_current_get();
	meshcore_request_acceptance.chan = chan;
	meshcore_request_acceptance.result = -ENODEV;
	meshcore_request_acceptance.reported = false;
	k_spin_unlock(&meshcore_request_acceptance_lock, key);

	rc = zbus_chan_pub(chan, msg, K_NO_WAIT);

	key = k_spin_lock(&meshcore_request_acceptance_lock);
	reported = meshcore_request_acceptance.reported;
	result = meshcore_request_acceptance.result;
	meshcore_request_acceptance =
		(struct meshcore_request_acceptance_context){0};
	k_spin_unlock(&meshcore_request_acceptance_lock, key);

	if (!reported) {
		result = rc != 0 ? rc : -ENODEV;
		meshcore_request_stats_record(result);
	}

	k_mutex_unlock(&meshcore_request_submit_mutex);
	return result;
}

static bool channel_data_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_channel_data_send_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->payload_len > sizeof(event->payload)) {
		return false;
	}

	return meshbus_meshcore_path_valid(event->path, event->path_len, true);
}

static bool channel_data_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_channel_data_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->payload_len > sizeof(event->payload)) {
		return false;
	}

	return meshbus_meshcore_path_valid(event->path, event->path_len, true);
}

static bool raw_data_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_raw_data_send_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) || event->payload_len == 0U ||
	    event->payload_len > sizeof(event->payload)) {
		return false;
	}

	return meshbus_meshcore_path_valid(event->path, event->path_len, false);
}

static bool raw_data_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_raw_data_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) || event->payload_len == 0U ||
	    event->payload_len > sizeof(event->payload)) {
		return false;
	}

	return meshbus_meshcore_path_valid(event->path, event->path_len, false);
}

static bool meshcore_route_valid(uint8_t route)
{
	return route == MESHBUS_MESHCORE_ROUTE_UNSPECIFIED ||
	       route == MESHBUS_MESHCORE_ROUTE_FLOOD ||
	       route == MESHBUS_MESHCORE_ROUTE_DIRECT;
}

static bool binary_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_binary_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) ||
	    !meshcore_route_valid(event->route) || event->tag == 0U ||
	    event->payload_len == 0U || event->payload_len > sizeof(event->payload)) {
		return false;
	}
	if (event->route == MESHBUS_MESHCORE_ROUTE_FLOOD) {
		return meshbus_meshcore_path_valid(event->path, event->path_len, false);
	}

	return true;
}

static bool binary_response_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_binary_response_send_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) ||
	    !meshcore_route_valid(event->route) || event->tag == 0U ||
	    event->payload_len > sizeof(event->payload)) {
		return false;
	}
	if (event->route == MESHBUS_MESHCORE_ROUTE_FLOOD) {
		return meshbus_meshcore_path_valid(event->path, event->path_len, false);
	}

	return true;
}

static bool anon_data_explicit_path_valid(uint8_t path_byte_len,
					  uint8_t path_hash_size)
{
	return path_hash_size > 0U && path_hash_size <= 3U &&
	       path_byte_len <= MESHBUS_MESHCORE_PATH_MAX_LEN &&
	       path_byte_len % path_hash_size == 0U &&
	       path_byte_len / path_hash_size <= 63U;
}

static bool anon_data_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_anon_data_send_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) ||
	    event->payload_len == 0U || event->payload_len > sizeof(event->payload)) {
		return false;
	}
	if (!event->has_explicit_path) {
		return event->path_byte_len == 0U && event->path_hash_size == 0U;
	}

	return event->direct_only && anon_data_explicit_path_valid(
					     event->path_byte_len,
					     event->path_hash_size);
}

static bool anon_data_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_anon_data_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) ||
	    !meshcore_route_valid(event->route) || event->payload_len == 0U ||
	    event->payload_len > sizeof(event->payload)) {
		return false;
	}
	if (event->route == MESHBUS_MESHCORE_ROUTE_FLOOD) {
		return meshbus_meshcore_path_valid(event->path, event->path_len, false);
	}

	return true;
}

static bool control_data_request_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_control_data_send_request_event *event = msg;

	return event != NULL && msg_size == sizeof(*event) &&
	       event->payload_len > 0U && event->payload_len <= sizeof(event->payload) &&
	       (event->payload[0] & 0x80U) != 0U;
}

static bool control_data_response_validator(const void *msg, size_t msg_size)
{
	const struct meshbus_meshcore_control_data_response_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event) || event->payload_len == 0U ||
	    event->payload_len > sizeof(event->payload)) {
		return false;
	}

	return meshbus_meshcore_path_valid(event->path, event->path_len, true);
}

#if defined(CONFIG_MESHBUS_CONTACT)
struct meshbus_meshcore_contact_trace_pending {
	bool active;
	uint32_t tag;
	int64_t deadline_ms;
	uint8_t key_prefix[MESHBUS_CONTACT_PREFIX_BYTES];
};

static struct meshbus_meshcore_contact_trace_pending contact_trace_pending;
static K_MUTEX_DEFINE(contact_trace_pending_mutex);

void meshbus_meshcore_contact_trace_pending_clear(void)
{
	k_mutex_lock(&contact_trace_pending_mutex, K_FOREVER);
	contact_trace_pending.active = false;
	k_mutex_unlock(&contact_trace_pending_mutex);
}

void meshbus_meshcore_contact_trace_pending_register(uint32_t tag,
						     const uint8_t *key_prefix)
{
	if (tag == 0U || key_prefix == NULL) {
		return;
	}

	k_mutex_lock(&contact_trace_pending_mutex, K_FOREVER);
	contact_trace_pending.active = true;
	contact_trace_pending.tag = tag;
	contact_trace_pending.deadline_ms =
		k_uptime_get() + (int64_t)CONFIG_MESHBUS_CONTACT_REQUEST_TIMEOUT_MS;
	memcpy(contact_trace_pending.key_prefix, key_prefix,
	       sizeof(contact_trace_pending.key_prefix));
	k_mutex_unlock(&contact_trace_pending_mutex);
}

bool meshbus_meshcore_contact_trace_response_claim(
	const meshbus_meshcore_trace_response_event *event)
{
	meshbus_contact_response_trace_path_event contact_event = {0};

	if (event == NULL) {
		return false;
	}

	k_mutex_lock(&contact_trace_pending_mutex, K_FOREVER);
	if (!contact_trace_pending.active) {
		k_mutex_unlock(&contact_trace_pending_mutex);
		return false;
	}
	if (contact_trace_pending.tag != event->tag) {
		if (k_uptime_get() > contact_trace_pending.deadline_ms) {
			contact_trace_pending =
				(struct meshbus_meshcore_contact_trace_pending){0};
		}
		k_mutex_unlock(&contact_trace_pending_mutex);
		return false;
	}
	if (k_uptime_get() > contact_trace_pending.deadline_ms) {
		contact_trace_pending =
			(struct meshbus_meshcore_contact_trace_pending){0};
		k_mutex_unlock(&contact_trace_pending_mutex);
		return true;
	}

	memcpy(contact_event.key_prefix, contact_trace_pending.key_prefix,
	       sizeof(contact_event.key_prefix));
	contact_trace_pending = (struct meshbus_meshcore_contact_trace_pending){0};
	k_mutex_unlock(&contact_trace_pending_mutex);

	contact_event.timestamp = event->timestamp;
	contact_event.tag = event->tag;
	contact_event.state = event->state;
	contact_event.has_response_snr = event->has_response_snr;
	contact_event.response_snr = event->response_snr;
	contact_event.out_path_snr_count =
		MIN(event->out_path_snr_count,
		    (uint8_t)ARRAY_SIZE(contact_event.out_path_snr));
	contact_event.return_path_snr_count =
		MIN(event->return_path_snr_count,
		    (uint8_t)ARRAY_SIZE(contact_event.return_path_snr));
	if (contact_event.out_path_snr_count > 0U) {
		memcpy(contact_event.out_path_snr, event->out_path_snr,
		       contact_event.out_path_snr_count);
	}
	if (contact_event.return_path_snr_count > 0U) {
		memcpy(contact_event.return_path_snr, event->return_path_snr,
		       contact_event.return_path_snr_count);
	}

	(void)zbus_chan_pub(&meshbus_contact_trace_path_response_chan,
			    &contact_event, K_NO_WAIT);
	return true;
}
#endif

#if defined(CONFIG_MESHBUS_NOTIFY)
static void meshbus_meshcore_trace_notify_copy_snr(int32_t *dst,
						   pb_size_t *dst_count,
						   const int8_t *src,
						   uint8_t src_count)
{
	size_t n = 0U;

	if (dst == NULL || dst_count == NULL) {
		return;
	}
	if (src != NULL) {
		n = MIN((size_t)src_count, (size_t)MESHBUS_MESHCORE_PATH_MAX_LEN);
	}

	for (size_t i = 0; i < n; i++) {
		dst[i] = (int32_t)src[i] * 4;
	}
	*dst_count = (pb_size_t)n;
}

static void meshbus_meshcore_trace_response_publish_notify(
	const meshbus_meshcore_trace_response_event *event)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	int rc;

	if (event == NULL) {
		return;
	}

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_MESHCORE_TRACE;
	payload.payload_variant.meshcore_trace.tag = event->tag;
	payload.payload_variant.meshcore_trace.state = event->state;
	meshbus_meshcore_trace_notify_copy_snr(
		payload.payload_variant.meshcore_trace.out_path_snr,
		&payload.payload_variant.meshcore_trace.out_path_snr_count,
		event->out_path_snr, event->out_path_snr_count);
	meshbus_meshcore_trace_notify_copy_snr(
		payload.payload_variant.meshcore_trace.return_path_snr,
		&payload.payload_variant.meshcore_trace.return_path_snr_count,
		event->return_path_snr, event->return_path_snr_count);

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_MESHCORE_TRACE, &payload);
	if (rc != 0) {
		/* Notify is best effort; callers still observe the response channel. */
		return;
	}
}
#endif

static void meshbus_meshcore_trace_response_listener_cb(const struct zbus_channel *chan)
{
	const meshbus_meshcore_trace_response_event *event;

	if (chan != &meshbus_meshcore_trace_response_chan) {
		return;
	}

	event = (const meshbus_meshcore_trace_response_event *)zbus_chan_const_msg(chan);
	if (event == NULL) {
		return;
	}

#if defined(CONFIG_MESHBUS_CONTACT)
	if (meshbus_meshcore_contact_trace_response_claim(event)) {
		return;
	}
#endif

#if defined(CONFIG_MESHBUS_NOTIFY)
	meshbus_meshcore_trace_response_publish_notify(event);
#endif
}

ZBUS_LISTENER_DEFINE(meshbus_meshcore_trace_response_listener,
		     meshbus_meshcore_trace_response_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_meshcore_trace_response_chan,
		  meshbus_meshcore_trace_response_listener, 0);

ZBUS_CHAN_DEFINE(meshbus_meshcore_channel_data_send_request_chan,
		 struct meshbus_meshcore_channel_data_send_request_event,
		 channel_data_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_channel_data_response_chan,
		 struct meshbus_meshcore_channel_data_response_event,
		 channel_data_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_raw_data_send_request_chan,
		 struct meshbus_meshcore_raw_data_send_request_event,
		 raw_data_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_raw_data_response_chan,
		 struct meshbus_meshcore_raw_data_response_event,
		 raw_data_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_binary_request_chan,
		 struct meshbus_meshcore_binary_request_event,
		 binary_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_binary_response_send_request_chan,
		 struct meshbus_meshcore_binary_response_send_request_event,
		 binary_response_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_anon_data_send_request_chan,
		 struct meshbus_meshcore_anon_data_send_request_event,
		 anon_data_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_anon_data_response_chan,
		 struct meshbus_meshcore_anon_data_response_event,
		 anon_data_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_control_data_send_request_chan,
		 struct meshbus_meshcore_control_data_send_request_event,
		 control_data_request_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(meshbus_meshcore_control_data_response_chan,
		 struct meshbus_meshcore_control_data_response_event,
		 control_data_response_validator, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

int meshbus_meshcore_channel_data_send(size_t channel_index,
				       const uint8_t *path, uint8_t path_len,
				       uint16_t data_type,
				       const uint8_t *payload,
				       size_t payload_len)
{
	struct meshbus_meshcore_channel_data_send_request_event event = { 0 };
	size_t path_bytes = 0U;

	if (channel_index > UINT8_MAX || (payload == NULL && payload_len > 0U) ||
	    payload_len > sizeof(event.payload) ||
	    !meshbus_meshcore_path_valid(path, path_len, true)) {
		return -EINVAL;
	}

	event.channel_index = (uint8_t)channel_index;
	event.path_len = path_len;
	event.data_type = data_type;
	if (path_len != MESHBUS_MESHCORE_OUT_PATH_UNKNOWN &&
	    meshbus_meshcore_path_len_to_bytes(path_len, &path_bytes, NULL) &&
	    path_bytes > 0U) {
		memcpy(event.path, path, path_bytes);
	}
	event.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		memcpy(event.payload, payload, payload_len);
	}

	return meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_channel_data_send_request_chan, &event);
}

int meshbus_meshcore_raw_data_send(const uint8_t *path, uint8_t path_len,
				   const uint8_t *payload, size_t payload_len)
{
	struct meshbus_meshcore_raw_data_send_request_event event = { 0 };
	size_t path_bytes = 0U;

	if (payload == NULL || payload_len == 0U || payload_len > sizeof(event.payload) ||
	    !meshbus_meshcore_path_valid(path, path_len, false)) {
		return -EINVAL;
	}

	event.path_len = path_len;
	if (meshbus_meshcore_path_len_to_bytes(path_len, &path_bytes, NULL) &&
	    path_bytes > 0U) {
		memcpy(event.path, path, path_bytes);
	}
	event.payload_len = (uint8_t)payload_len;
	memcpy(event.payload, payload, payload_len);

	return meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_raw_data_send_request_chan, &event);
}

int meshbus_meshcore_control_data_send(const uint8_t *payload,
				       size_t payload_len)
{
	struct meshbus_meshcore_control_data_send_request_event event = { 0 };

	if (payload == NULL || payload_len == 0U || payload_len > sizeof(event.payload) ||
	    (payload[0] & 0x80U) == 0U) {
		return -EINVAL;
	}

	event.payload_len = (uint8_t)payload_len;
	memcpy(event.payload, payload, payload_len);

	return meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_control_data_send_request_chan, &event);
}

int meshbus_meshcore_binary_response_send(
	const struct meshbus_meshcore_binary_response_send_request_event *request)
{
	if (!binary_response_request_validator(request, sizeof(*request))) {
		return -EINVAL;
	}

	return meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_binary_response_send_request_chan, request);
}

static int meshbus_meshcore_anon_data_send_internal(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, bool direct_only,
	bool has_explicit_path, const uint8_t *path, uint8_t path_byte_len,
	uint8_t path_hash_size, uint32_t delay_ms)
{
	struct meshbus_meshcore_anon_data_send_request_event event = {0};

	if (public_key == NULL || payload == NULL || payload_len == 0U ||
	    payload_len > sizeof(event.payload)) {
		return -EINVAL;
	}
	if (has_explicit_path &&
	    (!anon_data_explicit_path_valid(path_byte_len, path_hash_size) ||
	     (path_byte_len > 0U && path == NULL))) {
		return -EINVAL;
	}

	memcpy(event.public_key, public_key, sizeof(event.public_key));
	event.payload_len = (uint8_t)payload_len;
	memcpy(event.payload, payload, payload_len);
	event.delay_ms = delay_ms;
	event.direct_only = direct_only;
	event.has_explicit_path = has_explicit_path;
	event.path_byte_len = path_byte_len;
	event.path_hash_size = path_hash_size;
	if (path_byte_len > 0U) {
		memcpy(event.path, path, path_byte_len);
	}

	return meshbus_meshcore_request_publish_accepted(
		&meshbus_meshcore_anon_data_send_request_chan, &event);
}

int meshbus_meshcore_anon_data_send(const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
				    const uint8_t *payload, size_t payload_len)
{
	return meshbus_meshcore_anon_data_send_internal(public_key, payload,
							payload_len, false, false,
							NULL, 0U, 0U, 0U);
}

int meshbus_meshcore_anon_data_send_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, uint32_t delay_ms)
{
	return meshbus_meshcore_anon_data_send_internal(public_key, payload,
							payload_len, false, false,
							NULL, 0U, 0U, delay_ms);
}

int meshbus_meshcore_anon_data_send_direct(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len)
{
	return meshbus_meshcore_anon_data_send_internal(public_key, payload,
							payload_len, true, false,
							NULL, 0U, 0U, 0U);
}

int meshbus_meshcore_anon_data_send_direct_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, uint32_t delay_ms)
{
	return meshbus_meshcore_anon_data_send_internal(public_key, payload,
							payload_len, true, false,
							NULL, 0U, 0U, delay_ms);
}

int meshbus_meshcore_anon_data_send_via_path(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, const uint8_t *path,
	uint8_t path_byte_len, uint8_t path_hash_size)
{
	return meshbus_meshcore_anon_data_send_internal(
		public_key, payload, payload_len, true, true, path,
		path_byte_len, path_hash_size, 0U);
}

int meshbus_meshcore_anon_data_send_via_path_delayed(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *payload, size_t payload_len, const uint8_t *path,
	uint8_t path_byte_len, uint8_t path_hash_size, uint32_t delay_ms)
{
	return meshbus_meshcore_anon_data_send_internal(
		public_key, payload, payload_len, true, true, path,
		path_byte_len, path_hash_size, delay_ms);
}
