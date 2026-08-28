/*
 * Copyright (c) 2025 FoBE Projects
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <psa/crypto.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/channel.h>
#if defined(CONFIG_MESHBUS_NOTIFY)
#include <zephyr/meshbus/notify.h>
#endif
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "common/settings.h"

LOG_MODULE_REGISTER(meshbus_channel, CONFIG_MESHBUS_CHANNEL_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
#define MESHBUS_CHANNEL_SETTINGS_SUBTREE "meshbus/channel"
#define PREFIX_BUCKET_COUNT              256
#define PREFIX_BUCKET_INVALID            (-1)
#define CHANNEL_HASH_SIZE                1U
#define CHANNEL_SHA256_SIZE              32U

BUILD_ASSERT(CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES >= 3 &&
		     CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES <= 5,
	     "CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES must be in [3,5]");

enum channel_slot_state {
	CHANNEL_SLOT_EMPTY,
	CHANNEL_SLOT_BUSY,
	CHANNEL_SLOT_READY,
};

struct channel_slot_meta {
	enum channel_slot_state state;
	int16_t bucket_next;
	uint8_t prefix[CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES];
	uint8_t hash;
};

MB_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(channel_settings_schema, MESHBUS_CHANNEL_SETTINGS_SUBTREE,
				       NULL, meshbus_Channel, meshbus_Channel);

/*
 * Locking:
 * - channel_mutex protects in-memory slot metadata and slot counters.
 * - channel_mutation_busy is protected by channel_mutex; it serializes set/reset
 *   transactions without holding a mutex across settings writes.
 * - settings reads/writes are performed without holding channel_mutex.
 * - CHANNEL_SLOT_BUSY reserves a slot/prefix while set/reset updates settings.
 */
static struct channel_slot_meta channel_slots[CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS];
static int16_t channel_prefix_bucket_head[PREFIX_BUCKET_COUNT];
static uint8_t channel_count;
static bool channel_mutation_busy;

static K_MUTEX_DEFINE(channel_mutex);

/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
/* -------------------------------------------------------------------------- */
static int channel_load_by_idx(size_t idx, meshbus_Channel *channel);
static int channel_get_by_idx(size_t idx, meshbus_channel *channel);

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static bool channel_secret_size_valid(size_t secret_size)
{
	return (secret_size == MESHBUS_CHANNEL_SECRET_DEFAULT_LEN) ||
	       (secret_size == MESHBUS_CHANNEL_SECRET_SIZE);
}

static bool channel_pb_valid(const meshbus_Channel *channel)
{
	if (channel == NULL) {
		return false;
	}
	if (channel->hash.size != 1U) {
		return false;
	}
	if (!channel_secret_size_valid(channel->secret.size) ||
	    channel->secret.size > sizeof(channel->secret.bytes)) {
		return false;
	}

	return true;
}

static int channel_copy_out(const meshbus_Channel *src, meshbus_channel *channel)
{
	if (src == NULL || channel == NULL || !channel_pb_valid(src)) {
		return -EINVAL;
	}

	*channel = *src;
	channel->name[sizeof(channel->name) - 1U] = '\0';

	return 0;
}

static bool channel_secret_matches(const meshbus_Channel *channel, const uint8_t *secret,
				   size_t secret_len)
{
	if (channel == NULL || secret == NULL) {
		return false;
	}
	if (channel->secret.size != secret_len) {
		return false;
	}

	return memcmp(channel->secret.bytes, secret, secret_len) == 0;
}

static void channel_assign_default_name(meshbus_Channel *channel)
{
	if (channel == NULL) {
		return;
	}

	if (channel->name[0] != '\0') {
		channel->name[sizeof(channel->name) - 1U] = '\0';
		return;
	}

	(void)snprintk(channel->name, sizeof(channel->name), "%02X%02X", channel->secret.bytes[0],
		       channel->secret.bytes[1]);
}

static void channel_notify_publish_changed(const uint8_t *secret_prefix)
{
#if defined(CONFIG_MESHBUS_NOTIFY)
	meshbus_notify payload = meshbus_Notify_init_zero;
	int rc;

	payload.which_payload_variant = MESHBUS_NOTIFY_TAG_CHANNEL;
	if (secret_prefix != NULL) {
		pb_bytes_array_t *prefix =
			(pb_bytes_array_t *)&payload.payload_variant.channel.secret_prefix;

		payload.payload_variant.channel.has_secret_prefix = true;
		prefix->size = CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES;
		memcpy(prefix->bytes, secret_prefix, CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES);
	}

	rc = meshbus_notify_publish(MESHBUS_NOTIFY_TYPE_CHANNELS_CHANGED, &payload);
	if (rc != 0) {
		LOG_WRN("Channel notify publish failed: rc=%d", rc);
	}
#else
	ARG_UNUSED(secret_prefix);
#endif
}

static int channel_secret_hash(const uint8_t *secret, size_t secret_len,
			       uint8_t hash[CHANNEL_HASH_SIZE])
{
	uint8_t full_hash[CHANNEL_SHA256_SIZE];
	size_t full_hash_len;
	psa_status_t status;

	if (secret == NULL || hash == NULL) {
		return -EINVAL;
	}

	status = psa_hash_compute(PSA_ALG_SHA_256, secret, secret_len, full_hash,
				  sizeof(full_hash), &full_hash_len);
	if (status != PSA_SUCCESS || full_hash_len < CHANNEL_HASH_SIZE) {
		memset(full_hash, 0, sizeof(full_hash));
		return -EIO;
	}

	memcpy(hash, full_hash, CHANNEL_HASH_SIZE);
	memset(full_hash, 0, sizeof(full_hash));
	return 0;
}

static void channel_prefix_bucket_remove(size_t idx)
{
	int16_t prev = PREFIX_BUCKET_INVALID;
	int16_t cur;
	uint8_t bucket;

	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS ||
	    channel_slots[idx].state == CHANNEL_SLOT_EMPTY) {
		return;
	}

	bucket = channel_slots[idx].prefix[0];
	cur = channel_prefix_bucket_head[bucket];
	while (cur != PREFIX_BUCKET_INVALID) {
		if ((size_t)cur == idx) {
			if (prev == PREFIX_BUCKET_INVALID) {
				channel_prefix_bucket_head[bucket] = channel_slots[cur].bucket_next;
			} else {
				channel_slots[prev].bucket_next = channel_slots[cur].bucket_next;
			}
			channel_slots[idx].bucket_next = PREFIX_BUCKET_INVALID;
			return;
		}
		prev = cur;
		cur = channel_slots[cur].bucket_next;
	}
}

static void channel_prefix_bucket_insert(size_t idx)
{
	int16_t prev = PREFIX_BUCKET_INVALID;
	int16_t cur;
	uint8_t bucket;

	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS ||
	    channel_slots[idx].state == CHANNEL_SLOT_EMPTY) {
		return;
	}

	bucket = channel_slots[idx].prefix[0];
	cur = channel_prefix_bucket_head[bucket];
	while (cur != PREFIX_BUCKET_INVALID && (size_t)cur < idx) {
		prev = cur;
		cur = channel_slots[cur].bucket_next;
	}

	if (prev == PREFIX_BUCKET_INVALID) {
		channel_slots[idx].bucket_next = channel_prefix_bucket_head[bucket];
		channel_prefix_bucket_head[bucket] = (int16_t)idx;
		return;
	}

	channel_slots[idx].bucket_next = channel_slots[prev].bucket_next;
	channel_slots[prev].bucket_next = (int16_t)idx;
}

static int channel_slot_find_by_prefix_locked(const uint8_t *prefix, size_t *idx_out)
{
	int16_t cur;

	if (prefix == NULL || idx_out == NULL) {
		return -EINVAL;
	}

	cur = channel_prefix_bucket_head[prefix[0]];
	while (cur != PREFIX_BUCKET_INVALID) {
		size_t idx = (size_t)cur;

		if (channel_slots[idx].state != CHANNEL_SLOT_EMPTY &&
		    memcmp(channel_slots[idx].prefix, prefix,
			   sizeof(channel_slots[idx].prefix)) == 0) {
			*idx_out = idx;
			return 0;
		}

		cur = channel_slots[idx].bucket_next;
	}

	return -ENOENT;
}

static void channel_slot_clear_locked(size_t idx)
{
	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS ||
	    channel_slots[idx].state == CHANNEL_SLOT_EMPTY) {
		return;
	}

	channel_prefix_bucket_remove(idx);
	if (channel_slots[idx].state == CHANNEL_SLOT_READY && channel_count > 0U) {
		channel_count--;
	}
	channel_slots[idx].state = CHANNEL_SLOT_EMPTY;
	channel_slots[idx].bucket_next = PREFIX_BUCKET_INVALID;
	memset(channel_slots[idx].prefix, 0, sizeof(channel_slots[idx].prefix));
	channel_slots[idx].hash = 0U;
}

static void channel_slot_reserve_locked(size_t idx, const uint8_t *secret_prefix, uint8_t hash)
{
	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS || secret_prefix == NULL) {
		return;
	}

	if (channel_slots[idx].state != CHANNEL_SLOT_EMPTY) {
		channel_prefix_bucket_remove(idx);
		if (channel_slots[idx].state == CHANNEL_SLOT_READY && channel_count > 0U) {
			channel_count--;
		}
	}

	channel_slots[idx].state = CHANNEL_SLOT_BUSY;
	channel_slots[idx].bucket_next = PREFIX_BUCKET_INVALID;
	memcpy(channel_slots[idx].prefix, secret_prefix, sizeof(channel_slots[idx].prefix));
	channel_slots[idx].hash = hash;
	channel_prefix_bucket_insert(idx);
}

static void channel_slot_commit_locked(size_t idx)
{
	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS ||
	    channel_slots[idx].state != CHANNEL_SLOT_BUSY) {
		return;
	}

	channel_slots[idx].state = CHANNEL_SLOT_READY;
	channel_count++;
}

static void channel_slot_set_locked(size_t idx, const uint8_t *secret_prefix, uint8_t hash)
{
	channel_slot_reserve_locked(idx, secret_prefix, hash);
	channel_slot_commit_locked(idx);
}

static void channel_tables_reset(void)
{
	for (size_t i = 0; i < PREFIX_BUCKET_COUNT; i++) {
		channel_prefix_bucket_head[i] = PREFIX_BUCKET_INVALID;
	}

	for (size_t i = 0; i < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS; i++) {
		channel_slots[i].state = CHANNEL_SLOT_EMPTY;
		channel_slots[i].bucket_next = PREFIX_BUCKET_INVALID;
		memset(channel_slots[i].prefix, 0, sizeof(channel_slots[i].prefix));
		channel_slots[i].hash = 0U;
	}

	channel_count = 0U;
	channel_mutation_busy = false;
}

static void channel_mutation_finish(void)
{
	k_mutex_lock(&channel_mutex, K_FOREVER);
	channel_mutation_busy = false;
	k_mutex_unlock(&channel_mutex);
}

static int channel_classify_secret_conflict_by_idx(size_t idx, const uint8_t *secret,
						   size_t secret_len)
{
	meshbus_Channel existing = meshbus_Channel_init_zero;
	int rc;

	rc = channel_load_by_idx(idx, &existing);
	if (rc == 0 && channel_secret_matches(&existing, secret, secret_len)) {
		return -EEXIST;
	}
	if (rc == 0 || rc == -ENOENT) {
		return -EADDRINUSE;
	}

	return rc;
}

static int channel_load_by_idx(size_t idx, meshbus_Channel *channel)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];
	int rc;

	if (channel == NULL) {
		return -EINVAL;
	}
	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
		return -ENOENT;
	}

	*channel = (meshbus_Channel)meshbus_Channel_init_zero;
	rc = mb_settings_indexed_blob_load_with_buffer(&channel_settings_schema, idx, channel,
						       buffer, sizeof(buffer));
	if (rc != 0) {
		return rc;
	}
	if (!channel_pb_valid(channel)) {
		return -ENOENT;
	}

	channel_assign_default_name(channel);
	return 0;
}

static int channel_get_by_idx(size_t idx, meshbus_channel *channel)
{
	meshbus_Channel loaded = meshbus_Channel_init_zero;
	int rc;

	if (channel == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS ||
	    channel_slots[idx].state != CHANNEL_SLOT_READY) {
		k_mutex_unlock(&channel_mutex);
		return -ENOENT;
	}
	k_mutex_unlock(&channel_mutex);

	rc = channel_load_by_idx(idx, &loaded);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (channel_slots[idx].state != CHANNEL_SLOT_READY) {
		k_mutex_unlock(&channel_mutex);
		return -ENOENT;
	}
	k_mutex_unlock(&channel_mutex);

	return channel_copy_out(&loaded, channel);
}

/* -------------------------------------------------------------------------- */
/* Settings Schema And Apply                                                  */
/* -------------------------------------------------------------------------- */
static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];
	meshbus_Channel channel = meshbus_Channel_init_zero;
	size_t idx;
	int rc;

	if (name == NULL || read_cb == NULL) {
		return -EINVAL;
	}

	rc = mb_settings_indexed_blob_read_slot_with_buffer(
		&channel_settings_schema, name, len, read_cb, cb_arg,
		CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS, &channel, buffer, sizeof(buffer), &idx);
	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Ignore malformed restored channel blob: idx=%u rc=%d",
			(unsigned int)idx, rc);
		return 0;
	}
	if (!channel_pb_valid(&channel)) {
		LOG_WRN("Ignore invalid restored channel blob: idx=%u", (unsigned int)idx);
		return 0;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	size_t existing_idx = 0U;
	rc = channel_slot_find_by_prefix_locked(channel.secret.bytes, &existing_idx);
	if (rc == 0 && existing_idx != idx) {
		if (existing_idx < idx) {
			k_mutex_unlock(&channel_mutex);
			LOG_WRN("Ignore restored channel with duplicated prefix: idx=%u kept=%u",
				(unsigned int)idx, (unsigned int)existing_idx);
			return 0;
		}

		channel_slot_clear_locked(existing_idx);
		channel_slot_set_locked(idx, channel.secret.bytes, channel.hash.bytes[0]);
		k_mutex_unlock(&channel_mutex);
		LOG_WRN("Ignore restored channel with duplicated prefix: idx=%u kept=%u",
			(unsigned int)existing_idx, (unsigned int)idx);
		return 0;
	}
	if (rc != 0 && rc != -ENOENT) {
		k_mutex_unlock(&channel_mutex);
		return rc;
	}

	channel_slot_set_locked(idx, channel.secret.bytes, channel.hash.bytes[0]);
	k_mutex_unlock(&channel_mutex);

	return 0;
}

static int settings_handle_export(int (*export_func)(const char *name, const void *val,
						      size_t val_len))
{
	bool ready_slots[CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS] = {0};
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];

	if (export_func == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	for (size_t i = 0; i < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS; i++) {
		ready_slots[i] = (channel_slots[i].state == CHANNEL_SLOT_READY);
	}
	k_mutex_unlock(&channel_mutex);

	for (size_t i = 0; i < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS; i++) {
		int rc;

		if (!ready_slots[i]) {
			continue;
		}

		rc = mb_settings_indexed_blob_export_encoded(&channel_settings_schema, i, export_func,
							 buffer, sizeof(buffer));
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(meshbus_channel, MESHBUS_CHANNEL_SETTINGS_SUBTREE, NULL,
			       settings_handle_set, NULL, settings_handle_export);

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
static int channel_persist_slot(size_t idx, const meshbus_Channel *channel)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];

	if (channel == NULL || !channel_pb_valid(channel)) {
		return -EINVAL;
	}

	return mb_settings_indexed_blob_save_with_buffer(&channel_settings_schema, idx, channel,
							 buffer, sizeof(buffer));
}

static void channel_restore_slot_best_effort(size_t idx, const meshbus_Channel *old_channel,
					     bool old_loaded)
{
	bool restored = false;

	(void)mb_settings_indexed_blob_delete(&channel_settings_schema, idx);

	if (old_loaded && old_channel != NULL) {
		restored = (channel_persist_slot(idx, old_channel) == 0);
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (restored) {
		channel_slot_set_locked(idx, old_channel->secret.bytes, old_channel->hash.bytes[0]);
	} else {
		channel_slot_clear_locked(idx);
	}
	k_mutex_unlock(&channel_mutex);
}

int meshbus_channel_set(size_t index, const uint8_t *secret, size_t secret_len,
			const char *name)
{
	meshbus_Channel expected = meshbus_Channel_init_zero;
	meshbus_Channel old_channel = meshbus_Channel_init_zero;
	bool old_loaded = false;
	size_t prefix_idx = 0U;
	int rc;

	if (index >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
		return -ENOENT;
	}
	if (secret == NULL || !channel_secret_size_valid(secret_len)) {
		return -EINVAL;
	}
	if (name != NULL && name[0] != '\0') {
		size_t name_len = strnlen(name, sizeof(expected.name));

		if (name_len >= sizeof(expected.name)) {
			return -EINVAL;
		}

		memcpy(expected.name, name, name_len);
		expected.name[name_len] = '\0';
	}

	expected.hash.size = CHANNEL_HASH_SIZE;
	rc = channel_secret_hash(secret, secret_len, expected.hash.bytes);
	if (rc != 0) {
		return rc;
	}
	expected.secret.size = (pb_size_t)secret_len;
	memcpy(expected.secret.bytes, secret, secret_len);
	channel_assign_default_name(&expected);

	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (channel_mutation_busy) {
		k_mutex_unlock(&channel_mutex);
		return -EBUSY;
	}
	channel_mutation_busy = true;
	if (channel_slots[index].state == CHANNEL_SLOT_BUSY) {
		channel_mutation_busy = false;
		k_mutex_unlock(&channel_mutex);
		return -EBUSY;
	}
	rc = channel_slot_find_by_prefix_locked(secret, &prefix_idx);
	if (rc == 0 && prefix_idx != index) {
		if (channel_slots[prefix_idx].state == CHANNEL_SLOT_BUSY) {
			channel_mutation_busy = false;
			k_mutex_unlock(&channel_mutex);
			return -EBUSY;
		}
		k_mutex_unlock(&channel_mutex);
		rc = channel_classify_secret_conflict_by_idx(prefix_idx, secret, secret_len);
		channel_mutation_finish();
		return rc;
	}
	if (rc != 0 && rc != -ENOENT) {
		channel_mutation_busy = false;
		k_mutex_unlock(&channel_mutex);
		return rc;
	}
	channel_slot_reserve_locked(index, secret, expected.hash.bytes[0]);
	k_mutex_unlock(&channel_mutex);

	rc = channel_load_by_idx(index, &old_channel);
	if (rc == 0) {
		old_loaded = true;
	} else if (rc != -ENOENT) {
		k_mutex_lock(&channel_mutex, K_FOREVER);
		if (channel_slots[index].state == CHANNEL_SLOT_BUSY) {
			old_loaded = false;
		}
		k_mutex_unlock(&channel_mutex);
	}

	rc = channel_persist_slot(index, &expected);
	if (rc != 0) {
		channel_restore_slot_best_effort(index, &old_channel, old_loaded);
		channel_mutation_finish();
		return rc;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	channel_slot_commit_locked(index);
	channel_mutation_busy = false;
	k_mutex_unlock(&channel_mutex);

	channel_notify_publish_changed(expected.secret.bytes);
	LOG_INF("Set channel idx=%u name=%s", (unsigned int)index, expected.name);
	return 0;
}

int meshbus_channel_reset(size_t index)
{
	uint8_t prefix[CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES];
	uint8_t hash;
	bool ready;
	int rc;

	if (index >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
		return -ENOENT;
	}

	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (channel_mutation_busy) {
		k_mutex_unlock(&channel_mutex);
		return -EBUSY;
	}
	channel_mutation_busy = true;
	if (channel_slots[index].state == CHANNEL_SLOT_BUSY) {
		channel_mutation_busy = false;
		k_mutex_unlock(&channel_mutex);
		return -EBUSY;
	}
	ready = (channel_slots[index].state == CHANNEL_SLOT_READY);
	if (!ready) {
		k_mutex_unlock(&channel_mutex);
		rc = mb_settings_indexed_blob_delete(&channel_settings_schema, index);
		channel_mutation_finish();
		return rc;
	}
	memcpy(prefix, channel_slots[index].prefix, sizeof(prefix));
	hash = channel_slots[index].hash;
	channel_slot_reserve_locked(index, prefix, hash);
	k_mutex_unlock(&channel_mutex);

	rc = mb_settings_indexed_blob_delete(&channel_settings_schema, index);
	k_mutex_lock(&channel_mutex, K_FOREVER);
	if (rc != 0) {
		channel_slot_commit_locked(index);
		channel_mutation_busy = false;
		k_mutex_unlock(&channel_mutex);
		return rc;
	}
	channel_slot_clear_locked(index);
	channel_mutation_busy = false;
	k_mutex_unlock(&channel_mutex);

	channel_notify_publish_changed(prefix);
	LOG_INF("Reset channel idx=%u", (unsigned int)index);
	return 0;
}

int meshbus_channel_get(size_t index, meshbus_channel *channel)
{
	if (channel == NULL) {
		return -EINVAL;
	}
	if (index >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
		return -ENOENT;
	}

	return channel_get_by_idx(index, channel);
}

int meshbus_channel_next_by_hash(const uint8_t *hash, size_t start_slot, size_t *slot_id,
				 meshbus_channel *channel)
{
	size_t cursor = start_slot;

	if (hash == NULL || slot_id == NULL || channel == NULL) {
		return -EINVAL;
	}

	while (cursor < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
		size_t matched_idx = CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS;

		k_mutex_lock(&channel_mutex, K_FOREVER);
		for (size_t i = cursor; i < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS; i++) {
			if (channel_slots[i].state == CHANNEL_SLOT_READY &&
			    channel_slots[i].hash == hash[0]) {
				matched_idx = i;
				break;
			}
		}
		k_mutex_unlock(&channel_mutex);

		if (matched_idx >= CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS) {
			return -ENOENT;
		}

		meshbus_channel matched_channel = meshbus_Channel_init_zero;
		int rc = channel_get_by_idx(matched_idx, &matched_channel);

		cursor = matched_idx + 1U;
		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}
		if (matched_channel.hash.size == 0U || matched_channel.hash.bytes[0] != hash[0]) {
			continue;
		}

		*slot_id = matched_idx;
		*channel = matched_channel;
		return 0;
	}

	return -ENOENT;
}

uint8_t meshbus_channel_store_count(void)
{
	uint8_t count;

	k_mutex_lock(&channel_mutex, K_FOREVER);
	count = channel_count;
	k_mutex_unlock(&channel_mutex);

	return count;
}

uint8_t meshbus_channel_store_size(void)
{
	return CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS;
}

uint8_t meshbus_channel_next_free_slot(void)
{
	uint8_t index = CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS;

	k_mutex_lock(&channel_mutex, K_FOREVER);
	for (size_t i = 0; i < CONFIG_MESHBUS_CHANNEL_MAX_CHANNELS; i++) {
		if (channel_slots[i].state == CHANNEL_SLOT_EMPTY) {
			index = (uint8_t)i;
			break;
		}
	}
	k_mutex_unlock(&channel_mutex);

	return index;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
static int meshbus_channel_init(void)
{
	uint8_t loaded_count;
	int rc;

	LOG_DBG("Initializing Meshbus channel service");

	k_mutex_lock(&channel_mutex, K_FOREVER);
	channel_tables_reset();
	k_mutex_unlock(&channel_mutex);

	rc = settings_load_subtree(MESHBUS_CHANNEL_SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("Failed to load channel settings: %d", rc);
	}

	loaded_count = meshbus_channel_store_count();
	LOG_INF("Meshbus channel ready: loaded=%u prefix_bytes=%u", (unsigned int)loaded_count,
		(unsigned int)CONFIG_MESHBUS_CHANNEL_SECRET_PREFIX_BYTES);
	return 0;
}

SYS_INIT(meshbus_channel_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
