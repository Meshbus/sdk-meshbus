/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pb_decode.h>
#include <psa/crypto.h>

#include <zephyr/kernel.h>
#include <channel/channel.h>
#include <notify/notify.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include "mbs_settings_internal.h"
#include "meshbus/channel.pb.h"

#define TEST_CHANNEL_SETTINGS_SUBTREE "meshbus/channel"

MBS_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(channel_blob_schema, TEST_CHANNEL_SETTINGS_SUBTREE, NULL,
				       meshbus_Channel, meshbus_Channel);

static struct k_sem channel_notify_sem;
static struct k_sem settings_save_entered_sem;
static struct k_sem settings_save_release_sem;
static mbs_notify_event last_channel_notify;

enum settings_fault_mode {
	SETTINGS_FAULT_NONE,
	SETTINGS_FAULT_SAVE_EIO,
	SETTINGS_FAULT_SAVE_ENOSPC,
	SETTINGS_FAULT_SAVE_PARTIAL,
	SETTINGS_FAULT_SAVE_BLOCK,
	SETTINGS_FAULT_DELETE_EIO,
};

static atomic_t settings_fault_mode;
static int concurrent_set_result;
static struct k_thread concurrent_set_thread;
K_THREAD_STACK_DEFINE(concurrent_set_stack, 2048);

int __real_settings_save_one(const char *name, const void *value, size_t val_len);
int __real_settings_delete(const char *name);

static bool is_channel_setting(const char *name)
{
	return name != NULL && strncmp(name, TEST_CHANNEL_SETTINGS_SUBTREE "/",
				       strlen(TEST_CHANNEL_SETTINGS_SUBTREE) + 1U) == 0;
}

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
	atomic_val_t mode;

	if (!is_channel_setting(name)) {
		return __real_settings_save_one(name, value, val_len);
	}

	mode = atomic_get(&settings_fault_mode);
	if (mode == SETTINGS_FAULT_SAVE_BLOCK &&
	    atomic_cas(&settings_fault_mode, SETTINGS_FAULT_SAVE_BLOCK, SETTINGS_FAULT_NONE)) {
		k_sem_give(&settings_save_entered_sem);
		(void)k_sem_take(&settings_save_release_sem, K_FOREVER);
		return __real_settings_save_one(name, value, val_len);
	}
	if (mode == SETTINGS_FAULT_SAVE_EIO &&
	    atomic_cas(&settings_fault_mode, SETTINGS_FAULT_SAVE_EIO, SETTINGS_FAULT_NONE)) {
		return -EIO;
	}
	if (mode == SETTINGS_FAULT_SAVE_ENOSPC &&
	    atomic_cas(&settings_fault_mode, SETTINGS_FAULT_SAVE_ENOSPC, SETTINGS_FAULT_NONE)) {
		return -ENOSPC;
	}
	if (mode == SETTINGS_FAULT_SAVE_PARTIAL &&
	    atomic_cas(&settings_fault_mode, SETTINGS_FAULT_SAVE_PARTIAL, SETTINGS_FAULT_NONE)) {
		size_t partial_len = MAX(1U, val_len / 2U);

		(void)__real_settings_save_one(name, value, partial_len);
		return -EIO;
	}

	return __real_settings_save_one(name, value, val_len);
}

int __wrap_settings_delete(const char *name)
{
	if (is_channel_setting(name) &&
	    atomic_cas(&settings_fault_mode, SETTINGS_FAULT_DELETE_EIO, SETTINGS_FAULT_NONE)) {
		return -EIO;
	}

	return __real_settings_delete(name);
}

static const uint8_t test_hash_a[1] = {0x11};
static const uint8_t test_hash_missing[1] = {0x7f};

static const uint8_t secret_a[MBS_CHANNEL_SECRET_DEFAULT_LEN] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t secret_b[MBS_CHANNEL_SECRET_DEFAULT_LEN] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

static const uint8_t secret_same_prefix[MBS_CHANNEL_SECRET_DEFAULT_LEN] = {
	0x20, 0x21, 0x22, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};

static const uint8_t secret_32[MBS_CHANNEL_SECRET_SIZE] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
};

static void assert_channel_matches(const mbs_channel *channel, const uint8_t *hash,
				   const uint8_t *secret, size_t secret_len, const char *name);

static void channel_notify_listener_cb(const struct zbus_channel *chan)
{
	const mbs_notify_event *event = zbus_chan_const_msg(chan);

	if (chan != &mbs_notify_chan || event == NULL) {
		return;
	}

	last_channel_notify = *event;
	k_sem_give(&channel_notify_sem);
}

ZBUS_LISTENER_DEFINE(channel_notify_listener, channel_notify_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_notify_chan, channel_notify_listener, 0);

static void channel_notify_drain(void)
{
	memset(&last_channel_notify, 0, sizeof(last_channel_notify));
	while (k_sem_take(&channel_notify_sem, K_NO_WAIT) == 0) {
	}
}

static int decode_notify_event(const mbs_notify_event *event, mbs_notify *payload)
{
	pb_istream_t stream;

	if (event == NULL || payload == NULL || event->payload_len == 0U ||
	    event->payload_len > MBS_NOTIFY_PAYLOAD_MAX_LEN) {
		return -EINVAL;
	}

	*payload = (mbs_notify)meshbus_Notify_init_zero;
	stream = pb_istream_from_buffer(event->payload, event->payload_len);
	if (!pb_decode(&stream, meshbus_Notify_fields, payload)) {
		return -EINVAL;
	}

	return 0;
}

static int wait_channel_notify(mbs_notify_event *out, int32_t timeout_ms)
{
	k_timeout_t timeout = K_MSEC(timeout_ms);
	int rc;

	for (;;) {
		rc = k_sem_take(&channel_notify_sem, timeout);
		if (rc != 0) {
			return -ETIMEDOUT;
		}

		if (last_channel_notify.type == MBS_NOTIFY_TYPE_CHANNELS_CHANGED) {
			if (out != NULL) {
				*out = last_channel_notify;
			}
			return 0;
		}

		timeout = K_NO_WAIT;
	}
}

static void build_default_name(const uint8_t *secret, char *name, size_t name_size)
{
	(void)snprintk(name, name_size, "%02X%02X", secret[0], secret[1]);
}

static void build_channel_hash(const uint8_t *secret, size_t secret_len, uint8_t hash[1])
{
	uint8_t full_hash[32];
	size_t full_hash_len;
	psa_status_t status;

	status = psa_hash_compute(PSA_ALG_SHA_256, secret, secret_len, full_hash, sizeof(full_hash),
				  &full_hash_len);
	zassert_equal(status, PSA_SUCCESS, "psa_hash_compute failed: %d", (int)status);
	zassert_true(full_hash_len >= 1U, "sha256 output too short");
	hash[0] = full_hash[0];
	memset(full_hash, 0, sizeof(full_hash));
}

static void build_secret_with_hash(uint8_t secret[MBS_CHANNEL_SECRET_DEFAULT_LEN],
				   uint8_t first_byte, uint8_t target_hash)
{
	uint8_t hash[1];

	for (uint32_t attempt = 0U; attempt <= UINT16_MAX; attempt++) {
		for (size_t i = 0U; i < MBS_CHANNEL_SECRET_DEFAULT_LEN; i++) {
			secret[i] = (uint8_t)(first_byte + (i * 17U) + (attempt * 3U));
		}
		secret[0] = first_byte;
		secret[1] = (uint8_t)attempt;
		secret[2] = (uint8_t)(attempt >> 8);
		secret[3] = (uint8_t)(first_byte ^ attempt);

		build_channel_hash(secret, MBS_CHANNEL_SECRET_DEFAULT_LEN, hash);
		if (hash[0] == target_hash) {
			return;
		}
	}

	zassert_true(false, "failed to build channel secret with requested hash");
}

static void build_channel_slot_key(char *buf, size_t buf_sz, size_t idx)
{
	snprintk(buf, buf_sz, TEST_CHANNEL_SETTINGS_SUBTREE "/%u", (unsigned int)idx);
}

static void build_channel_residue_field_key(char *buf, size_t buf_sz, size_t idx, uint32_t tag)
{
	snprintk(buf, buf_sz, TEST_CHANNEL_SETTINGS_SUBTREE "/%u/%u", (unsigned int)idx,
		 (unsigned int)tag);
}

static void save_channel_residue_field_bytes(size_t idx, uint32_t tag, const uint8_t *data,
					     size_t len)
{
	char key[64];
	int rc;

	build_channel_residue_field_key(key, sizeof(key), idx, tag);
	rc = settings_save_one(key, data, len);
	zassert_ok(rc, "settings_save_one(%s) failed: %d", key, rc);
}

static void save_channel_blob(size_t idx, const meshbus_Channel *channel)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];
	int rc;

	rc = mbs_settings_indexed_blob_save_with_buffer(&channel_blob_schema, idx, channel, buffer,
						       sizeof(buffer));
	zassert_ok(rc, "save channel blob idx=%u failed: %d", (unsigned int)idx, rc);
}

static void save_channel_raw_blob(size_t idx, const uint8_t *data, size_t len)
{
	char key[64];
	int rc;

	rc = mbs_settings_indexed_blob_key_assemble(key, sizeof(key), &channel_blob_schema, idx);
	zassert_true(rc > 0, "channel blob key assemble failed: idx=%u rc=%d", (unsigned int)idx,
		     rc);
	rc = settings_save_one(key, data, len);
	zassert_ok(rc, "save raw channel blob idx=%u failed: %d", (unsigned int)idx, rc);
}

static void delete_all_channel_settings(void)
{
	static const uint32_t residue_tags[] = {
		meshbus_Channel_hash_tag,
		meshbus_Channel_secret_tag,
		meshbus_Channel_name_tag,
		99U,
	};
	char key[64];

	for (size_t idx = 0; idx < CONFIG_MBS_CHANNEL_MAX_CHANNELS; idx++) {
		zassert_ok(mbs_settings_indexed_blob_delete(&channel_blob_schema, idx),
			   "delete channel blob idx=%u failed", (unsigned int)idx);

		for (size_t i = 0; i < ARRAY_SIZE(residue_tags); i++) {
			build_channel_residue_field_key(key, sizeof(key), idx, residue_tags[i]);
			(void)settings_delete(key);
		}
	}
}

static void load_channel_blob_from_store(size_t idx, meshbus_Channel *channel)
{
	uint8_t buffer[MBS_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Channel_size)];
	int rc;

	*channel = (meshbus_Channel)meshbus_Channel_init_zero;
	rc = mbs_settings_indexed_blob_load_with_buffer(&channel_blob_schema, idx, channel, buffer,
						       sizeof(buffer));
	zassert_ok(rc, "load channel blob idx=%u failed: %d", (unsigned int)idx, rc);
}

static void expect_slot_store_matches(size_t idx, const uint8_t *hash, const uint8_t *secret,
				      size_t secret_len, const char *name)
{
	meshbus_Channel stored = meshbus_Channel_init_zero;

	load_channel_blob_from_store(idx, &stored);
	assert_channel_matches(&stored, hash, secret, secret_len, name);
}

static void expect_slot_store_empty(size_t idx)
{
	char key[48];
	ssize_t len;

	build_channel_slot_key(key, sizeof(key), idx);
	len = settings_get_val_len(key);
	zassert_true(len == -ENOENT || len == 0,
		     "channel slot blob should be deleted or tombstoned: %s len=%zd", key, len);
}

static void expect_slot_store_present(size_t idx)
{
	char key[48];
	ssize_t len;

	build_channel_slot_key(key, sizeof(key), idx);
	len = settings_get_val_len(key);
	zassert_true(len > 0, "channel slot blob should exist: %s len=%zd", key, len);
}

static void reload_channel_settings(void)
{
	int rc = settings_load_subtree(TEST_CHANNEL_SETTINGS_SUBTREE);

	zassert_ok(rc, "settings_load_subtree(%s) failed: %d", TEST_CHANNEL_SETTINGS_SUBTREE, rc);
}

static void assert_channel_matches(const mbs_channel *channel, const uint8_t *hash,
				   const uint8_t *secret, size_t secret_len, const char *name)
{
	zassert_not_null(channel, "channel should not be NULL");
	zassert_equal(channel->hash.size, 1U, "hash size mismatch");
	zassert_equal(channel->hash.bytes[0], hash[0], "hash mismatch");
	zassert_equal(channel->secret.size, secret_len, "secret size mismatch");
	zassert_mem_equal(channel->secret.bytes, secret, secret_len, "secret mismatch");
	zassert_true(strcmp(channel->name, name) == 0, "name mismatch: %s", channel->name);
}

static int set_channel_slot(size_t index, const uint8_t *secret, size_t secret_len,
			    const char *name)
{
	int rc = mbs_channel_set(index, secret, secret_len, name);

	zassert_ok(rc, "channel_set(%u) failed: %d", (unsigned int)index, rc);
	return rc;
}

static meshbus_Channel make_channel_record(const uint8_t *hash, const uint8_t *secret,
					   size_t secret_len, const char *name)
{
	meshbus_Channel channel = meshbus_Channel_init_zero;

	channel.hash.size = 1U;
	memcpy(channel.hash.bytes, hash, 1U);
	channel.secret.size = (pb_size_t)secret_len;
	memcpy(channel.secret.bytes, secret, secret_len);
	if (name != NULL) {
		strncpy(channel.name, name, sizeof(channel.name) - 1U);
	}

	return channel;
}

static void reset_state(void)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t size = mbs_channel_store_size();

	for (size_t i = 0; i < size; i++) {
		int rc = mbs_channel_get(i, &channel);

		if (rc == -ENOENT) {
			continue;
		}

		zassert_ok(rc, "channel_get(%u) failed: %d", (unsigned int)i, rc);
		rc = mbs_channel_reset(i);
		zassert_ok(rc, "channel_reset(%u) failed: %d", (unsigned int)i, rc);
	}

	delete_all_channel_settings();
	zassert_equal(mbs_channel_store_count(), 0U, "channel store should be empty");
	zassert_equal(mbs_channel_next_free_slot(), 0U, "first slot should be free");
}

static void *suite_setup(void)
{
	k_sem_init(&channel_notify_sem, 0, 16);
	k_sem_init(&settings_save_entered_sem, 0, 1);
	k_sem_init(&settings_save_release_sem, 0, 1);
	reset_state();
	channel_notify_drain();
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	atomic_set(&settings_fault_mode, SETTINGS_FAULT_NONE);
	while (k_sem_take(&settings_save_entered_sem, K_NO_WAIT) == 0) {
	}
	while (k_sem_take(&settings_save_release_sem, K_NO_WAIT) == 0) {
	}
	reset_state();
	channel_notify_drain();
}

static void concurrent_set_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	concurrent_set_result = mbs_channel_set(0U, secret_a, sizeof(secret_a), "thread");
}

ZTEST(mbs_channel_contract, test_input_validation)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t hash = 0U;
	size_t slot_id = 0U;
	char long_name[MBS_CHANNEL_NAME_MAX_LEN + 4U];

	memset(long_name, 'a', sizeof(long_name));
	long_name[sizeof(long_name) - 1U] = '\0';

	zassert_equal(mbs_channel_set(0U, NULL, sizeof(secret_a), "room"), -EINVAL,
		      "set NULL secret mismatch");
	zassert_equal(mbs_channel_set(0U, secret_a, 8U, "room"), -EINVAL,
		      "set invalid short secret mismatch");
	zassert_equal(mbs_channel_set(0U, secret_a, 20U, "room"), -EINVAL,
		      "set invalid secret len mismatch");
	zassert_equal(mbs_channel_set(0U, secret_a, sizeof(secret_a), long_name), -EINVAL,
		      "set long name mismatch");
	zassert_equal(mbs_channel_set(mbs_channel_store_size(), secret_a, sizeof(secret_a),
					  "room"),
		      -ENOENT, "set invalid index mismatch");

	zassert_equal(mbs_channel_get(0U, NULL), -EINVAL, "get NULL out mismatch");
	zassert_equal(mbs_channel_get(mbs_channel_store_size(), &channel), -ENOENT,
		      "get out-of-range mismatch");

	zassert_equal(mbs_channel_next_by_hash(NULL, 0U, &slot_id, &channel), -EINVAL,
		      "next_by_hash NULL hash mismatch");
	zassert_equal(mbs_channel_next_by_hash(&hash, 0U, NULL, &channel), -EINVAL,
		      "next_by_hash NULL slot mismatch");
	zassert_equal(mbs_channel_next_by_hash(&hash, 0U, &slot_id, NULL), -EINVAL,
		      "next_by_hash NULL channel mismatch");

	zassert_equal(mbs_channel_reset(mbs_channel_store_size()), -ENOENT,
		      "reset invalid index mismatch");
}

ZTEST(mbs_channel_contract, test_maximum_secret_and_name_boundaries)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	char max_name[MBS_CHANNEL_NAME_MAX_LEN];
	uint8_t hash[1];

	memset(max_name, 'n', sizeof(max_name) - 1U);
	max_name[sizeof(max_name) - 1U] = '\0';
	build_channel_hash(secret_32, sizeof(secret_32), hash);
	zassert_ok(mbs_channel_set(0U, secret_32, sizeof(secret_32), max_name),
		   "maximum secret/name should be accepted");
	zassert_ok(mbs_channel_get(0U, &channel), "maximum channel get failed");
	assert_channel_matches(&channel, hash, secret_32, sizeof(secret_32), max_name);
}

ZTEST(mbs_channel_contract, test_backend_failures_rollback_and_recover)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t hash_a[1];
	int rc;

	atomic_set(&settings_fault_mode, SETTINGS_FAULT_SAVE_EIO);
	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), "new");
	zassert_equal(rc, -EIO, "save I/O failure mismatch: %d", rc);
	zassert_equal(mbs_channel_store_count(), 0U, "failed save changed count");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "failed save became visible");
	expect_slot_store_empty(0U);

	set_channel_slot(0U, secret_a, sizeof(secret_a), "stable");
	build_channel_hash(secret_a, sizeof(secret_a), hash_a);
	atomic_set(&settings_fault_mode, SETTINGS_FAULT_SAVE_PARTIAL);
	rc = mbs_channel_set(0U, secret_b, sizeof(secret_b), "partial");
	zassert_equal(rc, -EIO, "partial save failure mismatch: %d", rc);
	zassert_ok(mbs_channel_get(0U, &channel), "old record not restored");
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "stable");
	expect_slot_store_matches(0U, hash_a, secret_a, sizeof(secret_a), "stable");

	atomic_set(&settings_fault_mode, SETTINGS_FAULT_SAVE_ENOSPC);
	rc = mbs_channel_set(1U, secret_b, sizeof(secret_b), "full");
	zassert_equal(rc, -ENOSPC, "backend exhaustion mismatch: %d", rc);
	zassert_equal(mbs_channel_get(1U, &channel), -ENOENT, "exhausted save became visible");

	atomic_set(&settings_fault_mode, SETTINGS_FAULT_DELETE_EIO);
	rc = mbs_channel_reset(0U);
	zassert_equal(rc, -EIO, "delete I/O failure mismatch: %d", rc);
	zassert_ok(mbs_channel_get(0U, &channel), "failed delete hid old record");
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "stable");
	zassert_ok(mbs_channel_reset(0U), "reset did not recover after delete failure");
}

ZTEST(mbs_channel_contract, test_concurrent_mutation_is_bounded_and_recovers)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	k_tid_t thread;
	int rc;

	concurrent_set_result = -EINPROGRESS;
	atomic_set(&settings_fault_mode, SETTINGS_FAULT_SAVE_BLOCK);
	thread = k_thread_create(&concurrent_set_thread, concurrent_set_stack,
				 K_THREAD_STACK_SIZEOF(concurrent_set_stack), concurrent_set_entry,
				 NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_not_null(thread, "failed to create concurrent setter");
	zassert_ok(k_sem_take(&settings_save_entered_sem, K_SECONDS(1)),
		   "concurrent setter did not reach backend");

	rc = mbs_channel_set(1U, secret_b, sizeof(secret_b), "contender");
	zassert_equal(rc, -EBUSY, "concurrent mutation must return -EBUSY: %d", rc);
	k_sem_give(&settings_save_release_sem);
	zassert_ok(k_thread_join(thread, K_SECONDS(1)), "concurrent setter did not finish");
	zassert_ok(concurrent_set_result, "concurrent setter failed: %d", concurrent_set_result);
	zassert_ok(mbs_channel_get(0U, &channel), "thread record missing");
	zassert_ok(mbs_channel_set(1U, secret_b, sizeof(secret_b), "after"),
		   "mutation did not recover after contention");
}

ZTEST(mbs_channel_contract, test_set_get_find_overwrite_and_reset)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	char default_name[MBS_CHANNEL_NAME_MAX_LEN];
	uint8_t hash_a[1];
	size_t slot_id = 0U;
	int rc;

	zassert_equal(mbs_channel_store_size(), CONFIG_MBS_CHANNEL_MAX_CHANNELS,
		      "store size mismatch");
	zassert_equal(mbs_channel_store_count(), 0U, "store count should start empty");
	zassert_equal(mbs_channel_next_free_slot(), 0U, "first free slot should start at zero");

	build_channel_hash(secret_a, sizeof(secret_a), hash_a);
	set_channel_slot(0U, secret_a, sizeof(secret_a), "alpha");
	zassert_equal(mbs_channel_store_count(), 1U, "store count should increment");
	zassert_equal(mbs_channel_next_free_slot(), 1U, "next free slot should advance");

	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "get failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "alpha");

	rc = mbs_channel_next_by_hash(hash_a, 0U, &slot_id, &channel);
	zassert_ok(rc, "next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 0U, "hash match slot mismatch: %u", (unsigned int)slot_id);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "alpha");
	rc = mbs_channel_next_by_hash(hash_a, slot_id + 1U, &slot_id, &channel);
	zassert_equal(rc, -ENOENT, "next_by_hash should stop after last hash match: %d", rc);

	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), "beta");
	zassert_ok(rc, "set overwrite failed: %d", rc);
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "find overwritten failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "beta");
	rc = mbs_channel_next_by_hash(hash_a, 0U, &slot_id, &channel);
	zassert_ok(rc, "next_by_hash overwritten failed: %d", rc);
	zassert_equal(slot_id, 0U, "overwritten hash match slot mismatch");
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "beta");

	build_default_name(secret_a, default_name, sizeof(default_name));
	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), NULL);
	zassert_ok(rc, "set default NULL failed: %d", rc);
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "find default(NULL) failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), default_name);

	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), "");
	zassert_ok(rc, "set default empty failed: %d", rc);
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "find default(empty) failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), default_name);

	rc = mbs_channel_reset(0U);
	zassert_ok(rc, "reset failed: %d", rc);
	zassert_equal(mbs_channel_store_count(), 0U, "store count should decrement");
	zassert_equal(mbs_channel_next_free_slot(), 0U, "reset slot should be reusable");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "get deleted mismatch");
	zassert_equal(mbs_channel_next_by_hash(hash_a, 0U, &slot_id, &channel), -ENOENT,
		      "next_by_hash after reset should return -ENOENT");
	zassert_ok(mbs_channel_reset(0U), "reset empty slot should be idempotent");
}

ZTEST(mbs_channel_contract, test_next_by_hash_cursor_orders_slots)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	uint8_t secret_c[MBS_CHANNEL_SECRET_DEFAULT_LEN];
	uint8_t secret_d[MBS_CHANNEL_SECRET_DEFAULT_LEN];
	uint8_t hash[1];
	size_t slot_id = 0U;
	int rc;

	build_channel_hash(secret_a, sizeof(secret_a), hash);
	build_secret_with_hash(secret_c, 0x40U, hash[0]);
	build_secret_with_hash(secret_d, 0x60U, hash[0]);

	set_channel_slot(0U, secret_a, sizeof(secret_a), "first");
	set_channel_slot(1U, secret_c, sizeof(secret_c), "second");
	set_channel_slot(2U, secret_d, sizeof(secret_d), "third");

	rc = mbs_channel_next_by_hash(hash, 0U, &slot_id, &channel);
	zassert_ok(rc, "first next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 0U, "first hash match slot mismatch: %u", (unsigned int)slot_id);
	assert_channel_matches(&channel, hash, secret_a, sizeof(secret_a), "first");

	rc = mbs_channel_next_by_hash(hash, slot_id + 1U, &slot_id, &channel);
	zassert_ok(rc, "second next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 1U, "second hash match slot mismatch: %u", (unsigned int)slot_id);
	assert_channel_matches(&channel, hash, secret_c, sizeof(secret_c), "second");

	rc = mbs_channel_next_by_hash(hash, slot_id + 1U, &slot_id, &channel);
	zassert_ok(rc, "third next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 2U, "third hash match slot mismatch: %u", (unsigned int)slot_id);
	assert_channel_matches(&channel, hash, secret_d, sizeof(secret_d), "third");

	rc = mbs_channel_next_by_hash(hash, slot_id + 1U, &slot_id, &channel);
	zassert_equal(rc, -ENOENT, "next_by_hash should return -ENOENT after cursor end: %d", rc);
}

ZTEST(mbs_channel_contract, test_next_free_slot_tracks_sparse_slots_and_full_store)
{
	uint8_t expected_full = mbs_channel_store_size();
	uint8_t secret_fill[CONFIG_MBS_CHANNEL_MAX_CHANNELS]
			   [MBS_CHANNEL_SECRET_DEFAULT_LEN] = {{0}};

	zassert_equal(mbs_channel_next_free_slot(), 0U, "empty store next-free mismatch");

	set_channel_slot(1U, secret_a, sizeof(secret_a), "one");
	zassert_equal(mbs_channel_next_free_slot(), 0U, "slot zero should remain first free");

	set_channel_slot(0U, secret_b, sizeof(secret_b), "zero");
	zassert_equal(mbs_channel_next_free_slot(), 2U, "slot two should be first free");

	zassert_ok(mbs_channel_reset(1U), "reset slot one failed");
	zassert_equal(mbs_channel_next_free_slot(), 1U, "reset slot should become first free");

	for (size_t i = 1U; i < CONFIG_MBS_CHANNEL_MAX_CHANNELS; i++) {
		memcpy(secret_fill[i], secret_32, MBS_CHANNEL_SECRET_DEFAULT_LEN);
		secret_fill[i][0] = (uint8_t)(0x80U + i);
		secret_fill[i][1] = (uint8_t)(0x90U + i);
		secret_fill[i][2] = (uint8_t)(0xa0U + i);
		set_channel_slot(i, secret_fill[i], MBS_CHANNEL_SECRET_DEFAULT_LEN, "fill");
	}

	zassert_equal(mbs_channel_store_count(), CONFIG_MBS_CHANNEL_MAX_CHANNELS,
		      "store should be full");
	zassert_equal(mbs_channel_next_free_slot(), expected_full,
		      "full store should return store size");
}

ZTEST(mbs_channel_contract, test_set_and_reset_publish_channel_notify)
{
	mbs_notify_event notify = MBS_NOTIFY_EVENT_INIT_ZERO;
	mbs_notify payload = meshbus_Notify_init_zero;
	int rc;

	channel_notify_drain();

	set_channel_slot(0U, secret_a, sizeof(secret_a), "notify");
	rc = wait_channel_notify(&notify, 300);
	zassert_ok(rc, "set notify missing: %d", rc);
	rc = decode_notify_event(&notify, &payload);
	zassert_ok(rc, "set notify decode failed: %d", rc);
	zassert_equal(payload.which_payload_variant, MBS_NOTIFY_TAG_CHANNEL,
		      "set notify tag mismatch");
	zassert_equal(payload.payload_variant.channel.secret_prefix.size,
		      CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES, "set prefix size mismatch");
	zassert_mem_equal(payload.payload_variant.channel.secret_prefix.bytes, secret_a,
			  CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES, "set prefix mismatch");

	channel_notify_drain();
	rc = mbs_channel_reset(0U);
	zassert_ok(rc, "reset failed: %d", rc);
	rc = wait_channel_notify(&notify, 300);
	zassert_ok(rc, "reset notify missing: %d", rc);
	rc = decode_notify_event(&notify, &payload);
	zassert_ok(rc, "reset notify decode failed: %d", rc);
	zassert_equal(payload.which_payload_variant, MBS_NOTIFY_TAG_CHANNEL,
		      "reset notify tag mismatch");
	zassert_equal(payload.payload_variant.channel.secret_prefix.size,
		      CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES, "reset prefix size mismatch");
	zassert_mem_equal(payload.payload_variant.channel.secret_prefix.bytes, secret_a,
			  CONFIG_MBS_CHANNEL_SECRET_PREFIX_BYTES, "reset prefix mismatch");

	channel_notify_drain();
	rc = mbs_channel_reset(0U);
	zassert_ok(rc, "empty reset failed: %d", rc);
	rc = k_sem_take(&channel_notify_sem, K_MSEC(50));
	zassert_not_equal(rc, 0, "empty reset should not emit notify");
}

ZTEST(mbs_channel_contract, test_default_name_duplicate_conflict_and_capacity)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	char default_name[MBS_CHANNEL_NAME_MAX_LEN];
	uint8_t secret_fill[CONFIG_MBS_CHANNEL_MAX_CHANNELS]
			   [MBS_CHANNEL_SECRET_DEFAULT_LEN] = {{0}};
	uint8_t hash_b[1];
	int rc;

	build_channel_hash(secret_b, sizeof(secret_b), hash_b);
	set_channel_slot(0U, secret_b, sizeof(secret_b), NULL);
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "find default-name channel failed: %d", rc);
	build_default_name(secret_b, default_name, sizeof(default_name));
	assert_channel_matches(&channel, hash_b, secret_b, sizeof(secret_b), default_name);

	zassert_equal(mbs_channel_set(1U, secret_b, sizeof(secret_b), "dup"), -EEXIST,
		      "duplicate secret mismatch");
	zassert_equal(
		mbs_channel_set(1U, secret_same_prefix, sizeof(secret_same_prefix), "conflict"),
		-EADDRINUSE, "same-prefix conflict mismatch");

	for (size_t i = 1; i < CONFIG_MBS_CHANNEL_MAX_CHANNELS; i++) {
		memcpy(secret_fill[i], secret_32, MBS_CHANNEL_SECRET_DEFAULT_LEN);
		secret_fill[i][0] = (uint8_t)(0x40U + i);
		secret_fill[i][1] = (uint8_t)(0x50U + i);
		secret_fill[i][2] = (uint8_t)(0x60U + i);
		set_channel_slot(i, secret_fill[i], MBS_CHANNEL_SECRET_DEFAULT_LEN, "fill");
	}

	zassert_equal(mbs_channel_store_count(), CONFIG_MBS_CHANNEL_MAX_CHANNELS,
		      "store count should reach capacity");
	zassert_equal(mbs_channel_set(mbs_channel_store_size(), secret_a, sizeof(secret_a),
					  "full"),
		      -ENOENT, "set beyond capacity mismatch");
}

ZTEST(mbs_channel_contract, test_missing_queries_and_hash_miss)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	size_t slot_id = 0U;
	int rc;

	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "missing get mismatch");
	zassert_ok(mbs_channel_reset(0U), "missing reset should be idempotent");

	rc = mbs_channel_next_by_hash(test_hash_missing, 0U, &slot_id, &channel);
	zassert_equal(rc, -ENOENT, "next_by_hash miss should return -ENOENT: %d", rc);
	rc = mbs_channel_next_by_hash(test_hash_missing, mbs_channel_store_size(), &slot_id,
					  &channel);
	zassert_equal(rc, -ENOENT, "next_by_hash past store end should return -ENOENT: %d", rc);
}

ZTEST(mbs_channel_contract, test_settings_blob_persistence_round_trip)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	meshbus_Channel stored = meshbus_Channel_init_zero;
	char default_name[MBS_CHANNEL_NAME_MAX_LEN];
	uint8_t hash_a[1];
	int rc;

	build_channel_hash(secret_a, sizeof(secret_a), hash_a);
	set_channel_slot(0U, secret_a, sizeof(secret_a), "alpha");
	expect_slot_store_matches(0U, hash_a, secret_a, sizeof(secret_a), "alpha");

	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), "beta");
	zassert_ok(rc, "set overwrite failed: %d", rc);
	expect_slot_store_matches(0U, hash_a, secret_a, sizeof(secret_a), "beta");

	rc = mbs_channel_set(0U, secret_a, sizeof(secret_a), NULL);
	zassert_ok(rc, "default-name set failed: %d", rc);
	build_default_name(secret_a, default_name, sizeof(default_name));
	expect_slot_store_matches(0U, hash_a, secret_a, sizeof(secret_a), default_name);
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "default-name get failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), default_name);

	reset_state();
	stored = make_channel_record(hash_a, secret_a, sizeof(secret_a), "restored");
	save_channel_blob(0U, &stored);
	reload_channel_settings();

	zassert_equal(mbs_channel_store_count(), 1U, "restore should create one slot");
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "restored get failed: %d", rc);
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), "restored");

	rc = mbs_channel_reset(0U);
	zassert_ok(rc, "reset failed: %d", rc);
	expect_slot_store_empty(0U);
}

ZTEST(mbs_channel_contract, test_settings_restore_validates_records_and_ignores_unknown_keys)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	static const uint8_t malformed_blob[] = {0xff, 0xff, 0xff};
	char default_name[MBS_CHANNEL_NAME_MAX_LEN];
	meshbus_Channel stored = meshbus_Channel_init_zero;
	uint8_t hash_a[1];

	build_channel_hash(secret_a, sizeof(secret_a), hash_a);

	stored.hash.size = sizeof(test_hash_a);
	memcpy(stored.hash.bytes, test_hash_a, sizeof(test_hash_a));
	save_channel_blob(0U, &stored);
	reload_channel_settings();
	zassert_equal(mbs_channel_store_count(), 0U, "hash-only restore should be ignored");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "hash-only get mismatch");
	zassert_ok(mbs_channel_reset(0U), "hash-only reset failed");
	expect_slot_store_empty(0U);

	reset_state();
	stored = (meshbus_Channel)meshbus_Channel_init_zero;
	stored.secret.size = sizeof(secret_a);
	memcpy(stored.secret.bytes, secret_a, sizeof(secret_a));
	save_channel_blob(0U, &stored);
	reload_channel_settings();
	zassert_equal(mbs_channel_store_count(), 0U, "secret-only restore should be ignored");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "secret-only get mismatch");
	zassert_ok(mbs_channel_reset(0U), "secret-only reset failed");
	expect_slot_store_empty(0U);

	reset_state();
	stored = make_channel_record(hash_a, secret_a, sizeof(secret_a), NULL);
	save_channel_blob(0U, &stored);
	reload_channel_settings();
	zassert_equal(mbs_channel_store_count(), 1U, "empty-name restore should occupy slot");
	zassert_ok(mbs_channel_get(0U, &channel), "empty-name get failed");
	build_default_name(secret_a, default_name, sizeof(default_name));
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), default_name);

	reset_state();
	stored = make_channel_record(hash_a, secret_a, sizeof(secret_a), NULL);
	save_channel_blob(0U, &stored);
	save_channel_residue_field_bytes(0U, 99U, (const uint8_t *)"ignored", strlen("ignored"));
	reload_channel_settings();
	zassert_equal(mbs_channel_store_count(), 1U, "residue field key should be ignored");
	zassert_ok(mbs_channel_get(0U, &channel), "residue-field restore get failed");
	build_default_name(secret_a, default_name, sizeof(default_name));
	assert_channel_matches(&channel, hash_a, secret_a, sizeof(secret_a), default_name);

	reset_state();
	save_channel_raw_blob(0U, malformed_blob, sizeof(malformed_blob));
	reload_channel_settings();
	zassert_equal(mbs_channel_store_count(), 0U, "malformed blob should be ignored");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "malformed blob get mismatch");
}

ZTEST(mbs_channel_contract, test_settings_restore_conflict_keeps_first_slot)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	meshbus_Channel stored = meshbus_Channel_init_zero;
	uint8_t hash_b[1];
	uint8_t hash_same_prefix[1];
	int rc;

	build_channel_hash(secret_b, sizeof(secret_b), hash_b);
	build_channel_hash(secret_same_prefix, sizeof(secret_same_prefix), hash_same_prefix);

	stored = make_channel_record(hash_b, secret_b, sizeof(secret_b), "first");
	save_channel_blob(0U, &stored);
	stored = make_channel_record(hash_same_prefix, secret_same_prefix,
				     sizeof(secret_same_prefix), "second");
	save_channel_blob(1U, &stored);

	reload_channel_settings();

	zassert_equal(mbs_channel_store_count(), 1U, "conflict restore should keep one slot");
	rc = mbs_channel_get(0U, &channel);
	zassert_ok(rc, "kept channel get failed: %d", rc);
	assert_channel_matches(&channel, hash_b, secret_b, sizeof(secret_b), "first");
	zassert_equal(mbs_channel_get(1U, &channel), -ENOENT,
		      "conflicted slot should be empty");
	expect_slot_store_present(1U);
}

ZTEST(mbs_channel_contract, test_restore_hash_without_secret_does_not_occupy_slot)
{
	mbs_channel channel = meshbus_Channel_init_zero;
	meshbus_Channel stored = meshbus_Channel_init_zero;

	stored.hash.size = sizeof(test_hash_a);
	memcpy(stored.hash.bytes, test_hash_a, sizeof(test_hash_a));
	strncpy(stored.name, "dangling", sizeof(stored.name) - 1U);
	save_channel_blob(0U, &stored);
	reload_channel_settings();

	zassert_equal(mbs_channel_store_count(), 0U, "hash-only slot should stay invisible");
	zassert_equal(mbs_channel_get(0U, &channel), -ENOENT, "hash-only get mismatch");
}

ZTEST_SUITE(mbs_channel_contract, NULL, suite_setup, test_before, NULL, NULL);
