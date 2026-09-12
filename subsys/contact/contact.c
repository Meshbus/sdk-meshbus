/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <contact/contact.h>
#if defined(CONFIG_MBS_MESHCORE)
#include <meshcore/meshcore.h>
#endif
#include <power/power.h>
#include <clock/timestamp.h>
#if defined(CONFIG_MBS_NOTIFY)
#include <notify/notify.h>
#endif

#include "mbs_settings_internal.h"

LOG_MODULE_REGISTER(mbs_contact, CONFIG_MBS_CONTACT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* ZBus Channels                                                              */
/* -------------------------------------------------------------------------- */

static bool contact_response_advert_validator(const void *msg, size_t msg_size);
static bool contact_change_event_validator(const void *msg, size_t msg_size);
static bool contact_response_discover_validator(const void *msg, size_t msg_size);
static bool contact_response_path_validator(const void *msg, size_t msg_size);
static bool contact_response_trace_path_validator(const void *msg, size_t msg_size);
static bool contact_binary_request_validator(const void *msg, size_t msg_size);
static bool contact_response_binary_validator(const void *msg, size_t msg_size);

ZBUS_CHAN_DEFINE(mbs_contact_share_request_chan, mbs_contact_share_request_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_discover_path_request_chan,
		 mbs_contact_discover_path_request_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_trace_path_request_chan,
		 mbs_contact_trace_path_request_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_telemetry_request_chan,
		 mbs_contact_telemetry_request_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_binary_request_chan,
		 mbs_contact_binary_request_event, contact_binary_request_validator,
		 NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_advert_response_chan,
		 mbs_contact_response_advert_event, contact_response_advert_validator,
		 NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_advert_chan, mbs_contact_response_advert_event,
		 contact_response_advert_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_store_change_chan, mbs_contact_store_change_event,
		 contact_change_event_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_discover_response_chan,
		 mbs_contact_response_discover_event, contact_response_discover_validator,
		 NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_path_response_chan, mbs_contact_response_path_event,
		 contact_response_path_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_trace_path_response_chan,
		 mbs_contact_response_trace_path_event,
		 contact_response_trace_path_validator, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_telemetry_response_chan,
		 mbs_contact_response_telemetry_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(mbs_contact_binary_response_chan,
		 mbs_contact_response_binary_event, contact_response_binary_validator,
		 NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

#define MBS_CONTACT_ROLE_MIN MBS_CONTACT_ROLE_CHAT
#define MBS_CONTACT_ROLE_MAX MBS_CONTACT_ROLE_SENSOR
#define MBS_CONTACT_SETTINGS_SUBTREE "meshbus/contact"

BUILD_ASSERT(CONFIG_MBS_CONTACT_PREFIX_BYTES >= 3 &&
		     CONFIG_MBS_CONTACT_PREFIX_BYTES <= 5,
	     "CONFIG_MBS_CONTACT_PREFIX_BYTES must be in [3,5]");

K_MUTEX_DEFINE(mbs_contact_settings_mutex);
static atomic_t shutting_down = ATOMIC_INIT(0);

static bool mbs_contact_is_shutting_down(void);
static void mbs_contact_publish_advert_event(const mbs_contact_response_advert_event *event,
						 bool is_new);
static void mbs_contact_publish_store_change(const uint8_t *prefix);

static void contact_secure_free(void *ptr, size_t len)
{
	volatile uint8_t *bytes = ptr;

	while (bytes != NULL && len > 0U) {
		*bytes++ = 0U;
		len--;
	}
	k_free(ptr);
}

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */

static bool contact_response_advert_validator(const void *msg, size_t msg_size)
{
	if (msg == NULL || msg_size != sizeof(mbs_contact_response_advert_event)) {
		return false;
	}

	const mbs_contact_response_advert_event *event = msg;

	if (event->name[sizeof(event->name) - 1U] != '\0') {
		return false;
	}
	if (event->role < MBS_CONTACT_ROLE_MIN || event->role > MBS_CONTACT_ROLE_MAX) {
		return false;
	}
	if (event->raw_advert_len > sizeof(event->raw_advert)) {
		return false;
	}
	if (event->path_hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX) {
		return false;
	}
	if (event->out_path_len > MBS_CONTACT_OUTPATH_MAX_LEN) {
		return false;
	}
	if (!event->has_out_path &&
	    (event->out_path_len != 0U || event->path_hash_size != 0U)) {
		return false;
	}
	if (event->has_out_path && event->path_hash_size == 0U) {
		return false;
	}
	if (event->has_out_path && (event->out_path_len % event->path_hash_size) != 0U) {
		return false;
	}

	return true;
}

static bool contact_change_event_validator(const void *msg, size_t msg_size)
{
	return msg != NULL && msg_size == sizeof(mbs_contact_store_change_event);
}

static bool contact_response_discover_validator(const void *msg, size_t msg_size)
{
	const mbs_contact_response_discover_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->role < MBS_CONTACT_ROLE_MIN || event->role > MBS_CONTACT_ROLE_MAX) {
		return false;
	}
	if (event->path_len > sizeof(event->path)) {
		return false;
	}

	return true;
}

static bool contact_response_path_validator(const void *msg, size_t msg_size)
{
	const mbs_contact_response_path_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}
	if (event->path_hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX) {
		return false;
	}
	if (event->out_path_len > MBS_CONTACT_OUTPATH_MAX_LEN ||
	    event->in_path_len > MBS_CONTACT_OUTPATH_MAX_LEN ||
	    event->out_path_snr_count > MBS_CONTACT_OUTPATH_MAX_LEN ||
	    event->return_path_snr_count > MBS_CONTACT_OUTPATH_MAX_LEN) {
		return false;
	}
	if (event->has_out_path && event->path_hash_size > 0U &&
	    (event->out_path_len % event->path_hash_size) != 0U) {
		return false;
	}
	if (event->in_path_len > 0U && event->path_hash_size > 0U &&
	    (event->in_path_len % event->path_hash_size) != 0U) {
		return false;
	}

	return true;
}

static bool contact_response_trace_path_validator(const void *msg, size_t msg_size)
{
	const mbs_contact_response_trace_path_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return event->out_path_snr_count <= MBS_CONTACT_OUTPATH_MAX_LEN &&
	       event->return_path_snr_count <= MBS_CONTACT_OUTPATH_MAX_LEN;
}

static bool contact_binary_request_validator(const void *msg, size_t msg_size)
{
	const mbs_contact_binary_request_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return event->payload_len > 0U && event->payload_len <= sizeof(event->payload);
}

static bool contact_response_binary_validator(const void *msg, size_t msg_size)
{
	const mbs_contact_response_binary_event *event = msg;

	if (event == NULL || msg_size != sizeof(*event)) {
		return false;
	}

	return event->payload_len <= sizeof(event->payload);
}

#define MBS_CONTACT_SETTINGS_KEY_CONTACT NULL
#define MBS_CONTACT_SETTINGS_KEY_ADVERT_RAW "advert_raw"
#define CONTACT_PREFIX_BUCKET_COUNT   256
#define CONTACT_PREFIX_BUCKET_INVALID (-1)
#define CONTACT_AUTO_ADD_FILTER_DEFAULT \
	(MBS_CONTACT_ADD_FILTER_CHAT | MBS_CONTACT_ADD_FILTER_REPEATER | \
	 MBS_CONTACT_ADD_FILTER_ROOM | MBS_CONTACT_ADD_FILTER_SENSOR | \
	 MBS_CONTACT_ADD_FILTER_MANUAL_MODE)

static size_t contact_next_free_slot;

struct contact_slot_meta {
	bool used;
	int16_t bucket_next;
	uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
};

struct contact_request_inflight {
	bool active;
	uint8_t key_prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES];
	int64_t deadline_ms;
};

struct add_contact_config {
	uint8_t add_filter;
	uint32_t hops_limit;
};

enum contact_request_type {
	CONTACT_REQUEST_DISCOVER = 0,
	CONTACT_REQUEST_TRACE = 1,
	CONTACT_REQUEST_TELEMETRY = 2,
	CONTACT_REQUEST_BINARY = 3,
};

/* Local per-request-kind throttle slots. Runtime pending state validates responses. */
#define CONTACT_REQUEST_COUNT 4U

static struct contact_slot_meta contact_slots[CONFIG_MBS_CONTACT_MAX_CONTACTS];
static int16_t contact_prefix_bucket_head[CONTACT_PREFIX_BUCKET_COUNT];
static struct contact_request_inflight contact_request_inflight[CONTACT_REQUEST_COUNT];
static struct k_work_delayable reset_contact_work;
/*
 * Contact record mutations are intentionally serialized across settings I/O.
 * Reads continue to use the short-held metadata mutex and load blobs on demand.
 * Lock order is contact_store_writer_mutex, then mbs_contact_settings_mutex.
 */
static K_MUTEX_DEFINE(contact_store_writer_mutex);
static atomic_t contact_resetting = ATOMIC_INIT(0);
static atomic_t contact_request_tag_counter = ATOMIC_INIT(1);
static uint8_t contact_count;
/*
 * Legacy records may decode with the historical 64-byte credential reserve
 * while new writes accept only the current password policy.  h_set records
 * which structurally valid slots need a sanitized rewrite; h_commit performs
 * that write after all values have been staged.
 */
#define CONTACT_SECRET_SANITIZE_PENDING_BYTES \
	DIV_ROUND_UP(CONFIG_MBS_CONTACT_MAX_CONTACTS, 8U)
static uint8_t contact_secret_sanitize_pending[CONTACT_SECRET_SANITIZE_PENDING_BYTES];

static int contact_load_by_index(size_t index, mbs_contact *out);

/* Must be called under mbs_contact_settings_mutex. */
static bool contact_secret_sanitize_pending_get_locked(size_t idx)
{
	return (contact_secret_sanitize_pending[idx / 8U] & BIT(idx % 8U)) != 0U;
}

/* Must be called under mbs_contact_settings_mutex. */
static void contact_secret_sanitize_pending_set_locked(size_t idx, bool pending)
{
	uint8_t *byte = &contact_secret_sanitize_pending[idx / 8U];
	uint8_t mask = (uint8_t)BIT(idx % 8U);

	if (pending) {
		*byte |= mask;
	} else {
		*byte &= (uint8_t)~mask;
	}
}

static uint32_t contact_timestamp_or_now(uint32_t timestamp)
{
	if (timestamp == 0U) {
		if (mbs_clock_timestamp_s_get(&timestamp) != 0 || timestamp == 0U) {
			timestamp = 1U;
		}
	}

	return timestamp;
}

static bool is_contact_resetting(void)
{
	return atomic_get(&contact_resetting) != 0;
}

static void add_contact_config_snapshot(struct add_contact_config *config)
{
	if (config == NULL) {
		return;
	}

	*config = (struct add_contact_config) {
		.add_filter = CONTACT_AUTO_ADD_FILTER_DEFAULT,
	};

#if defined(CONFIG_MBS_MESHCORE)
	mbs_meshcore_config cfg;

	if (mbs_meshcore_config_get(&cfg) == 0) {
		config->add_filter = cfg.add_contact_config;
		config->hops_limit = cfg.add_contact_hops_limit;
	}
#endif
}

static bool contact_advert_auto_add_role_allowed(uint8_t add_contact_config,
					      mbs_contact_role role)
{
	uint8_t role_bit = 0U;

	if ((add_contact_config & MBS_CONTACT_ADD_FILTER_MANUAL_MODE) == 0U) {
		return true;
	}

	switch (role) {
	case MBS_CONTACT_ROLE_CHAT:
		role_bit = MBS_CONTACT_ADD_FILTER_CHAT;
		break;
	case MBS_CONTACT_ROLE_REPEATER:
		role_bit = MBS_CONTACT_ADD_FILTER_REPEATER;
		break;
	case MBS_CONTACT_ROLE_ROOM:
		role_bit = MBS_CONTACT_ADD_FILTER_ROOM;
		break;
	case MBS_CONTACT_ROLE_SENSOR:
		role_bit = MBS_CONTACT_ADD_FILTER_SENSOR;
		break;
	default:
		return false;
	}

	return (add_contact_config & role_bit) != 0U;
}

static bool contact_advert_auto_add_hops_allowed(
	const mbs_contact_response_advert_event *event, const struct add_contact_config *config)
{
	uint8_t hop_count;

	if (config == NULL || config->hops_limit == 0U) {
		return true;
	}
	if (event == NULL || !event->has_out_path || event->path_hash_size == 0U ||
	    event->path_hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX ||
	    (event->out_path_len % event->path_hash_size) != 0U) {
		return false;
	}

	hop_count = event->out_path_len / event->path_hash_size;
	return hop_count < config->hops_limit;
}

static bool contact_advert_auto_add_allowed(const mbs_contact_response_advert_event *event)
{
	if (event == NULL) {
		return false;
	}

	struct add_contact_config config;

	add_contact_config_snapshot(&config);
	return contact_advert_auto_add_role_allowed(config.add_filter, event->role) &&
	       contact_advert_auto_add_hops_allowed(event, &config);
}

static int contact_persistent_structure_validate(const mbs_contact *contact_record)
{
	if (contact_record == NULL) {
		return -EINVAL;
	}
	if (contact_record->public_key.size != MBS_CONTACT_PUBLIC_KEY_SIZE) {
		return -EINVAL;
	}
	if (contact_record->out_path.size > sizeof(contact_record->out_path.bytes)) {
		return -EINVAL;
	}
	if (contact_record->path_hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX) {
		return -EINVAL;
	}
	if (contact_record->role < MBS_CONTACT_ROLE_MIN || contact_record->role > MBS_CONTACT_ROLE_MAX) {
		return -EINVAL;
	}
	if (contact_record->name[sizeof(contact_record->name) - 1U] != '\0') {
		return -EINVAL;
	}
	if (contact_record->alias[sizeof(contact_record->alias) - 1U] != '\0') {
		return -EINVAL;
	}
	if (contact_record->management_secret.size >
	    sizeof(contact_record->management_secret.bytes)) {
		return -EINVAL;
	}

	return 0;
}

static bool contact_management_secret_is_write_valid(const mbs_contact *contact_record)
{
	if (contact_record == NULL ||
	    contact_record->management_secret.size >
		    sizeof(contact_record->management_secret.bytes)) {
		return false;
	}
	if (contact_record->management_secret.size != 0U &&
	    (contact_record->management_secret.size <
		     MBS_CONTACT_MANAGEMENT_SECRET_MIN_LEN ||
	     contact_record->management_secret.size >
		     MBS_CONTACT_MANAGEMENT_SECRET_MAX_LEN)) {
		return false;
	}
	for (size_t i = 0U; i < contact_record->management_secret.size; i++) {
		if (contact_record->management_secret.bytes[i] < 0x21U ||
		    contact_record->management_secret.bytes[i] > 0x7eU) {
			return false;
		}
	}

	return true;
}

static bool contact_management_secret_sanitize(mbs_contact *contact_record)
{
	if (contact_record == NULL) {
		return false;
	}
	if (contact_management_secret_is_write_valid(contact_record)) {
		return false;
	}

	memset(&contact_record->management_secret, 0,
	       sizeof(contact_record->management_secret));
	return true;
}

static int contact_persistent_validate(const mbs_contact *contact_record)
{
	if (contact_persistent_structure_validate(contact_record) != 0 ||
	    !contact_management_secret_is_write_valid(contact_record)) {
		return -EINVAL;
	}

	return 0;
}

static void contact_prefix_bucket_remove(size_t idx)
{
	if (idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS || !contact_slots[idx].used) {
		return;
	}

	uint8_t bucket = contact_slots[idx].prefix[0];
	int16_t prev = CONTACT_PREFIX_BUCKET_INVALID;
	int16_t cur = contact_prefix_bucket_head[bucket];

	while (cur != CONTACT_PREFIX_BUCKET_INVALID) {
		if ((size_t)cur == idx) {
			if (prev == CONTACT_PREFIX_BUCKET_INVALID) {
				contact_prefix_bucket_head[bucket] = contact_slots[cur].bucket_next;
			} else {
				contact_slots[prev].bucket_next = contact_slots[cur].bucket_next;
			}
			contact_slots[idx].bucket_next = CONTACT_PREFIX_BUCKET_INVALID;
			return;
		}
		prev = cur;
		cur = contact_slots[cur].bucket_next;
	}
}

static void contact_prefix_bucket_insert(size_t idx)
{
	if (idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS || !contact_slots[idx].used) {
		return;
	}

	uint8_t bucket = contact_slots[idx].prefix[0];
	int16_t prev = CONTACT_PREFIX_BUCKET_INVALID;
	int16_t cur = contact_prefix_bucket_head[bucket];

	while (cur != CONTACT_PREFIX_BUCKET_INVALID && (size_t)cur < idx) {
		prev = cur;
		cur = contact_slots[cur].bucket_next;
	}

	if (prev == CONTACT_PREFIX_BUCKET_INVALID) {
		contact_slots[idx].bucket_next = contact_prefix_bucket_head[bucket];
		contact_prefix_bucket_head[bucket] = (int16_t)idx;
		return;
	}

	contact_slots[idx].bucket_next = contact_slots[prev].bucket_next;
	contact_slots[prev].bucket_next = (int16_t)idx;
}

/* Must be called under mbs_contact_settings_mutex. */
static int contact_slot_find_by_prefix_locked(const uint8_t *prefix, size_t *idx_out)
{
	if (prefix == NULL || idx_out == NULL) {
		return -EINVAL;
	}

	int16_t cur = contact_prefix_bucket_head[prefix[0]];
	while (cur != CONTACT_PREFIX_BUCKET_INVALID) {
		size_t idx = (size_t)cur;

		if (contact_slots[idx].used && memcmp(contact_slots[idx].prefix, prefix,
						   CONFIG_MBS_CONTACT_PREFIX_BYTES) == 0) {
			*idx_out = idx;
			return 0;
		}
		cur = contact_slots[cur].bucket_next;
	}

	return -ENOENT;
}

static size_t contact_find_next_free_slot_from(size_t start)
{
	for (size_t i = start; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		if (!contact_slots[i].used) {
			return i;
		}
	}

	return CONFIG_MBS_CONTACT_MAX_CONTACTS;
}

/* Must be called under mbs_contact_settings_mutex. */
static int contact_slot_mark_used_locked(size_t idx, const uint8_t *public_key)
{
	if (idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS || public_key == NULL) {
		return -EINVAL;
	}

	size_t existing_idx = 0U;
	int rc = contact_slot_find_by_prefix_locked(public_key, &existing_idx);
	if (rc == 0 && existing_idx != idx) {
		return -EADDRINUSE;
	}

	bool was_used = contact_slots[idx].used;
	if (was_used) {
		contact_prefix_bucket_remove(idx);
	}

	contact_slots[idx].used = true;
	memcpy(contact_slots[idx].prefix, public_key, sizeof(contact_slots[idx].prefix));
	contact_prefix_bucket_insert(idx);

	if (!was_used) {
		contact_count++;
		if (contact_next_free_slot == idx) {
			contact_next_free_slot = contact_find_next_free_slot_from(idx + 1U);
		}
	}

	return 0;
}

static void contact_slot_clear_by_id(size_t idx)
{
	if (idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS) {
		return;
	}

	bool was_used = contact_slots[idx].used;
	if (contact_slots[idx].used) {
		contact_prefix_bucket_remove(idx);
		contact_count = (contact_count > 0U) ? (uint8_t)(contact_count - 1U) : 0U;
	}
	contact_slots[idx].used = false;
	memset(contact_slots[idx].prefix, 0, sizeof(contact_slots[idx].prefix));
	contact_slots[idx].bucket_next = CONTACT_PREFIX_BUCKET_INVALID;
	contact_secret_sanitize_pending_set_locked(idx, false);
	if (was_used && idx < contact_next_free_slot) {
		contact_next_free_slot = idx;
	}
}

static void contact_prefix_bucket_reset(void)
{
	for (size_t i = 0; i < CONTACT_PREFIX_BUCKET_COUNT; i++) {
		contact_prefix_bucket_head[i] = CONTACT_PREFIX_BUCKET_INVALID;
	}
	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		contact_slots[i] = (struct contact_slot_meta){
			.bucket_next = CONTACT_PREFIX_BUCKET_INVALID,
		};
	}
	memset(contact_secret_sanitize_pending, 0, sizeof(contact_secret_sanitize_pending));
	contact_count = 0;
	contact_next_free_slot = 0U;
	for (size_t i = 0; i < CONTACT_REQUEST_COUNT; i++) {
		contact_request_inflight[i] = (struct contact_request_inflight){0};
	}
}

static void contact_request_inflight_clear_locked(struct contact_request_inflight *inflight)
{
	if (inflight == NULL) {
		return;
	}

	memset(inflight, 0, sizeof(*inflight));
}

static void contact_request_inflight_clear_if_match_locked(
	struct contact_request_inflight *inflight,
	const struct contact_request_inflight *snapshot)
{
	if (inflight == NULL || snapshot == NULL) {
		return;
	}
	if (!inflight->active || !snapshot->active) {
		return;
	}
	if (inflight->deadline_ms != snapshot->deadline_ms) {
		return;
	}
	if (memcmp(inflight->key_prefix, snapshot->key_prefix,
		   sizeof(inflight->key_prefix)) != 0) {
		return;
	}

	contact_request_inflight_clear_locked(inflight);
}

static bool contact_persist_has_change(const mbs_contact *old_contact,
				       const mbs_contact *new_contact)
{
	if (old_contact == NULL || new_contact == NULL) {
		return true;
	}

	return memcmp(old_contact->name, new_contact->name, sizeof(old_contact->name)) != 0 ||
	       old_contact->role != new_contact->role ||
	       old_contact->out_path.size != new_contact->out_path.size ||
	       memcmp(old_contact->out_path.bytes, new_contact->out_path.bytes,
		      old_contact->out_path.size) != 0 ||
	       old_contact->is_neighbor != new_contact->is_neighbor ||
	       old_contact->first_seen_timestamp != new_contact->first_seen_timestamp ||
	       old_contact->flags != new_contact->flags ||
	       old_contact->latitude != new_contact->latitude ||
	       old_contact->longitude != new_contact->longitude ||
	       old_contact->last_seen_timestamp != new_contact->last_seen_timestamp ||
	       old_contact->last_seen_snr != new_contact->last_seen_snr ||
	       old_contact->path_hash_size != new_contact->path_hash_size ||
	       memcmp(old_contact->alias, new_contact->alias, sizeof(old_contact->alias)) != 0 ||
	       old_contact->management_secret.size != new_contact->management_secret.size ||
	       memcmp(old_contact->management_secret.bytes, new_contact->management_secret.bytes,
		      old_contact->management_secret.size) != 0;
}

/* -------------------------------------------------------------------------- */
/* Settings Helpers And Schema                                                */
/* -------------------------------------------------------------------------- */

MBS_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(contact_settings_schema, MBS_CONTACT_SETTINGS_SUBTREE,
				       MBS_CONTACT_SETTINGS_KEY_CONTACT, meshbus_Contact,
				       mbs_contact);

#if defined(CONFIG_MBS_CONTACT_ADVERT_RAW_STORE)
MBS_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(contact_advert_raw_settings_schema,
				      MBS_CONTACT_SETTINGS_SUBTREE,
				      MBS_CONTACT_SETTINGS_KEY_ADVERT_RAW);
#endif

static int contact_slot_persist_blob(size_t slot_idx, const mbs_contact *contact_record)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	int rc;

	if (slot_idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS || contact_record == NULL) {
		return -EINVAL;
	}
	if (contact_persistent_validate(contact_record) != 0) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_blob_save_with_buffer(&contact_settings_schema, slot_idx, contact_record,
						       buffer, sizeof(buffer));
	if (rc != 0) {
		LOG_WRN("Contact blob persist failed: idx=%u rc=%d", (unsigned int)slot_idx, rc);
		return rc;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	contact_secret_sanitize_pending_set_locked(slot_idx, false);
	k_mutex_unlock(&mbs_contact_settings_mutex);

	return 0;
}

static int contact_slot_persist_diff(size_t slot_idx, const mbs_contact *old_contact,
				  const mbs_contact *new_contact)
{
	if (old_contact == NULL || new_contact == NULL || contact_persistent_validate(new_contact) != 0) {
		return -EINVAL;
	}

	if (!contact_persist_has_change(old_contact, new_contact)) {
		return 0;
	}

	return contact_slot_persist_blob(slot_idx, new_contact);
}

static int contact_find_by_prefix_internal(const uint8_t *prefix, size_t *slot_idx_out,
					mbs_contact *contact_out)
{
	if (prefix == NULL) {
		return -EINVAL;
	}

	size_t idx = 0U;
	int rc = 0;

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	rc = contact_slot_find_by_prefix_locked(prefix, &idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc != 0) {
		return rc;
	}

	mbs_contact contact_record = meshbus_Contact_init_zero;
	rc = contact_load_by_index(idx, &contact_record);
	if (rc != 0) {
		return rc;
	}
	if (memcmp(contact_record.public_key.bytes, prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES) != 0) {
		return -ENOENT;
	}

	if (slot_idx_out != NULL) {
		*slot_idx_out = idx;
	}
	if (contact_out != NULL) {
		*contact_out = contact_record;
	}

	return 0;
}

static int contact_update_last_seen_internal(size_t slot_idx, mbs_contact *contact, uint32_t timestamp,
					  bool has_snr, int32_t snr, bool *changed_out)
{
	bool changed = false;

	ARG_UNUSED(slot_idx);

	if (contact == NULL) {
		return -EINVAL;
	}

	timestamp = contact_timestamp_or_now(timestamp);

	if (contact->first_seen_timestamp == 0U) {
		contact->first_seen_timestamp = timestamp;
		changed = true;
	}

	changed = changed || (contact->last_seen_timestamp != timestamp);
	contact->last_seen_timestamp = timestamp;

	if (has_snr) {
		changed = changed || (contact->last_seen_snr != snr);
		contact->last_seen_snr = snr;
	}

	if (changed_out != NULL) {
		*changed_out = changed;
	}

	return 0;
}

static int contact_update_known_path_fields_internal(mbs_contact *contact, bool has_out_path,
						  uint8_t out_path_len, uint8_t path_hash_size,
						  const uint8_t *out_path, bool *changed_out)
{
	bool changed = false;
	size_t path_len;
	bool is_neighbor;

	if (contact == NULL) {
		return -EINVAL;
	}

	if (!has_out_path) {
		if (changed_out != NULL) {
			*changed_out = false;
		}
		return 0;
	}

	if (path_hash_size == 0U || path_hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX) {
		return -EINVAL;
	}
	if (out_path_len > sizeof(contact->out_path.bytes)) {
		return -EINVAL;
	}
	path_len = out_path_len;
	if ((path_len > 0U && out_path == NULL) || (path_len % path_hash_size) != 0U) {
		return -EINVAL;
	}
	is_neighbor = (path_len == 0U);

	if (contact->out_path.size != path_len ||
	    (path_len > 0U && memcmp(contact->out_path.bytes, out_path, path_len) != 0)) {
		memset(contact->out_path.bytes, 0, sizeof(contact->out_path.bytes));
		if (path_len > 0U) {
			memcpy(contact->out_path.bytes, out_path, path_len);
		}
		contact->out_path.size = (pb_size_t)path_len;
		changed = true;
	}

	if (contact->path_hash_size != path_hash_size) {
		contact->path_hash_size = path_hash_size;
		changed = true;
	}

	if (contact->is_neighbor != is_neighbor) {
		contact->is_neighbor = is_neighbor;
		changed = true;
	}

	if (changed_out != NULL) {
		*changed_out = changed;
	}

	return 0;
}

static int contact_update_advert_fields_internal(size_t slot_idx, mbs_contact *contact,
					      const mbs_contact_response_advert_event *event,
					      bool *changed_out)
{
	bool changed = false;
	bool path_changed = false;
	int rc;

	ARG_UNUSED(slot_idx);

	if (contact == NULL || event == NULL) {
		return -EINVAL;
	}

	if (memcmp(contact->name, event->name, sizeof(contact->name)) != 0) {
		memcpy(contact->name, event->name, sizeof(contact->name));
		contact->name[sizeof(contact->name) - 1U] = '\0';
		changed = true;
	}

	if (event->has_position) {
		if (contact->latitude != event->latitude) {
			contact->latitude = event->latitude;
			changed = true;
		}
		if (contact->longitude != event->longitude) {
			contact->longitude = event->longitude;
			changed = true;
		}
	}

	rc = contact_update_known_path_fields_internal(contact, event->has_out_path,
						   event->out_path_len, event->path_hash_size,
						   event->out_path, &path_changed);
	if (rc != 0) {
		return rc;
	}
	changed = changed || path_changed;

	if (changed_out != NULL) {
		*changed_out = changed;
	}

	return 0;
}

static int contact_identity_init(mbs_contact *contact, const uint8_t *public_key,
				 const char *name, mbs_contact_role role)
{
	if (contact == NULL || public_key == NULL ||
	    role < MBS_CONTACT_ROLE_MIN || role > MBS_CONTACT_ROLE_MAX) {
		return -EINVAL;
	}

	*contact = (mbs_contact)meshbus_Contact_init_zero;
	contact->public_key.size = MBS_CONTACT_PUBLIC_KEY_SIZE;
	memcpy(contact->public_key.bytes, public_key, MBS_CONTACT_PUBLIC_KEY_SIZE);
	contact->role = role;
	if (name != NULL && name[0] != '\0') {
		strncpy(contact->name, name, sizeof(contact->name) - 1U);
		contact->name[sizeof(contact->name) - 1U] = '\0';
	}

	return 0;
}

static int contact_update_path_fields_internal(size_t slot_idx, mbs_contact *contact,
					    const mbs_contact_response_path_event *event,
					    bool *changed_out)
{
	uint8_t path_hash_size;

	ARG_UNUSED(slot_idx);

	if (contact == NULL || event == NULL) {
		return -EINVAL;
	}

	path_hash_size = event->path_hash_size;
	if (event->has_out_path && path_hash_size == 0U) {
		path_hash_size = 1U;
	}

	return contact_update_known_path_fields_internal(contact, event->has_out_path,
						     event->out_path_len,
						     path_hash_size, event->out_path,
						     changed_out);
}

#if defined(CONFIG_MBS_CONTACT_ADVERT_RAW_STORE)
static int contact_setting_load_advert_raw_by_id(size_t slot_idx, uint8_t *out_raw, size_t out_raw_sz,
					      size_t *out_raw_len)
{
	int rc;

	if (out_raw == NULL || out_raw_len == NULL || out_raw_sz == 0U) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_raw_load(&contact_advert_raw_settings_schema, slot_idx,
					  out_raw, out_raw_sz, out_raw_len);
	if (rc == -ENOENT) {
		return -ENODATA;
	}
	if (rc == 0 && *out_raw_len == 0U) {
		return -ENODATA;
	}

	return rc;
}

static int contact_setting_save_advert_raw_by_id(size_t idx, const uint8_t *raw, size_t raw_len)
{
	uint8_t existing[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
	size_t existing_len = 0U;
	int rc;

	if (raw == NULL || raw_len == 0U || raw_len > MBS_CONTACT_ADVERT_RAW_MAX_LEN) {
		return -EINVAL;
	}

	rc = contact_setting_load_advert_raw_by_id(idx, existing, sizeof(existing), &existing_len);
	if (rc == 0 && existing_len == raw_len && memcmp(existing, raw, raw_len) == 0) {
		return 0;
	}

	rc = mbs_settings_indexed_raw_save(&contact_advert_raw_settings_schema, idx, raw, raw_len);
	if (rc != 0) {
		LOG_WRN("Contact advert_raw persist failed: idx=%u rc=%d", (unsigned int)idx, rc);
		return rc;
	}

	return 0;
}

static int contact_setting_delete_advert_raw_by_id(size_t idx)
{
	return mbs_settings_indexed_raw_delete(&contact_advert_raw_settings_schema, idx);
}

#else
static int contact_setting_load_advert_raw_by_id(size_t slot_idx, uint8_t *out_raw, size_t out_raw_sz,
					      size_t *out_raw_len)
{
	ARG_UNUSED(slot_idx);
	ARG_UNUSED(out_raw);
	ARG_UNUSED(out_raw_sz);
	ARG_UNUSED(out_raw_len);

	return -ENODATA;
}

static int contact_setting_save_advert_raw_by_id(size_t idx, const uint8_t *raw, size_t raw_len)
{
	ARG_UNUSED(idx);
	ARG_UNUSED(raw);
	ARG_UNUSED(raw_len);

	return 0;
}

static int contact_setting_delete_advert_raw_by_id(size_t idx)
{
	ARG_UNUSED(idx);

	return 0;
}
#endif

static int contact_setting_delete_slot_by_id(size_t idx)
{
	int rc;

	/* Clear advert_raw before the contact slot can be rewritten. */
	rc = contact_setting_delete_advert_raw_by_id(idx);
	if (rc != 0) {
		return rc;
	}

	return mbs_settings_indexed_blob_delete(&contact_settings_schema, idx);
}

static int contact_load_by_index(size_t index, mbs_contact *out)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}
	if (index >= CONFIG_MBS_CONTACT_MAX_CONTACTS) {
		return -ENOENT;
	}

	memset(out, 0, sizeof(*out));
	rc = mbs_settings_indexed_blob_load_with_buffer(&contact_settings_schema, index, out,
						       buffer, sizeof(buffer));
	if (rc != 0) {
		memset(out, 0, sizeof(*out));
		return rc;
	}
	if (contact_persistent_structure_validate(out) != 0) {
		LOG_WRN("Invalid persisted contact ignored: idx=%u", (unsigned int)index);
		memset(out, 0, sizeof(*out));
		return -EINVAL;
	}

	/* Never return a legacy credential that violates the current write policy. */
	(void)contact_management_secret_sanitize(out);

	return 0;
}

static int contact_find_by_key(const uint8_t *key, size_t *slot_idx_out, mbs_contact *contact_out)
{
	if (key == NULL) {
		return -EINVAL;
	}

	size_t idx = 0U;
	int rc = 0;

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	rc = contact_slot_find_by_prefix_locked(key, &idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc != 0) {
		return rc;
	}

	mbs_contact contact_record = meshbus_Contact_init_zero;
	rc = contact_load_by_index(idx, &contact_record);
	if (rc != 0) {
		return rc;
	}
	if (memcmp(contact_record.public_key.bytes, key, MBS_CONTACT_PUBLIC_KEY_SIZE) != 0) {
		return -ENOENT;
	}

	if (slot_idx_out != NULL) {
		*slot_idx_out = idx;
	}
	if (contact_out != NULL) {
		*contact_out = contact_record;
	}

	return 0;
}

static int contact_alloc_index(size_t *slot_idx_out, bool *evicted_out)
{
	if (slot_idx_out == NULL || evicted_out == NULL) {
		return -EINVAL;
	}

	bool overwrite_oldest = false;

	*evicted_out = false;
	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (contact_next_free_slot < CONFIG_MBS_CONTACT_MAX_CONTACTS &&
	    !contact_slots[contact_next_free_slot].used) {
		*slot_idx_out = contact_next_free_slot;
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return 0;
	}

	contact_next_free_slot =
		contact_find_next_free_slot_from(contact_next_free_slot < CONFIG_MBS_CONTACT_MAX_CONTACTS ?
						      contact_next_free_slot :
						      0U);
	if (contact_next_free_slot < CONFIG_MBS_CONTACT_MAX_CONTACTS) {
		*slot_idx_out = contact_next_free_slot;
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return 0;
	}

	struct add_contact_config config;

	add_contact_config_snapshot(&config);
	overwrite_oldest =
		(config.add_filter & MBS_CONTACT_ADD_FILTER_OVERWRITE_OLDEST) != 0U;
	if (!overwrite_oldest) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return -ENOSPC;
	}

	k_mutex_unlock(&mbs_contact_settings_mutex);

	bool have = false;
	size_t victim = 0U;
	uint32_t best_ts = 0U;

	/* contact_store_writer_mutex keeps the slot table stable while persistence
	 * is read, so snapshot only the current slot instead of retaining an array
	 * of every used index on the caller's stack.
	 */
	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		mbs_contact contact_record = meshbus_Contact_init_zero;
		int rc = 0;
		uint8_t prefix[CONFIG_MBS_CONTACT_PREFIX_BYTES] = {0};

		k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
		if (!contact_slots[i].used) {
			k_mutex_unlock(&mbs_contact_settings_mutex);
			continue;
		}
		memcpy(prefix, contact_slots[i].prefix, sizeof(prefix));
		k_mutex_unlock(&mbs_contact_settings_mutex);

		rc = contact_load_by_index(i, &contact_record);
		if (rc != 0) {
			LOG_WRN("Contact load failed during eviction pick: idx=%u rc=%d",
				(unsigned int)i, rc);
			continue;
		}
		if (memcmp(contact_record.public_key.bytes, prefix, sizeof(prefix)) != 0) {
			LOG_WRN("Contact prefix mismatch during eviction pick: idx=%u",
				(unsigned int)i);
			continue;
		}
		if ((contact_record.flags & MBS_CONTACT_FLAG_FAVORITE) != 0U) {
			continue;
		}

		uint32_t ts = contact_record.last_seen_timestamp;
		if (!have || ts < best_ts) {
			have = true;
			best_ts = ts;
			victim = i;
		}
	}

	if (!have) {
		return -ENOSPC;
	}

	*slot_idx_out = victim;
	*evicted_out = true;
	return 0;
}

static int contact_slots_build(const char *name, size_t len, settings_read_cb read_cb,
			    void *cb_arg)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	mbs_contact contact = meshbus_Contact_init_zero;
	size_t idx = 0U;
	bool secret_sanitized;
	int rc;

	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}
	rc = mbs_settings_indexed_blob_read_slot_with_buffer(&contact_settings_schema, name, len,
							   read_cb, cb_arg,
							   CONFIG_MBS_CONTACT_MAX_CONTACTS,
							   &contact, buffer, sizeof(buffer), &idx);
	if (rc == -ENOENT) {
		return -ENOENT;
	}
	if (rc == -ERANGE) {
		LOG_WRN("Ignore out-of-range or oversized contact slot blob: key=%s len=%u", name,
			(unsigned int)len);
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Ignore malformed restored contact blob: idx=%u rc=%d",
			(unsigned int)idx, rc);
		return 0;
	}
	if (contact_persistent_structure_validate(&contact) != 0) {
		LOG_WRN("Ignore invalid restored contact blob: idx=%u", (unsigned int)idx);
		return 0;
	}
	secret_sanitized = contact_management_secret_sanitize(&contact);

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	rc = contact_slot_mark_used_locked(idx, contact.public_key.bytes);
	if (rc == 0) {
		contact_secret_sanitize_pending_set_locked(idx, secret_sanitized);
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);

	if (rc == -EADDRINUSE) {
		LOG_WRN("Ignore restored contact with duplicated prefix: idx=%u", (unsigned int)idx);
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Restore contact slot failed: idx=%u rc=%d", (unsigned int)idx, rc);
		return 0;
	}
	if (secret_sanitized) {
		LOG_WRN("Cleared legacy contact management password: idx=%u",
			(unsigned int)idx);
	}

	return 0;
}

static void reset_contact_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (!is_contact_resetting()) {
		k_mutex_unlock(&contact_store_writer_mutex);
		return;
	}

	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		int rc = contact_setting_delete_slot_by_id(i);

		if (rc != 0) {
				LOG_WRN("Contact reset cleanup delete failed: idx=%u rc=%d",
					(unsigned int)i, rc);
		}
	}

	atomic_clear(&contact_resetting);
	k_mutex_unlock(&contact_store_writer_mutex);

	LOG_DBG("Contact reset cleanup finished");
	return;
}

/* -------------------------------------------------------------------------- */
/* Contact Store Helpers                                                         */
/* -------------------------------------------------------------------------- */

static int contact_insert_locked(const mbs_contact *contact, size_t *index_out)
{
	if (contact_persistent_validate(contact) != 0) {
		return -EINVAL;
	}
	if (is_contact_resetting()) {
		return -EBUSY;
	}

	size_t idx;
	bool evicted;

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	size_t prefix_idx = 0U;
	int rc = contact_slot_find_by_prefix_locked(contact->public_key.bytes, &prefix_idx);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return -ESHUTDOWN;
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);

	if (rc == 0) {
		mbs_contact contact_record = meshbus_Contact_init_zero;

		rc = contact_load_by_index(prefix_idx, &contact_record);
		if (rc == 0 && memcmp(contact_record.public_key.bytes,
				      contact->public_key.bytes,
				      MBS_CONTACT_PUBLIC_KEY_SIZE) == 0) {
			LOG_DBG("Contact insert ignored: already exists");
			return -EEXIST;
		}

		LOG_WRN("Contact insert rejected: prefix already used by another key");
		return -EADDRINUSE;
	}
	if (rc != -ENOENT) {
		return rc;
	}

	rc = contact_alloc_index(&idx, &evicted);
	if (rc != 0) {
		if (rc == -ENOSPC) {
			LOG_WRN("Contact insert failed: no slot available");
		}
		return rc;
	}

	if (evicted) {
		LOG_WRN("Contact table full: evicted index %u", (unsigned int)idx);
	}

	/*
	 * Preserve an eviction victim's main blob until the replacement save
	 * succeeds. The indexed save replaces the same key without a preceding main
	 * blob delete; only the victim's optional raw advert needs explicit cleanup
	 * first.
	 */
	rc = evicted ? contact_setting_delete_advert_raw_by_id(idx) :
		       contact_setting_delete_slot_by_id(idx);
	if (rc != 0) {
		LOG_WRN("Contact insert slot cleanup failed: idx=%u rc=%d", (unsigned int)idx, rc);
		return rc;
	}

	mbs_contact contact_record = *contact;

	contact_record.name[sizeof(contact_record.name) - 1U] = '\0';
	contact_record.alias[sizeof(contact_record.alias) - 1U] = '\0';
	if (contact_record.alias[0] == '\0' && contact_record.name[0] != '\0') {
		memcpy(contact_record.alias, contact_record.name,
		       sizeof(contact_record.alias));
		contact_record.alias[sizeof(contact_record.alias) - 1U] = '\0';
	}
	if (contact_record.last_seen_timestamp == 0U) {
		contact_record.last_seen_timestamp =
			contact_timestamp_or_now(contact_record.first_seen_timestamp);
	}

	rc = contact_slot_persist_blob(idx, &contact_record);
	if (rc != 0) {
		LOG_WRN("Contact persist failed after insert: idx=%u rc=%d", (unsigned int)idx, rc);
		return rc;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	rc = contact_slot_mark_used_locked(idx, contact_record.public_key.bytes);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc != 0) {
		LOG_WRN("Contact slot index update failed after create: idx=%u rc=%d",
			(unsigned int)idx, rc);
		return rc;
	}

	LOG_DBG("Contact inserted: idx=%u role=%u name=%s", (unsigned int)idx, (unsigned int)contact_record.role,
		contact_record.name);
	if (index_out != NULL) {
		*index_out = idx;
	}
	return 0;
}

static __noinline int contact_apply_update_locked(size_t idx,
					  const mbs_contact *contact,
					  bool allow_name_update,
					  bool *changed_out)
{
	struct contact_apply_update_context {
		mbs_contact contact_record;
		mbs_contact existing;
	};
	struct contact_apply_update_context *context;
	int rc;
	bool persist_changed;

	if (changed_out != NULL) {
		*changed_out = false;
	}
	if (contact_persistent_validate(contact) != 0) {
		return -EINVAL;
	}
	if (is_contact_resetting()) {
		return -EBUSY;
	}

	context = k_calloc(1U, sizeof(*context));
	if (context == NULL) {
		return -ENOMEM;
	}
	context->contact_record = *contact;
	context->contact_record.name[
		sizeof(context->contact_record.name) - 1U] = '\0';
	context->contact_record.alias[
		sizeof(context->contact_record.alias) - 1U] = '\0';

	rc = contact_load_by_index(idx, &context->existing);
	if (rc != 0) {
		goto out;
	}
	if (memcmp(context->existing.public_key.bytes,
		   contact->public_key.bytes,
		   MBS_CONTACT_PUBLIC_KEY_SIZE) != 0) {
		rc = -EADDRINUSE;
		goto out;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		rc = -ESHUTDOWN;
		goto out;
	}

	if (!allow_name_update) {
		memcpy(context->contact_record.name, context->existing.name,
		       sizeof(context->contact_record.name));
		context->contact_record.name[
			sizeof(context->contact_record.name) - 1U] = '\0';
	}
	if (context->contact_record.alias[0] == '\0') {
		if (context->existing.alias[0] != '\0') {
			memcpy(context->contact_record.alias, context->existing.alias,
			       sizeof(context->contact_record.alias));
			context->contact_record.alias[
				sizeof(context->contact_record.alias) - 1U] = '\0';
		} else if (context->contact_record.name[0] != '\0') {
			memcpy(context->contact_record.alias,
			       context->contact_record.name,
			       sizeof(context->contact_record.alias));
			context->contact_record.alias[
				sizeof(context->contact_record.alias) - 1U] = '\0';
		}
	}

	persist_changed = contact_persist_has_change(&context->existing,
					     &context->contact_record);
	if (!persist_changed) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		rc = 0;
		goto out;
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);

	rc = contact_slot_persist_diff(idx, &context->existing,
				       &context->contact_record);
	if (rc != 0) {
		LOG_WRN("Contact persist failed after update: idx=%u rc=%d", (unsigned int)idx, rc);
		goto out;
	}
	LOG_DBG("Contact updated: idx=%u role=%u name=%s", (unsigned int)idx,
		(unsigned int)context->contact_record.role,
		context->contact_record.name);

	if (changed_out != NULL) {
		*changed_out = true;
	}

out:
	contact_secure_free(context, sizeof(*context));
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Request And Response Handlers                                              */
/* -------------------------------------------------------------------------- */

static int contact_publish_request(enum contact_request_type type, const struct zbus_channel *chan,
				   const uint8_t *key_prefix, uint32_t tag)
{
	if ((unsigned int)type >= CONTACT_REQUEST_COUNT) {
		return -EINVAL;
	}

	mbs_contact contact = meshbus_Contact_init_zero;
	int rc = mbs_contact_find_by_prefix(key_prefix, &contact);
	if (rc != 0) {
		return rc;
	}
	if (type == CONTACT_REQUEST_TRACE && contact.out_path.size == 0U && !contact.is_neighbor) {
		LOG_WRN("Trace request rejected: unknown out_path key_prefix=%02x%02x%02x",
			key_prefix[0], key_prefix[1], key_prefix[2]);
		return -EINVAL;
	}

	int64_t now_ms = k_uptime_get();
	struct contact_request_inflight snapshot = {0};

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return -ESHUTDOWN;
	}
	struct contact_request_inflight *inflight = &contact_request_inflight[(size_t)type];
	if (inflight->active) {
		if (now_ms <= inflight->deadline_ms) {
			LOG_DBG("Contact request throttled: kind=%u key_prefix=%02x%02x%02x "
				"inflight=%02x%02x%02x",
				(unsigned int)type, key_prefix[0], key_prefix[1], key_prefix[2],
				inflight->key_prefix[0], inflight->key_prefix[1],
				inflight->key_prefix[2]);
			k_mutex_unlock(&mbs_contact_settings_mutex);
			return -EBUSY;
		}

		LOG_WRN("Contact request timeout: kind=%u key_prefix=%02x%02x%02x",
			(unsigned int)type, inflight->key_prefix[0], inflight->key_prefix[1],
			inflight->key_prefix[2]);
		contact_request_inflight_clear_locked(inflight);
	}

	inflight->active = true;
	memcpy(inflight->key_prefix, key_prefix, sizeof(inflight->key_prefix));
	inflight->deadline_ms = now_ms + (int64_t)CONFIG_MBS_CONTACT_REQUEST_TIMEOUT_MS;
	/* Keep a copy so publish-failure cleanup won't clear a newer inflight state
	 * that may have been replaced or completed after we dropped the mutex.
	 */
	snapshot = *inflight;
	k_mutex_unlock(&mbs_contact_settings_mutex);

	switch (type) {
	case CONTACT_REQUEST_DISCOVER: {
		mbs_contact_discover_path_request_event event = {
			.key_prefix = {0},
			.tag = tag,
		};

		memcpy(event.key_prefix, key_prefix, sizeof(event.key_prefix));
		rc = zbus_chan_pub(chan, &event, K_NO_WAIT);
		break;
	}
	case CONTACT_REQUEST_TRACE: {
		mbs_contact_trace_path_request_event event = {
			.key_prefix = {0},
			.tag = tag,
		};

		memcpy(event.key_prefix, key_prefix, sizeof(event.key_prefix));
		rc = zbus_chan_pub(chan, &event, K_NO_WAIT);
		break;
	}
	case CONTACT_REQUEST_TELEMETRY: {
		mbs_contact_telemetry_request_event event = {
			.key_prefix = {0},
			.tag = tag,
		};

		memcpy(event.key_prefix, key_prefix, sizeof(event.key_prefix));
		rc = zbus_chan_pub(chan, &event, K_NO_WAIT);
		break;
	}
	default:
		rc = -EINVAL;
		break;
	}
	if (rc == 0) {
		LOG_DBG("Contact request published: kind=%u key_prefix=%02x%02x%02x",
			(unsigned int)type, key_prefix[0], key_prefix[1], key_prefix[2]);
		return 0;
	}

	LOG_WRN("Contact request publish failed: kind=%u rc=%d key_prefix=%02x%02x%02x",
		(unsigned int)type, rc, key_prefix[0], key_prefix[1], key_prefix[2]);

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	inflight = &contact_request_inflight[(size_t)type];
	contact_request_inflight_clear_if_match_locked(inflight, &snapshot);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	return rc;
}

static void contact_clear_request_inflight(enum contact_request_type kind, const uint8_t *key_prefix)
{
	if ((unsigned int)kind >= CONTACT_REQUEST_COUNT) {
		return;
	}
	if (key_prefix == NULL) {
		return;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	struct contact_request_inflight *inflight = &contact_request_inflight[(size_t)kind];
	if (inflight->active &&
	    memcmp(inflight->key_prefix, key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES) == 0) {
		contact_request_inflight_clear_locked(inflight);
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);
}

static uint32_t contact_request_tag_next(void)
{
	uint32_t tag = (uint32_t)atomic_inc(&contact_request_tag_counter);

	if (tag == 0U) {
		tag = (uint32_t)atomic_inc(&contact_request_tag_counter);
	}

	return tag;
}

static bool contact_handle_advert_response(
	const mbs_contact_response_advert_event *event)
{
	size_t slot_idx = 0U;
	bool changed = false;
	bool notify_changed = false;
	bool store_changed = false;

	if (event == NULL) {
		return false;
	}

	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		LOG_DBG("Reset cleanup in progress");
		k_mutex_unlock(&contact_store_writer_mutex);
		return true;
	}
	if (mbs_contact_is_shutting_down()) {
		LOG_DBG("Contact service stopping");
		k_mutex_unlock(&contact_store_writer_mutex);
		return true;
	}

	mbs_contact contact = meshbus_Contact_init_zero;
	bool is_new = false;
	uint32_t now_s;
	int rc = contact_find_by_key(event->public_key, &slot_idx, &contact);

	if (rc == -ENOENT) {
		is_new = true;
		if (!contact_advert_auto_add_allowed(event)) {
			k_mutex_unlock(&contact_store_writer_mutex);
			mbs_contact_publish_advert_event(event, true);
			return true;
		}
		rc = contact_identity_init(&contact, event->public_key, event->name,
					   event->role);
		if (rc != 0) {
			LOG_WRN("Advert response identity apply failed: rc=%d", rc);
			k_mutex_unlock(&contact_store_writer_mutex);
			return true;
		}
		if (event->has_position) {
			contact.latitude = event->latitude;
			contact.longitude = event->longitude;
		}
		rc = contact_update_known_path_fields_internal(&contact, event->has_out_path,
							   event->out_path_len,
							   event->path_hash_size, event->out_path,
							   &changed);
		if (rc != 0) {
			LOG_WRN("Advert response path apply failed: rc=%d", rc);
			k_mutex_unlock(&contact_store_writer_mutex);
			return true;
		}
		rc = 0;
	}
	if (rc != 0) {
		LOG_WRN("Advert response contact load failed: rc=%d", rc);
		k_mutex_unlock(&contact_store_writer_mutex);
		return true;
	}

	if (mbs_clock_timestamp_s_get(&now_s) != 0 || now_s == 0U) {
		now_s = 1U;
	}
	if (is_new) {
		contact.first_seen_timestamp = now_s;
		contact.last_seen_timestamp = now_s;
		if (event->has_response_snr) {
			contact.last_seen_snr = event->response_snr;
		}
		rc = contact_insert_locked(&contact, &slot_idx);
		store_changed = rc == 0;
	} else {
		rc = contact_update_advert_fields_internal(slot_idx, &contact, event, &changed);
		if (rc == 0) {
			notify_changed = changed;
			rc = contact_update_last_seen_internal(slot_idx, &contact, now_s,
							    event->has_response_snr,
							    event->response_snr, &changed);
			notify_changed = notify_changed || changed;
		}
		if (rc == 0 && notify_changed) {
			rc = contact_slot_persist_blob(slot_idx, &contact);
			store_changed = rc == 0;
		}
	}
	if (rc == -EEXIST || rc == -EADDRINUSE) {
		LOG_DBG("Advert create ignored: rc=%d", rc);
		k_mutex_unlock(&contact_store_writer_mutex);
		return true;
	}
	if (rc == -ENOSPC && is_new) {
		k_mutex_unlock(&contact_store_writer_mutex);
		mbs_contact_publish_advert_event(event, true);
		return true;
	}
	if (rc == -ENOENT && !is_new) {
		k_mutex_unlock(&contact_store_writer_mutex);
		return true;
	}
	if (rc != 0) {
		LOG_WRN("Advert response apply failed: is_new=%u rc=%d", is_new ? 1U : 0U, rc);
	}
	if (rc == 0 && event->raw_advert_len > 0U) {
		int raw_rc = contact_setting_save_advert_raw_by_id(
			slot_idx, event->raw_advert, event->raw_advert_len);

		if (raw_rc != 0) {
			LOG_WRN("Advert response raw persist failed: idx=%u rc=%d",
				(unsigned int)slot_idx, raw_rc);
		}
	}
	k_mutex_unlock(&contact_store_writer_mutex);

	if (rc == 0 && store_changed) {
		mbs_contact_publish_store_change(contact.public_key.bytes);
	}
	if (rc == 0) {
		mbs_contact_publish_advert_event(event, false);
	}
	return true;
}

static bool contact_handle_path_response(
	const mbs_contact_response_path_event *event)
{
	size_t slot_idx = 0U;
	bool changed = false;
	bool notify_changed = false;
	bool has_snr = false;
	int32_t seen_snr = 0;

	if (event == NULL) {
		return false;
	}

	LOG_DBG("Handle contact-path response: is_discover=%u prefix=%02x%02x%02x path_len=%u "
		  "has_out_path=%u out_path_snr_count=%u return_path_snr_count=%u",
		  event->is_discover ? 1U : 0U, event->key_prefix[0], event->key_prefix[1],
		  event->key_prefix[2], (unsigned int)event->out_path_len,
		  (unsigned int)event->has_out_path, (unsigned int)event->out_path_snr_count,
		  (unsigned int)event->return_path_snr_count);

	const uint8_t *key_prefix = event->key_prefix;
	mbs_contact contact = meshbus_Contact_init_zero;
	uint32_t seen_ts = event->timestamp;
	int rc;

	if (event->is_discover) {
		contact_clear_request_inflight(CONTACT_REQUEST_DISCOVER, key_prefix);
	}
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		LOG_DBG("Contact-path response ignored: reset cleanup in progress");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}
	if (mbs_contact_is_shutting_down()) {
		LOG_DBG("Contact-path response ignored: contact service stopping");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}

	rc = contact_find_by_prefix_internal(key_prefix, &slot_idx, &contact);
	if (rc == 0) {
		seen_ts = contact_timestamp_or_now(seen_ts);

		if (event->has_response_snr) {
			has_snr = true;
			seen_snr = event->response_snr;
		} else if (event->out_path_snr_count > 0U) {
			has_snr = true;
			seen_snr = event->out_path_snr[event->out_path_snr_count - 1U];
		}

		rc = contact_update_path_fields_internal(slot_idx, &contact, event, &changed);
		if (rc == 0) {
			notify_changed = changed;
			rc = contact_update_last_seen_internal(slot_idx, &contact, seen_ts, has_snr, seen_snr,
							    &changed);
			notify_changed = notify_changed || changed;
		}
		if (rc == 0 && notify_changed) {
			rc = contact_slot_persist_blob(slot_idx, &contact);
		}
		if (rc != 0 && rc != -ENOTSUP) {
			LOG_WRN("Contact-path response contact update failed: rc=%d", rc);
		}
	} else {
		LOG_WRN("Contact-path response contact not found: rc=%d prefix=%02x%02x%02x", rc,
			key_prefix[0], key_prefix[1], key_prefix[2]);
	}
	k_mutex_unlock(&contact_store_writer_mutex);

	if (event->is_discover && rc == 0 && notify_changed) {
		mbs_contact_publish_store_change(contact.public_key.bytes);
	}

	return true;
}

static bool contact_handle_trace_path_response(
	const mbs_contact_response_trace_path_event *event)
{
	size_t slot_idx = 0U;
	bool changed = false;
	bool has_snr = false;
	int32_t seen_snr = 0;

	if (event == NULL) {
		return false;
	}

	const uint8_t *key_prefix = event->key_prefix;
	mbs_contact contact = meshbus_Contact_init_zero;
	uint32_t seen_ts = event->timestamp;
	int rc;

	contact_clear_request_inflight(CONTACT_REQUEST_TRACE, key_prefix);
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		LOG_DBG("Trace response ignored: reset cleanup in progress");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}
	if (mbs_contact_is_shutting_down()) {
		LOG_DBG("Trace response ignored: contact service stopping");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}

	rc = contact_find_by_prefix_internal(key_prefix, &slot_idx, &contact);
	if (rc == 0) {
		seen_ts = contact_timestamp_or_now(seen_ts);

		if (event->has_response_snr) {
			has_snr = true;
			seen_snr = event->response_snr;
		} else if (event->out_path_snr_count > 0U) {
			has_snr = true;
			seen_snr = event->out_path_snr[event->out_path_snr_count - 1U];
		}

		rc = contact_update_last_seen_internal(slot_idx, &contact, seen_ts, has_snr, seen_snr,
						    &changed);
		if (rc == 0 && changed) {
			rc = contact_slot_persist_blob(slot_idx, &contact);
		}
		if (rc != 0 && rc != -ENOTSUP) {
			LOG_WRN("Trace response contact update failed: rc=%d", rc);
		}
	} else {
		LOG_WRN("Trace response contact not found: rc=%d prefix=%02x%02x%02x", rc,
			key_prefix[0], key_prefix[1], key_prefix[2]);
	}
	k_mutex_unlock(&contact_store_writer_mutex);
	return true;
}

static bool contact_handle_telemetry_response(
	const mbs_contact_response_telemetry_event *event)
{
	size_t slot_idx = 0U;
	bool changed = false;

	if (event == NULL) {
		return false;
	}

	LOG_DBG("Telemetry response: prefix=%02x%02x%02x tag=%u payload_len=%u timestamp=%u",
		  event->key_prefix[0], event->key_prefix[1], event->key_prefix[2],
		  event->tag, (unsigned int)event->payload_len, event->timestamp);

	const uint8_t *key_prefix = event->key_prefix;
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	contact_clear_request_inflight(CONTACT_REQUEST_TELEMETRY, key_prefix);
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		LOG_DBG("Telemetry response ignored: reset cleanup in progress");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}
	if (mbs_contact_is_shutting_down()) {
		LOG_DBG("Telemetry response ignored: contact service stopping");
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}

	rc = contact_find_by_prefix_internal(key_prefix, &slot_idx, &contact);
	if (rc == 0) {
		/*
		 * TODO: Parse Cayenne LPP telemetry payload in a meshbus-shared layer to refresh
		 * contact position. Zephyr does not provide a reusable Cayenne LPP parser for this path today.
		 */
		if (event->timestamp != 0U) {
			rc = contact_update_last_seen_internal(slot_idx, &contact, event->timestamp, false, 0,
							    &changed);
			if (rc == 0 && changed) {
				rc = contact_slot_persist_blob(slot_idx, &contact);
			}
		} else {
			rc = 0;
		}
		if (rc != 0 && rc != -ENOTSUP) {
			LOG_WRN("Telemetry response contact update failed: rc=%d", rc);
		}
	} else {
		LOG_WRN("Telemetry response contact not found: rc=%d prefix=%02x%02x%02x", rc,
			key_prefix[0], key_prefix[1], key_prefix[2]);
		k_mutex_unlock(&contact_store_writer_mutex);
		return false;
	}
	k_mutex_unlock(&contact_store_writer_mutex);
	return true;
}

static void contact_handle_binary_response(
	const mbs_contact_response_binary_event *event)
{
	size_t slot_idx = 0U;
	bool changed = false;

	if (event == NULL) {
		return;
	}

	LOG_DBG("Binary response: prefix=%02x%02x%02x tag=%u payload_len=%u timestamp=%u",
		event->key_prefix[0], event->key_prefix[1], event->key_prefix[2],
		event->tag, (unsigned int)event->payload_len, event->timestamp);

	const uint8_t *key_prefix = event->key_prefix;
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	contact_clear_request_inflight(CONTACT_REQUEST_BINARY, key_prefix);
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		LOG_DBG("Binary response ignored: reset cleanup in progress");
		k_mutex_unlock(&contact_store_writer_mutex);
		return;
	}
	if (mbs_contact_is_shutting_down()) {
		LOG_DBG("Binary response ignored: contact service stopping");
		k_mutex_unlock(&contact_store_writer_mutex);
		return;
	}

	rc = contact_find_by_prefix_internal(key_prefix, &slot_idx, &contact);
	if (rc == 0) {
		rc = contact_update_last_seen_internal(slot_idx, &contact, event->timestamp,
						    false, 0, &changed);
		if (rc == 0 && changed) {
			rc = contact_slot_persist_blob(slot_idx, &contact);
		}
		if (rc != 0 && rc != -ENOTSUP) {
			LOG_WRN("Binary response contact update failed: rc=%d", rc);
		}
	} else {
		LOG_WRN("Binary response contact not found: rc=%d prefix=%02x%02x%02x", rc,
			key_prefix[0], key_prefix[1], key_prefix[2]);
	}
	k_mutex_unlock(&contact_store_writer_mutex);
}

static int contact_next_by_hash_internal(const uint8_t *hash, size_t start_slot,
					 size_t *slot_id, mbs_contact *contact)
{
	if (hash == NULL || slot_id == NULL || contact == NULL) {
		return -EINVAL;
	}

	size_t cursor = start_slot;

	while (cursor < CONFIG_MBS_CONTACT_MAX_CONTACTS) {
		size_t matched_idx = CONFIG_MBS_CONTACT_MAX_CONTACTS;

		k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
		int16_t cur = contact_prefix_bucket_head[hash[0]];

		while (cur != CONTACT_PREFIX_BUCKET_INVALID) {
			size_t idx = (size_t)cur;

			if (idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS) {
				break;
			}
			cur = contact_slots[idx].bucket_next;

			if (idx < cursor || !contact_slots[idx].used ||
			    contact_slots[idx].prefix[0] != hash[0]) {
				continue;
			}

			matched_idx = idx;
			break;
		}
		k_mutex_unlock(&mbs_contact_settings_mutex);

		if (matched_idx >= CONFIG_MBS_CONTACT_MAX_CONTACTS) {
			return -ENOENT;
		}

		mbs_contact contact_record = meshbus_Contact_init_zero;
		int rc = contact_load_by_index(matched_idx, &contact_record);

		cursor = matched_idx + 1U;
		if (rc != 0 || contact_record.public_key.bytes[0] != hash[0]) {
			continue;
		}

		*slot_id = matched_idx;
		*contact = contact_record;
		return 0;
	}

	return -ENOENT;
}

static int contact_share_request_internal(const uint8_t *prefix)
{
	if (prefix == NULL) {
		return -EINVAL;
	}

	size_t slot_idx = 0U;
	int rc = 0;
	mbs_contact_share_request_event event = {0};
	size_t raw_len = 0U;

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return -ESHUTDOWN;
	}
	rc = contact_slot_find_by_prefix_locked(prefix, &slot_idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc != 0) {
		return rc;
	}

	rc = contact_setting_load_advert_raw_by_id(slot_idx, event.raw_advert, sizeof(event.raw_advert),
						&raw_len);
	if (rc != 0) {
		return rc;
	}

	memcpy(event.key_prefix, prefix, sizeof(event.key_prefix));
	event.raw_advert_len = (uint8_t)raw_len;
	rc = zbus_chan_pub(&mbs_contact_share_request_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_WRN("Contact advert request publish failed: prefix=%02x%02x%02x rc=%d",
			prefix[0], prefix[1], prefix[2], rc);
		return rc;
	}

	LOG_DBG("Contact advert request published: prefix=%02x%02x%02x raw_len=%u",
		prefix[0], prefix[1], prefix[2],
		(unsigned int)event.raw_advert_len);
	return 0;
}

static int contact_discover_path_request_internal(const uint8_t *prefix, uint32_t *out_tag)
{
	uint32_t tag;
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	tag = contact_request_tag_next();
	rc = contact_publish_request(CONTACT_REQUEST_DISCOVER,
				     &mbs_contact_discover_path_request_chan, prefix, tag);

	if (rc == 0 && out_tag != NULL) {
		*out_tag = tag;
	}

	return rc;
}

static int contact_trace_path_request_internal(const uint8_t *prefix, uint32_t *out_tag)
{
	uint32_t tag;
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	tag = contact_request_tag_next();
	rc = contact_publish_request(
		CONTACT_REQUEST_TRACE, &mbs_contact_trace_path_request_chan,
		prefix, tag);

	if (rc == 0 && out_tag != NULL) {
		*out_tag = tag;
	}

	return rc;
}

static int contact_telemetry_request_internal(const uint8_t *prefix, uint32_t *out_tag)
{
	uint32_t tag;
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}

	tag = contact_request_tag_next();
	rc = contact_publish_request(CONTACT_REQUEST_TELEMETRY,
				     &mbs_contact_telemetry_request_chan, prefix, tag);

	if (rc == 0 && out_tag != NULL) {
		*out_tag = tag;
	}

	return rc;
}

static int contact_binary_request_internal(const uint8_t *prefix, const uint8_t *payload,
					   size_t payload_len, uint32_t *out_tag)
{
	mbs_contact_binary_request_event event = {0};
	mbs_contact contact = meshbus_Contact_init_zero;
	struct contact_request_inflight snapshot = {0};
	struct contact_request_inflight *inflight;
	int64_t now_ms;
	uint32_t tag;
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (prefix == NULL || payload == NULL || payload_len == 0U ||
	    payload_len > sizeof(event.payload)) {
		return -EINVAL;
	}

	rc = mbs_contact_find_by_prefix(prefix, &contact);
	if (rc != 0) {
		return rc;
	}

	now_ms = k_uptime_get();
	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return -ESHUTDOWN;
	}

	inflight = &contact_request_inflight[CONTACT_REQUEST_BINARY];
	if (inflight->active) {
		if (now_ms <= inflight->deadline_ms) {
			k_mutex_unlock(&mbs_contact_settings_mutex);
			return -EBUSY;
		}
		contact_request_inflight_clear_locked(inflight);
	}

	inflight->active = true;
	memcpy(inflight->key_prefix, prefix, sizeof(inflight->key_prefix));
	inflight->deadline_ms = now_ms + (int64_t)CONFIG_MBS_CONTACT_REQUEST_TIMEOUT_MS;
	snapshot = *inflight;
	k_mutex_unlock(&mbs_contact_settings_mutex);

	tag = contact_request_tag_next();

	memcpy(event.key_prefix, prefix, sizeof(event.key_prefix));
	event.tag = tag;
	event.payload_len = (uint8_t)payload_len;
	memcpy(event.payload, payload, payload_len);

	rc = zbus_chan_pub(&mbs_contact_binary_request_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
		inflight = &contact_request_inflight[CONTACT_REQUEST_BINARY];
		contact_request_inflight_clear_if_match_locked(inflight, &snapshot);
		k_mutex_unlock(&mbs_contact_settings_mutex);
		return rc;
	}

	if (out_tag != NULL) {
		*out_tag = tag;
	}
	return 0;
}

static int contact_get_internal(size_t index, mbs_contact *contact)
{
	if (contact == NULL) {
		return -EINVAL;
	}
	if (index >= CONFIG_MBS_CONTACT_MAX_CONTACTS) {
		return -ENOENT;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	bool slot_used = contact_slots[index].used;
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (!slot_used) {
		return -ENOENT;
	}

	return contact_load_by_index(index, contact);
}

static int contact_set_internal(const uint8_t *public_key_prefix, const mbs_contact *contact)
{
	size_t idx = 0U;
	bool changed = false;
	int rc;

	if (public_key_prefix == NULL || contact == NULL) {
		return -EINVAL;
	}
	if (contact_persistent_validate(contact) != 0) {
		return -EINVAL;
	}
	if (memcmp(contact->public_key.bytes, public_key_prefix,
		   CONFIG_MBS_CONTACT_PREFIX_BYTES) != 0) {
		return -EINVAL;
	}
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		rc = -EBUSY;
		goto out;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		rc = -ESHUTDOWN;
		goto out;
	}
	rc = contact_slot_find_by_prefix_locked(public_key_prefix, &idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc == 0) {
		rc = contact_apply_update_locked(idx, contact, true, &changed);
		goto out;
	}
	if (rc != -ENOENT) {
		goto out;
	}

	rc = contact_insert_locked(contact, NULL);
	changed = rc == 0;

out:
	k_mutex_unlock(&contact_store_writer_mutex);
	if (rc == 0 && changed) {
		mbs_contact_publish_store_change(contact->public_key.bytes);
	}
	return rc;
}

static int contact_reset_internal(const uint8_t *public_key_prefix)
{
	size_t idx = 0U;
	bool changed = false;
	int rc;

	if (public_key_prefix == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	if (is_contact_resetting()) {
		rc = -EBUSY;
		goto out;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	if (mbs_contact_is_shutting_down()) {
		k_mutex_unlock(&mbs_contact_settings_mutex);
		rc = -ESHUTDOWN;
		goto out;
	}
	rc = contact_slot_find_by_prefix_locked(public_key_prefix, &idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	if (rc == -ENOENT) {
		rc = 0;
		goto out;
	}
	if (rc != 0) {
		goto out;
	}

	rc = contact_setting_delete_slot_by_id(idx);
	if (rc != 0) {
		LOG_WRN("Contact reset settings cleanup failed: idx=%u rc=%d",
			(unsigned int)idx, rc);
		goto out;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	contact_slot_clear_by_id(idx);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	changed = true;

out:
	k_mutex_unlock(&contact_store_writer_mutex);
	if (rc == 0 && changed) {
		LOG_INF("Contact reset: idx=%u", (unsigned int)idx);
		mbs_contact_publish_store_change(public_key_prefix);
	}
	return rc;
}

static uint8_t contact_count_get(void)
{
	uint8_t count;

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	count = contact_count;
	k_mutex_unlock(&mbs_contact_settings_mutex);

	return count;
}

static uint8_t contact_size_get(void)
{
	return CONFIG_MBS_CONTACT_MAX_CONTACTS;
}

static void contact_shutdown(void)
{
	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	for (size_t i = 0; i < CONTACT_REQUEST_COUNT; i++) {
		contact_request_inflight_clear_locked(&contact_request_inflight[i]);
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);
}

static void contact_runtime_init(void)
{
	k_work_init_delayable(&reset_contact_work, reset_contact_work_handler);

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	atomic_clear(&contact_resetting);
	contact_prefix_bucket_reset();
	k_mutex_unlock(&mbs_contact_settings_mutex);
}

static int contact_settings_set(const char *name, size_t len, settings_read_cb read_cb,
				void *cb_arg)
{
	int rc;

	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	rc = contact_slots_build(name, len, read_cb, cb_arg);
	k_mutex_unlock(&contact_store_writer_mutex);

	return (rc == -ENOENT) ? 0 : rc;
}

static int contact_settings_commit(void)
{
	/*
	 * settings_load_subtree() invokes commit only after every h_set callback has
	 * completed.  Serialize with ordinary Contact writers so each legacy slot is
	 * reloaded, sanitized by contact_load_by_index(), and rewritten atomically.
	 */
	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		mbs_contact contact = meshbus_Contact_init_zero;
		bool pending;
		int rc;

		k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
		pending = contact_secret_sanitize_pending_get_locked(i);
		k_mutex_unlock(&mbs_contact_settings_mutex);
		if (!pending) {
			continue;
		}

		rc = contact_load_by_index(i, &contact);
		if (rc == 0) {
			rc = contact_slot_persist_blob(i, &contact);
		}
		if (rc != 0) {
			LOG_WRN("Legacy contact password cleanup failed: idx=%u rc=%d",
				(unsigned int)i, rc);
		}
	}
	k_mutex_unlock(&contact_store_writer_mutex);

	/* A cleanup failure must not discard the restored, sanitized in-memory view. */
	return 0;
}

static int contact_slot_export_sanitized(
	size_t slot_idx, int (*export_func)(const char *name, const void *val, size_t val_len),
	uint8_t *buffer, size_t buffer_size)
{
	char slot_key[sizeof("4294967295")];
	mbs_contact contact = meshbus_Contact_init_zero;
	struct mbs_settings_blob_schema schema = {
		.subtree = MBS_CONTACT_SETTINGS_SUBTREE,
		.key = slot_key,
		.fields = meshbus_Contact_fields,
		.message_size = sizeof(contact),
	};
	int rc;

	rc = snprintk(slot_key, sizeof(slot_key), "%u", (unsigned int)slot_idx);
	if (rc < 0 || (size_t)rc >= sizeof(slot_key)) {
		return -ERANGE;
	}
	rc = contact_load_by_index(slot_idx, &contact);
	if (rc != 0) {
		return rc;
	}

	return mbs_settings_blob_export_with_buffer(&schema, &contact, buffer, buffer_size,
						   export_func);
}

static int contact_settings_export(int (*export_func)(const char *name, const void *val,
						      size_t val_len))
{
	bool used_slots[CONFIG_MBS_CONTACT_MAX_CONTACTS] = {0};
	uint8_t contact_buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
#if defined(CONFIG_MBS_CONTACT_ADVERT_RAW_STORE)
	uint8_t raw_buffer[MBS_CONTACT_ADVERT_RAW_MAX_LEN];
#endif
	int rc;

	if (export_func == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		used_slots[i] = contact_slots[i].used;
	}
	k_mutex_unlock(&mbs_contact_settings_mutex);

	for (size_t i = 0; i < CONFIG_MBS_CONTACT_MAX_CONTACTS; i++) {
		if (!used_slots[i]) {
			continue;
		}

		rc = contact_slot_export_sanitized(i, export_func, contact_buffer,
						   sizeof(contact_buffer));
		if (rc != 0) {
			return rc;
		}

#if defined(CONFIG_MBS_CONTACT_ADVERT_RAW_STORE)
		rc = mbs_settings_indexed_raw_export(&contact_advert_raw_settings_schema, i,
						    export_func, raw_buffer, sizeof(raw_buffer));
		if (rc != 0) {
			return rc;
		}
#endif
	}

	return 0;
}


/* -------------------------------------------------------------------------- */
/* Settings Schema                                                            */
/* -------------------------------------------------------------------------- */

static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}

	return contact_settings_set(name, len, read_cb, cb_arg);
}

static int settings_handle_export(int (*export_func)(const char *name, const void *val,
						      size_t val_len))
{
	if (export_func == NULL) {
		return -EINVAL;
	}

	return contact_settings_export(export_func);
}

SETTINGS_STATIC_HANDLER_DEFINE(mbs_contact, MBS_CONTACT_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, contact_settings_commit,
			       settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Notify Helpers                                                             */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_MBS_NOTIFY)
static void contact_notify_copy_bytes(pb_bytes_array_t *dst, size_t dst_capacity,
				      const uint8_t *src, size_t src_len)
{
	size_t n;

	if (dst == NULL) {
		return;
	}

	n = (src == NULL) ? 0U : MIN(src_len, dst_capacity);
	dst->size = (pb_size_t)n;
	for (size_t i = 0; i < n; i++) {
		dst->bytes[i] = src[i];
	}
}

static void contact_notify_copy_snr_q4(int32_t *dst, pb_size_t *dst_count,
				       const int8_t *src, uint8_t src_count)
{
	if (dst == NULL || dst_count == NULL) {
		return;
	}

	size_t n = 0U;
	if (src != NULL) {
		n = MIN((size_t)src_count, (size_t)MBS_CONTACT_OUTPATH_MAX_LEN);
	}

	for (size_t i = 0; i < n; i++) {
		dst[i] = (int32_t)src[i] * 4;
	}
	*dst_count = (pb_size_t)n;
}

static void publish_notify(mbs_notify_type type, const mbs_notify *payload)
{
	if (payload == NULL) {
		return;
	}

	int rc = mbs_notify_publish(type, payload);
	if (rc != 0) {
		LOG_DBG("Contact notify publish failed: type=%u rc=%d",
			(unsigned int)type, rc);
	}
}

static void publish_contact_changed_notify(const uint8_t *prefix)
{
	mbs_notify payload = meshbus_Notify_init_zero;

	payload.which_payload_variant = MBS_NOTIFY_TAG_NODE;
	if (prefix != NULL) {
		payload.payload_variant.node.has_public_key_prefix = true;
		contact_notify_copy_bytes(
			(pb_bytes_array_t *)&payload.payload_variant.node.public_key_prefix,
			CONFIG_MBS_CONTACT_PREFIX_BYTES, prefix,
			CONFIG_MBS_CONTACT_PREFIX_BYTES);
	}
	publish_notify(MBS_NOTIFY_TYPE_NODES_CHANGED, &payload);
}

static void publish_contact_advert_notify(const mbs_contact_response_advert_event *event)
{
	mbs_notify payload = meshbus_Notify_init_zero;

	if (event == NULL) {
		return;
	}

	payload.which_payload_variant = MBS_NOTIFY_TAG_NODE_ADVERT;
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_advert.public_key,
		ARRAY_SIZE(payload.payload_variant.node_advert.public_key.bytes),
		event->public_key, sizeof(event->public_key));
	payload.payload_variant.node_advert.response_snr =
		event->has_response_snr ? event->response_snr : 0;
	payload.payload_variant.node_advert.has_position = event->has_position;
	if (event->has_position) {
		payload.payload_variant.node_advert.latitude = event->latitude;
		payload.payload_variant.node_advert.longitude = event->longitude;
	}
	strncpy(payload.payload_variant.node_advert.name, event->name,
		sizeof(payload.payload_variant.node_advert.name) - 1U);
	payload.payload_variant.node_advert.name[
		sizeof(payload.payload_variant.node_advert.name) - 1U] = '\0';
	payload.payload_variant.node_advert.role = (uint32_t)event->role;
	payload.payload_variant.node_advert.is_new = event->is_new;
	payload.payload_variant.node_advert.advert_timestamp = event->advert_timestamp;
	payload.payload_variant.node_advert.has_out_path = event->has_out_path;
	payload.payload_variant.node_advert.path_hash_size = event->path_hash_size;
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_advert.out_path,
		ARRAY_SIZE(payload.payload_variant.node_advert.out_path.bytes),
		event->out_path, event->out_path_len);

	publish_notify(MBS_NOTIFY_TYPE_NODE_ADVERT, &payload);
}

static void publish_contact_discover_notify(const mbs_contact_response_discover_event *event)
{
	mbs_notify payload = meshbus_Notify_init_zero;

	if (event == NULL) {
		return;
	}

	payload.which_payload_variant = MBS_NOTIFY_TAG_NODE_DISCOVER;
	payload.payload_variant.node_discover.tag = event->tag;
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_discover.public_key,
		ARRAY_SIZE(payload.payload_variant.node_discover.public_key.bytes),
		event->public_key, sizeof(event->public_key));
	payload.payload_variant.node_discover.role = (uint32_t)event->role;
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_discover.path,
		ARRAY_SIZE(payload.payload_variant.node_discover.path.bytes),
		event->path, event->path_len);
	payload.payload_variant.node_discover.uplink_snr = event->uplink_snr;
	payload.payload_variant.node_discover.downlink_snr = event->downlink_snr;

	publish_notify(MBS_NOTIFY_TYPE_NODE_DISCOVER, &payload);
}
#endif

/* -------------------------------------------------------------------------- */
/* Request And Response Handlers                                              */
/* -------------------------------------------------------------------------- */

static void mbs_contact_publish_store_change(const uint8_t *prefix)
{
	mbs_contact_store_change_event event = {0};
	int rc;

	if (prefix == NULL) {
		return;
	}
	memcpy(event.key_prefix, prefix, sizeof(event.key_prefix));

	rc = zbus_chan_pub(&mbs_contact_store_change_chan, &event, K_NO_WAIT);
	if (rc != 0) {
		LOG_DBG("Contact store change publish failed: rc=%d", rc);
	}

#if defined(CONFIG_MBS_NOTIFY)
	publish_contact_changed_notify(prefix);
#endif
}

static void mbs_contact_publish_advert_event(const mbs_contact_response_advert_event *event,
						 bool is_new)
{
	mbs_contact_response_advert_event adjusted;
	int rc;

	if (event == NULL) {
		return;
	}

	adjusted = *event;
	adjusted.is_new = is_new;

	rc = zbus_chan_pub(&mbs_contact_advert_chan, &adjusted, K_NO_WAIT);
	if (rc != 0) {
		LOG_DBG("Contact advert event publish failed: rc=%d", rc);
	}

#if defined(CONFIG_MBS_NOTIFY)
	publish_contact_advert_notify(&adjusted);
#endif
}

static void handle_advert_response(const mbs_contact_response_advert_event *event)
{
	if (event == NULL) {
		return;
	}
	if (!contact_handle_advert_response(event)) {
		mbs_contact_publish_advert_event(event, true);
	}
}

static void handle_path_response(const mbs_contact_response_path_event *event)
{
	if (event == NULL) {
		return;
	}
	if (!contact_handle_path_response(event)) {
		return;
	}

#if defined(CONFIG_MBS_NOTIFY)
	if (event->is_discover) {
		mbs_notify payload = meshbus_Notify_init_zero;
		pb_bytes_array_t *public_key_prefix =
			(pb_bytes_array_t *)
				&payload.payload_variant.node_discover_path.public_key_prefix;

		payload.which_payload_variant = MBS_NOTIFY_TAG_NODE_DISCOVER_PATH;
		contact_notify_copy_bytes(public_key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES,
					  event->key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES);
		contact_notify_copy_bytes(
			(pb_bytes_array_t *)&payload.payload_variant.node_discover_path.out_path,
			MBS_CONTACT_OUTPATH_MAX_LEN, event->out_path, event->out_path_len);
		contact_notify_copy_snr_q4(
			payload.payload_variant.node_discover_path.out_path_snr,
			&payload.payload_variant.node_discover_path.out_path_snr_count,
			event->out_path_snr, event->out_path_snr_count);
		contact_notify_copy_snr_q4(
			payload.payload_variant.node_discover_path.return_path_snr,
			&payload.payload_variant.node_discover_path.return_path_snr_count,
			event->return_path_snr, event->return_path_snr_count);
		payload.payload_variant.node_discover_path.tag = event->tag;
		publish_notify(MBS_NOTIFY_TYPE_NODE_DISCOVER_PATH, &payload);
	}
#endif
}

static void handle_discover_response(const mbs_contact_response_discover_event *event)
{
	if (event == NULL) {
		return;
	}

#if defined(CONFIG_MBS_NOTIFY)
	publish_contact_discover_notify(event);
#endif
}

static void handle_trace_path_response(const mbs_contact_response_trace_path_event *event)
{
	if (event == NULL) {
		return;
	}
	if (!contact_handle_trace_path_response(event)) {
		return;
	}

#if defined(CONFIG_MBS_NOTIFY)
	mbs_notify payload = meshbus_Notify_init_zero;
	pb_bytes_array_t *public_key_prefix =
		(pb_bytes_array_t *)&payload.payload_variant.node_trace_path.public_key_prefix;

	payload.which_payload_variant = MBS_NOTIFY_TAG_NODE_TRACE_PATH;
	contact_notify_copy_bytes(public_key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES,
				  event->key_prefix, CONFIG_MBS_CONTACT_PREFIX_BYTES);
	payload.payload_variant.node_trace_path.state = event->state;
	contact_notify_copy_snr_q4(payload.payload_variant.node_trace_path.out_path_snr,
				   &payload.payload_variant.node_trace_path.out_path_snr_count,
				   event->out_path_snr, event->out_path_snr_count);
	contact_notify_copy_snr_q4(
		payload.payload_variant.node_trace_path.return_path_snr,
		&payload.payload_variant.node_trace_path.return_path_snr_count,
		event->return_path_snr, event->return_path_snr_count);
	payload.payload_variant.node_trace_path.tag = event->tag;
	publish_notify(MBS_NOTIFY_TYPE_NODE_TRACE_PATH, &payload);
#endif
}

static void handle_telemetry_response(const mbs_contact_response_telemetry_event *event)
{
	if (event == NULL) {
		return;
	}
	if (!contact_handle_telemetry_response(event)) {
		return;
	}

#if defined(CONFIG_MBS_NOTIFY)
	mbs_notify payload = meshbus_Notify_init_zero;
	size_t payload_len = MIN((size_t)event->payload_len, sizeof(event->payload));

	payload.which_payload_variant = MBS_NOTIFY_TAG_NODE_TELEMETRY;
	payload.payload_variant.node_telemetry.tag = event->tag;
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_telemetry.public_key_prefix,
		CONFIG_MBS_CONTACT_PREFIX_BYTES, event->key_prefix,
		CONFIG_MBS_CONTACT_PREFIX_BYTES);
	contact_notify_copy_bytes(
		(pb_bytes_array_t *)&payload.payload_variant.node_telemetry.payload,
		MBS_CONTACT_TELEMETRY_PAYLOAD_MAX_LEN, event->payload, payload_len);
	publish_notify(MBS_NOTIFY_TYPE_NODE_TELEMETRY, &payload);
#endif
}

static void handle_binary_response(const mbs_contact_response_binary_event *event)
{
	contact_handle_binary_response(event);
}

static void mbs_contact_response_cb(const struct zbus_channel *chan)
{
	if (chan == NULL) {
		return;
	}

	if (chan == &mbs_contact_advert_response_chan) {
		const mbs_contact_response_advert_event *event = zbus_chan_const_msg(chan);
		handle_advert_response(event);
		return;
	}

	if (chan == &mbs_contact_path_response_chan) {
		const mbs_contact_response_path_event *event = zbus_chan_const_msg(chan);
		handle_path_response(event);
		return;
	}

	if (chan == &mbs_contact_discover_response_chan) {
		const mbs_contact_response_discover_event *event = zbus_chan_const_msg(chan);
		handle_discover_response(event);
		return;
	}

	if (chan == &mbs_contact_trace_path_response_chan) {
		const mbs_contact_response_trace_path_event *event = zbus_chan_const_msg(chan);
		handle_trace_path_response(event);
		return;
	}

	if (chan == &mbs_contact_telemetry_response_chan) {
		const mbs_contact_response_telemetry_event *event = zbus_chan_const_msg(chan);
		handle_telemetry_response(event);
		return;
	}

	if (chan == &mbs_contact_binary_response_chan) {
		const mbs_contact_response_binary_event *event = zbus_chan_const_msg(chan);
		handle_binary_response(event);
		return;
	}
}

ZBUS_LISTENER_DEFINE(mbs_contact_response_listener, mbs_contact_response_cb);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

static bool mbs_contact_is_shutting_down(void)
{
	return atomic_get(&shutting_down) != 0;
}

int mbs_contact_find_by_key(const uint8_t *public_key, mbs_contact *contact)
{
	if (public_key == NULL || contact == NULL) {
		return -EINVAL;
	}
	return contact_find_by_key(public_key, NULL, contact);
}

int mbs_contact_find_by_prefix(const uint8_t *prefix, mbs_contact *contact)
{
	if (prefix == NULL || contact == NULL) {
		return -EINVAL;
	}
	return contact_find_by_prefix_internal(prefix, NULL, contact);
}

int mbs_contact_next_by_hash(const uint8_t *hash, size_t start_slot, size_t *slot_id,
				 mbs_contact *contact)
{
	if (hash == NULL || slot_id == NULL || contact == NULL) {
		return -EINVAL;
	}
	return contact_next_by_hash_internal(hash, start_slot, slot_id, contact);
}

int mbs_contact_share_request(const uint8_t *prefix)
{
	if (prefix == NULL) {
		return -EINVAL;
	}
	return contact_share_request_internal(prefix);
}

int mbs_contact_discover_path_request(const uint8_t *prefix, uint32_t *out_tag)
{
	if (prefix == NULL) {
		if (out_tag != NULL) {
			*out_tag = 0U;
		}
		return -EINVAL;
	}
	return contact_discover_path_request_internal(prefix, out_tag);
}

int mbs_contact_trace_path_request(const uint8_t *prefix, uint32_t *out_tag)
{
	if (prefix == NULL) {
		if (out_tag != NULL) {
			*out_tag = 0U;
		}
		return -EINVAL;
	}
	return contact_trace_path_request_internal(prefix, out_tag);
}

int mbs_contact_telemetry_request(const uint8_t *prefix, uint32_t *out_tag)
{
	if (prefix == NULL) {
		if (out_tag != NULL) {
			*out_tag = 0U;
		}
		return -EINVAL;
	}
	return contact_telemetry_request_internal(prefix, out_tag);
}

int mbs_contact_binary_request(const uint8_t *prefix, const uint8_t *payload,
				   size_t payload_len, uint32_t *out_tag)
{
	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (prefix == NULL || payload == NULL || payload_len == 0U ||
	    payload_len > MBS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN) {
		return -EINVAL;
	}
	return contact_binary_request_internal(prefix, payload, payload_len, out_tag);
}

int mbs_contact_get(size_t index, mbs_contact *contact)
{
	if (contact == NULL) {
		return -EINVAL;
	}
	return contact_get_internal(index, contact);
}

int mbs_contact_set(const uint8_t *public_key_prefix, const mbs_contact *contact)
{
	if (public_key_prefix == NULL || contact == NULL) {
		return -EINVAL;
	}
	return contact_set_internal(public_key_prefix, contact);
}

int mbs_contact_insert(const uint8_t *public_key, const char *name,
			   mbs_contact_role role)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	uint32_t now_s = contact_timestamp_or_now(0U);
	int rc;

	rc = contact_identity_init(&contact, public_key, name, role);
	if (rc != 0) {
		return rc;
	}
	contact.first_seen_timestamp = now_s;
	contact.last_seen_timestamp = now_s;

	k_mutex_lock(&contact_store_writer_mutex, K_FOREVER);
	rc = contact_insert_locked(&contact, NULL);
	k_mutex_unlock(&contact_store_writer_mutex);
	if (rc == 0) {
		mbs_contact_publish_store_change(contact.public_key.bytes);
	}
	return rc;
}

int mbs_contact_reset(const uint8_t *public_key_prefix)
{
	if (public_key_prefix == NULL) {
		return -EINVAL;
	}
	return contact_reset_internal(public_key_prefix);
}

uint8_t mbs_contact_store_count(void)
{
	return contact_count_get();
}

uint8_t mbs_contact_store_size(void)
{
	return contact_size_get();
}

/* -------------------------------------------------------------------------- */
/* Power Callback                                                             */
/* -------------------------------------------------------------------------- */

static void mbs_power_contact_cb(enum mbs_power_action action, void *user_data)
{
	ARG_UNUSED(user_data);

	if (action != MBS_POWER_ACTION_SHUTDOWN && action != MBS_POWER_ACTION_REBOOT) {
		return;
	}

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	atomic_set(&shutting_down, 1);
	k_mutex_unlock(&mbs_contact_settings_mutex);
	contact_shutdown();

	LOG_INF("Contact service stopping: action=%u", (unsigned int)action);
}
MBS_POWER_ACTION_CALLBACK_DEFINE(mbs_power_contact_cb, NULL);

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

static int mbs_contact_init(void)
{
	int rc;

	LOG_DBG("Initializing meshbus contact service");

	contact_runtime_init();

	k_mutex_lock(&mbs_contact_settings_mutex, K_FOREVER);
	atomic_clear(&shutting_down);
	k_mutex_unlock(&mbs_contact_settings_mutex);

	rc = zbus_chan_add_obs(&mbs_contact_advert_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: advert rc=%d", rc);
	}
	rc = zbus_chan_add_obs(&mbs_contact_path_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: path rc=%d", rc);
	}
	rc = zbus_chan_add_obs(&mbs_contact_discover_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: discover rc=%d", rc);
	}
	rc = zbus_chan_add_obs(&mbs_contact_trace_path_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: trace_path rc=%d", rc);
	}
	rc = zbus_chan_add_obs(&mbs_contact_telemetry_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: telemetry rc=%d", rc);
	}
	rc = zbus_chan_add_obs(&mbs_contact_binary_response_chan,
			       &mbs_contact_response_listener, K_NO_WAIT);
	if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
		LOG_WRN("Contact response subscribe failed: binary rc=%d", rc);
	}

	rc = settings_load_subtree(MBS_CONTACT_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("Failed to load contact settings: %d", rc);
	}

	LOG_INF("Meshbus contact service ready: contacts=%u/%u",
		(unsigned int)mbs_contact_store_count(),
		(unsigned int)mbs_contact_store_size());
	return 0;
}

SYS_INIT(mbs_contact_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
