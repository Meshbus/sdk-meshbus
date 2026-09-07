// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pb_decode.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/meshbus/notify.h>
#include <zephyr/meshbus/time.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "common/settings.h"

#define TEST_CONTACT_SETTINGS_SUBTREE     "meshbus/contact"
#define TEST_CONTACT_SETTINGS_KEY_ADVERT_RAW "advert_raw"

MB_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(contact_blob_schema, TEST_CONTACT_SETTINGS_SUBTREE,
				       NULL, meshbus_Contact, meshbus_contact);
MB_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(contact_advert_raw_blob_schema,
				      TEST_CONTACT_SETTINGS_SUBTREE,
				      TEST_CONTACT_SETTINGS_KEY_ADVERT_RAW);

static K_SEM_DEFINE(contact_notify_sem, 0, 32);
static meshbus_notify_event last_contacts_changed_notify = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
static meshbus_notify_event last_contact_advert_notify = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
static uint32_t contacts_changed_notify_count;
static uint32_t contact_advert_notify_count;

#define CONTACT_WRITER_TEST_STACK_SIZE 2048

static K_THREAD_STACK_DEFINE(contact_writer_stack_a, CONTACT_WRITER_TEST_STACK_SIZE);
static K_THREAD_STACK_DEFINE(contact_writer_stack_b, CONTACT_WRITER_TEST_STACK_SIZE);
static struct k_thread contact_writer_thread_a;
static struct k_thread contact_writer_thread_b;

static K_SEM_DEFINE(contact_save_gate_entered, 0, 1);
static K_SEM_DEFINE(contact_save_gate_release, 0, 1);
static atomic_t contact_save_gate_armed = ATOMIC_INIT(0);
static atomic_t contact_save_gate_claimed = ATOMIC_INIT(0);
static atomic_t contact_blob_save_call_count = ATOMIC_INIT(0);
static atomic_t contact_blob_save_fail_next = ATOMIC_INIT(0);

enum contact_writer_operation {
	CONTACT_WRITER_SET,
	CONTACT_WRITER_RESET,
	CONTACT_WRITER_PUBLISH,
};

struct contact_writer_task {
	enum contact_writer_operation operation;
	meshbus_contact contact;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	const struct zbus_channel *chan;
	const void *event;
	struct k_sem done;
	int rc;
};

extern int __real_settings_save_one(const char *name, const void *value, size_t val_len);

static bool is_contact_record_settings_key(const char *name)
{
	static const char prefix[] = TEST_CONTACT_SETTINGS_SUBTREE "/";
	const char *slot;

	if (name == NULL || strncmp(name, prefix, sizeof(prefix) - 1U) != 0) {
		return false;
	}

	slot = name + sizeof(prefix) - 1U;
	return slot[0] >= '0' && slot[0] <= '9';
}

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
	bool is_contact_record = is_contact_record_settings_key(name);

	if (is_contact_record && atomic_cas(&contact_blob_save_fail_next, 1, 0)) {
		return -EIO;
	}
	if (is_contact_record &&
	    atomic_get(&contact_save_gate_armed) != 0) {
		atomic_inc(&contact_blob_save_call_count);
		if (atomic_cas(&contact_save_gate_claimed, 0, 1)) {
			k_sem_give(&contact_save_gate_entered);
			(void)k_sem_take(&contact_save_gate_release, K_FOREVER);
		}
	}

	return __real_settings_save_one(name, value, val_len);
}

static void contact_save_gate_disarm(void)
{
	atomic_clear(&contact_save_gate_armed);
	atomic_clear(&contact_save_gate_claimed);
	atomic_clear(&contact_blob_save_call_count);
	atomic_clear(&contact_blob_save_fail_next);
	while (k_sem_take(&contact_save_gate_entered, K_NO_WAIT) == 0) {
	}
	while (k_sem_take(&contact_save_gate_release, K_NO_WAIT) == 0) {
	}
}

static void contact_save_gate_arm(void)
{
	contact_save_gate_disarm();
	atomic_set(&contact_save_gate_armed, 1);
}

static void contact_blob_save_fail_once(void)
{
	contact_save_gate_disarm();
	atomic_set(&contact_blob_save_fail_next, 1);
}

static void contact_writer_task_run(void *arg1, void *arg2, void *arg3)
{
	struct contact_writer_task *task = arg1;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	switch (task->operation) {
	case CONTACT_WRITER_SET:
		task->rc = meshbus_contact_set(task->contact.public_key.bytes, &task->contact);
		break;
	case CONTACT_WRITER_RESET:
		task->rc = meshbus_contact_reset(task->prefix);
		break;
	case CONTACT_WRITER_PUBLISH:
		task->rc = zbus_chan_pub(task->chan, task->event, K_FOREVER);
		break;
	default:
		task->rc = -EINVAL;
		break;
	}

	k_sem_give(&task->done);
}

static void contact_writer_task_init(struct contact_writer_task *task,
				     enum contact_writer_operation operation)
{
	memset(task, 0, sizeof(*task));
	task->operation = operation;
	k_sem_init(&task->done, 0, 1);
}

static void contact_writer_start(struct k_thread *thread, k_thread_stack_t *stack,
				 size_t stack_size, struct contact_writer_task *task)
{
	(void)k_thread_create(thread, stack, stack_size, contact_writer_task_run, task, NULL, NULL,
			      K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
}

static void contact_writer_finish(struct k_thread *thread, struct contact_writer_task *task,
				  bool done_already)
{
	if (!done_already) {
		zassert_ok(k_sem_take(&task->done, K_SECONDS(1)), "writer did not finish");
	}
	zassert_ok(k_thread_join(thread, K_SECONDS(1)), "writer thread did not exit");
}

static void contact_notify_listener_cb(const struct zbus_channel *chan)
{
	const meshbus_notify_event *event = zbus_chan_const_msg(chan);

	if (chan != &meshbus_notify_chan || event == NULL) {
		return;
	}

	if (event->type == MESHBUS_NOTIFY_TYPE_NODES_CHANGED) {
		last_contacts_changed_notify = *event;
		contacts_changed_notify_count++;
		k_sem_give(&contact_notify_sem);
	}
	if (event->type == MESHBUS_NOTIFY_TYPE_NODE_ADVERT) {
		last_contact_advert_notify = *event;
		contact_advert_notify_count++;
		k_sem_give(&contact_notify_sem);
	}
}

ZBUS_LISTENER_DEFINE(contact_notify_listener, contact_notify_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_notify_chan, contact_notify_listener, 0);

static void fill_bytes(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(seed + i);
	}
}

static void build_contact(meshbus_contact *contact, uint8_t seed, const char *name)
{
	*contact = (meshbus_contact)meshbus_Contact_init_zero;
	contact->role = MESHBUS_CONTACT_ROLE_CHAT;
	contact->public_key.size = MESHBUS_CONTACT_PUBLIC_KEY_SIZE;
	fill_bytes(contact->public_key.bytes, contact->public_key.size, seed);
	if (name != NULL) {
		strncpy(contact->name, name, sizeof(contact->name) - 1U);
		contact->name[sizeof(contact->name) - 1U] = '\0';
	}
}

static int create_contact(const meshbus_contact *contact)
{
	if (contact == NULL) {
		return -EINVAL;
	}

	return meshbus_contact_set(contact->public_key.bytes, contact);
}

static int find_contact_index_by_prefix(const uint8_t *public_key_prefix, size_t *index_out)
{
	if (public_key_prefix == NULL || index_out == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		meshbus_contact contact = meshbus_Contact_init_zero;
		int rc = meshbus_contact_get(i, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}
		if (memcmp(contact.public_key.bytes, public_key_prefix,
			   CONFIG_MESHBUS_CONTACT_PREFIX_BYTES) == 0) {
			*index_out = i;
			return 0;
		}
	}

	return -ENOENT;
}

static int delete_all_contacts(void)
{
	for (size_t i = 0; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		meshbus_contact contact = meshbus_Contact_init_zero;
		int rc = meshbus_contact_get(i, &contact);

		if (rc == -ENOENT) {
			continue;
		}
		if (rc != 0) {
			return rc;
		}

		rc = meshbus_contact_reset(contact.public_key.bytes);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static void wait_for_async_contact_response(void)
{
	k_sleep(K_MSEC(20));
}

static void test_realtime_reset(void)
{
	const struct timespec invalid_realtime = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &invalid_realtime));
}

static int publish_contact_response(const struct zbus_channel *chan, const void *event)
{
	int rc;

	if (chan == NULL || event == NULL) {
		return -EINVAL;
	}

	rc = zbus_chan_pub(chan, event, K_NO_WAIT);
	if (rc == 0) {
		wait_for_async_contact_response();
	}

	return rc;
}

static int read_notify_event(meshbus_notify_event *event)
{
	if (event == NULL) {
		return -EINVAL;
	}

	return zbus_chan_read(&meshbus_notify_chan, event, K_NO_WAIT);
}

static meshbus_notify decode_notify_event(const meshbus_notify_event *event)
{
	meshbus_notify payload = meshbus_Notify_init_zero;
	pb_istream_t stream;
	int rc;

	zassert_not_null(event, "event");
	zassert_true(event->payload_len > 0U &&
		     event->payload_len <= MESHBUS_NOTIFY_PAYLOAD_MAX_LEN,
		     "invalid notify payload len=%u", event->payload_len);

	stream = pb_istream_from_buffer(event->payload, event->payload_len);
	rc = pb_decode(&stream, meshbus_Notify_fields, &payload) ? 0 : -EINVAL;
	zassert_ok(rc, "notify decode failed: %d", rc);
	return payload;
}

static void drain_contact_notify_listener(void)
{
	last_contacts_changed_notify = (meshbus_notify_event)MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	last_contact_advert_notify = (meshbus_notify_event)MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	contacts_changed_notify_count = 0U;
	contact_advert_notify_count = 0U;
	while (k_sem_take(&contact_notify_sem, K_NO_WAIT) == 0) {
	}
}

static int wait_contact_notify_counts(uint32_t nodes_changed_count, uint32_t node_advert_count)
{
	for (int attempt = 0; attempt < 20; attempt++) {
		if (contacts_changed_notify_count >= nodes_changed_count &&
		    contact_advert_notify_count >= node_advert_count) {
			return 0;
		}
		(void)k_sem_take(&contact_notify_sem, K_MSEC(10));
	}

	return -ETIMEDOUT;
}

static int update_contact_seen_by_key(const uint8_t *public_key, uint32_t timestamp,
				       bool has_snr, int32_t snr)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (public_key == NULL) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	if (timestamp == 0U) {
		timestamp = (uint32_t)k_uptime_seconds();
		if (timestamp == 0U) {
			timestamp = 1U;
		}
	}
	contact.last_seen_timestamp = timestamp;
	if (has_snr) {
		contact.last_seen_snr = snr;
	}

	return meshbus_contact_set(contact.public_key.bytes, &contact);
}

static int update_contact_alias_by_key(const uint8_t *public_key, const char *alias)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (public_key == NULL || alias == NULL) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	memset(contact.alias, 0, sizeof(contact.alias));
	strncpy(contact.alias, alias, sizeof(contact.alias) - 1U);
	contact.alias[sizeof(contact.alias) - 1U] = '\0';

	return meshbus_contact_set(contact.public_key.bytes, &contact);
}

static int update_contact_path_by_key(const uint8_t *public_key, const uint8_t *out_path,
				       size_t out_path_len)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (public_key == NULL || (out_path == NULL && out_path_len > 0U) ||
	    out_path_len > sizeof(contact.out_path.bytes)) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	memset(contact.out_path.bytes, 0, sizeof(contact.out_path.bytes));
	if (out_path_len > 0U) {
		memcpy(contact.out_path.bytes, out_path, out_path_len);
	}
	contact.out_path.size = (pb_size_t)out_path_len;
	contact.path_hash_size = (contact.path_hash_size == 0U) ? 1U : contact.path_hash_size;
	contact.is_neighbor = (out_path_len == 0U);

	return meshbus_contact_set(contact.public_key.bytes, &contact);
}

static int update_contact_flags_by_key(const uint8_t *public_key, uint32_t clear_mask,
				       uint32_t set_mask)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (public_key == NULL) {
		return -EINVAL;
	}

	rc = meshbus_contact_find_by_key(public_key, &contact);
	if (rc != 0) {
		return rc;
	}

	contact.flags &= ~clear_mask;
	contact.flags |= set_mask;

	return meshbus_contact_set(contact.public_key.bytes, &contact);
}

static void build_key_prefix(const meshbus_contact *contact, uint8_t *prefix)
{
	memcpy(prefix, contact->public_key.bytes, CONFIG_MESHBUS_CONTACT_PREFIX_BYTES);
}

static void build_default_cfg(meshbus_meshcore_config *cfg, uint8_t seed)
{
	*cfg = (meshbus_meshcore_config)meshbus_MeshcoreConfig_init_zero;
	cfg->role = CONFIG_MESHBUS_MESHCORE_DEFAULT_ROLE;
	cfg->path_hash_size = 1U;
	cfg->loop_detect = MESHBUS_MESHCORE_LOOP_DETECT_OFF;
	cfg->client_repeat = false;
	cfg->add_contact_config = (MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT |
				MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER |
				MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM |
				MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR |
				MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE);
	cfg->tx_delay_factor = 0.5f;
	cfg->direct_tx_delay_factor = 0.2f;
	cfg->public_key.size = MESHBUS_CONTACT_PUBLIC_KEY_SIZE;
	cfg->private_key.size = MESHBUS_MESHCORE_PRIVATE_KEY_SIZE;
	fill_bytes(cfg->public_key.bytes, cfg->public_key.size, seed);
	fill_bytes(cfg->private_key.bytes, cfg->private_key.size, (uint8_t)(seed ^ 0x5a));
}

static void save_contact_blob(size_t slot, const meshbus_contact *contact)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	int rc;

	rc = mb_settings_indexed_blob_save_with_buffer(&contact_blob_schema, slot, contact,
						       buffer, sizeof(buffer));
	zassert_ok(rc, "contact blob save failed: slot=%u rc=%d", (unsigned int)slot, rc);
}

static void load_contact_blob(size_t slot, meshbus_contact *contact)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	int rc;

	*contact = (meshbus_contact)meshbus_Contact_init_zero;
	rc = mb_settings_indexed_blob_load_with_buffer(&contact_blob_schema, slot, contact,
						       buffer, sizeof(buffer));
	zassert_ok(rc, "contact blob load failed: slot=%u rc=%d", (unsigned int)slot, rc);
}

static void assert_contact_blob_deleted(size_t slot)
{
	uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(meshbus_Contact_size)];
	size_t len = 0U;
	int rc;

	rc = mb_settings_indexed_blob_load_encoded(&contact_blob_schema, slot, buffer,
					       sizeof(buffer), &len);
	zassert_true(rc == -ENOENT || len == 0U,
		     "contact blob should be deleted or tombstoned: slot=%u rc=%d len=%u",
		     (unsigned int)slot, rc, (unsigned int)len);
}

static void assert_contact_advert_raw_blob_deleted(size_t slot)
{
	uint8_t buffer[MESHBUS_CONTACT_ADVERT_RAW_MAX_LEN];
	size_t len = 0U;
	int rc;

	rc = mb_settings_indexed_raw_load(&contact_advert_raw_blob_schema, slot, buffer,
					  sizeof(buffer), &len);
	zassert_true(rc == -ENOENT || len == 0U,
		     "contact advert raw should be deleted or tombstoned: slot=%u rc=%d len=%u",
		     (unsigned int)slot, rc, (unsigned int)len);
}

static void reload_contact_settings_index(void)
{
	int rc = settings_load_subtree(TEST_CONTACT_SETTINGS_SUBTREE);

	zassert_ok(rc, "settings_load_subtree(%s) failed: %d", TEST_CONTACT_SETTINGS_SUBTREE, rc);
}

static void reset_test_state(void)
{
	int rc;

	contact_save_gate_disarm();
	test_realtime_reset();
	meshbus_meshcore_config cfg;

	build_default_cfg(&cfg, 0x10);
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);
	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
}

static void *suite_setup(void)
{
	reset_test_state();
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_test_state();
}

ZTEST(meshbus_contact_contract, test_invalid_arguments)
{
	meshbus_contact node = meshbus_Contact_init_zero;
	uint8_t key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE] = {0};
	uint8_t too_large_binary[MESHBUS_CONTACT_BINARY_REQUEST_PAYLOAD_MAX_LEN + 1U] = {0};
	size_t slot_id = 0U;

	zassert_equal(meshbus_meshcore_config_set(NULL), -EINVAL, "config_set(NULL) mismatch");
	zassert_equal(meshbus_meshcore_config_get(NULL), -EINVAL, "config_get(NULL) mismatch");

	zassert_equal(meshbus_contact_find_by_key(NULL, &node), -EINVAL,
		      "contact_get_by_key NULL key mismatch");
	zassert_equal(meshbus_contact_find_by_key(key, NULL), -EINVAL,
		      "contact_get_by_key NULL out mismatch");
	zassert_equal(meshbus_contact_find_by_prefix(NULL, &node), -EINVAL,
		      "contact_get_by_key_prefix NULL key mismatch");
	zassert_equal(meshbus_contact_find_by_prefix(key, NULL), -EINVAL,
		      "contact_get_by_key_prefix NULL out mismatch");

	zassert_equal(meshbus_contact_next_by_hash(NULL, 0U, &slot_id, &node), -EINVAL,
		      "next_by_hash NULL hash mismatch");
	zassert_equal(meshbus_contact_next_by_hash(key, 0U, NULL, &node), -EINVAL,
		      "next_by_hash NULL slot mismatch");
	zassert_equal(meshbus_contact_next_by_hash(key, 0U, &slot_id, NULL), -EINVAL,
		      "next_by_hash NULL contact mismatch");

	zassert_equal(meshbus_contact_set(NULL, &node), -EINVAL,
		      "contact_set NULL prefix mismatch");
	zassert_equal(meshbus_contact_set(key, NULL), -EINVAL,
		      "contact_set NULL contact mismatch");
	zassert_equal(meshbus_contact_reset(NULL), -EINVAL, "contact_reset(NULL) mismatch");
	zassert_equal(meshbus_contact_get(0U, NULL), -EINVAL,
		      "contact_get_by_index NULL out mismatch");
	zassert_equal(meshbus_contact_get(CONFIG_MESHBUS_CONTACT_MAX_CONTACTS, &node), -ENOENT,
		      "contact_get_by_index out-of-range mismatch");

	zassert_equal(meshbus_contact_discover_path_request(NULL, NULL), -EINVAL,
		      "discover NULL prefix mismatch");
	zassert_equal(meshbus_meshcore_node_discover_request(0U, 0U, NULL), -EINVAL,
		      "node_discover empty filter mismatch");
	zassert_equal(meshbus_meshcore_node_discover_request(0x80U, 0U, NULL), -EINVAL,
		      "node_discover unknown filter bit mismatch");
	zassert_equal(meshbus_meshcore_trace_request(NULL, 1U, 1U, NULL), -EINVAL,
		      "meshcore trace NULL path mismatch");
	zassert_equal(meshbus_meshcore_trace_request(key, 0U, 1U, NULL), -EINVAL,
		      "meshcore trace empty path mismatch");
	zassert_equal(meshbus_meshcore_trace_request(key, 2U, 1U, NULL), -EINVAL,
		      "meshcore trace even hop count mismatch");
	zassert_equal(meshbus_meshcore_trace_request(key, 1U, 0U, NULL), -EINVAL,
		      "meshcore trace zero hash size mismatch");
	zassert_equal(meshbus_contact_share_request(NULL), -EINVAL,
		      "contact_advert_request NULL prefix mismatch");
	zassert_equal(meshbus_contact_trace_path_request(NULL, NULL), -EINVAL,
		      "trace NULL prefix mismatch");
	zassert_equal(meshbus_contact_telemetry_request(NULL, NULL), -EINVAL,
		      "telemetry NULL prefix mismatch");
	zassert_equal(meshbus_contact_binary_request(NULL, key, sizeof(key), NULL), -EINVAL,
		      "binary NULL prefix mismatch");
	zassert_equal(meshbus_contact_binary_request(key, NULL, sizeof(key), NULL), -EINVAL,
		      "binary NULL payload mismatch");
	zassert_equal(meshbus_contact_binary_request(key, key, 0U, NULL), -EINVAL,
		      "binary empty payload mismatch");
	zassert_equal(meshbus_contact_binary_request(key, too_large_binary,
						     sizeof(too_large_binary), NULL),
		      -EINVAL, "binary oversized payload mismatch");
}

ZTEST(meshbus_contact_contract, test_node_discover_response_channel_validation)
{
	meshbus_contact_response_discover_event event = {0};
	meshbus_contact_response_discover_event got = {0};
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	int rc;

	event.role = MESHBUS_CONTACT_ROLE_SENSOR;
	event.tag = 0x11223344U;
	event.path_len = 2U;
	event.path[0] = 0x7aU;
	event.path[1] = 0x8bU;
	fill_bytes(event.public_key, sizeof(event.public_key), 0x40U);
	event.uplink_snr = -4;
	event.downlink_snr = 7;

	rc = publish_contact_response(&meshbus_contact_discover_response_chan, &event);
	zassert_ok(rc, "valid node_discover response publish failed: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_discover_response_chan, &got, K_NO_WAIT);
	zassert_ok(rc, "node_discover response read failed: %d", rc);
	zassert_equal(got.role, event.role, "node_discover response role mismatch");
	zassert_equal(got.tag, event.tag, "node_discover response tag mismatch");
	zassert_mem_equal(got.public_key, event.public_key, sizeof(event.public_key),
			  "node_discover response key mismatch");
	zassert_equal(got.path_len, event.path_len, "node_discover response path len mismatch");
	zassert_mem_equal(got.path, event.path, event.path_len,
			  "node_discover response path mismatch");
	zassert_equal(got.uplink_snr, event.uplink_snr,
		      "node_discover response uplink snr mismatch");
	zassert_equal(got.downlink_snr, event.downlink_snr,
		      "node_discover response downlink snr mismatch");
	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "node_discover notify read failed: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_DISCOVER,
		      "node_discover notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant,
		      MESHBUS_NOTIFY_TAG_NODE_DISCOVER,
		      "node_discover notify payload tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.tag, event.tag,
		      "node_discover notify request tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.role, event.role,
		      "node_discover notify role mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.public_key.size,
		      sizeof(event.public_key), "node_discover notify key len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_discover.public_key.bytes,
			  event.public_key, sizeof(event.public_key),
			  "node_discover notify key mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.path.size,
		      event.path_len, "node_discover notify path len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_discover.path.bytes,
			  event.path, event.path_len, "node_discover notify path mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.uplink_snr,
		      event.uplink_snr, "node_discover notify uplink snr mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover.downlink_snr,
		      event.downlink_snr, "node_discover notify downlink snr mismatch");

	event.path_len = sizeof(event.path) + 1U;
	rc = zbus_chan_pub(&meshbus_contact_discover_response_chan, &event, K_NO_WAIT);
	zassert_not_equal(rc, 0, "oversized node_discover path should fail");
}

ZTEST(meshbus_contact_contract, test_config_set_get_and_auto_name)
{
	static const meshbus_meshcore_loop_detect modes[] = {
		MESHBUS_MESHCORE_LOOP_DETECT_OFF,
		MESHBUS_MESHCORE_LOOP_DETECT_MINIMAL,
		MESHBUS_MESHCORE_LOOP_DETECT_MODERATE,
		MESHBUS_MESHCORE_LOOP_DETECT_STRICT,
	};
	meshbus_meshcore_config cfg;
	meshbus_meshcore_config got;
	char expect_name[sizeof(cfg.name)] = {0};
	size_t expect_len = CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES * 2U;
	int rc;

	build_default_cfg(&cfg, 0x11);
	cfg.name[0] = '\0';

	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	rc = meshbus_meshcore_config_get(&got);
	zassert_ok(rc, "config_get failed: %d", rc);
	zassert_equal(meshbus_meshcore_active_role_get(), CONFIG_MESHBUS_MESHCORE_DEFAULT_ROLE,
		      "active role mismatch");
	zassert_equal(got.path_hash_size, cfg.path_hash_size, "path_hash_size mismatch");
	zassert_equal(got.loop_detect, cfg.loop_detect, "loop_detect mismatch");
	zassert_equal(got.client_repeat, cfg.client_repeat, "client_repeat mismatch");
	zassert_equal(got.public_key.size, MESHBUS_CONTACT_PUBLIC_KEY_SIZE, "public_key size mismatch");
	zassert_equal(got.private_key.size, MESHBUS_MESHCORE_PRIVATE_KEY_SIZE,
		      "private_key size mismatch");

	for (size_t i = 0; i < CONFIG_MESHBUS_MESHCORE_NAME_PUBKEY_PREFIX_BYTES; i++) {
		(void)snprintk(&expect_name[i * 2U], sizeof(expect_name) - (i * 2U), "%02X",
			       got.public_key.bytes[i]);
	}
	expect_name[expect_len] = '\0';
	zassert_true(strcmp(got.name, expect_name) == 0, "auto name mismatch: %s", got.name);

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		build_default_cfg(&cfg, (uint8_t)(0x40 + i));
		cfg.loop_detect = modes[i];
		cfg.client_repeat = (i % 2U) != 0U;

		rc = meshbus_meshcore_config_set(&cfg);
		zassert_ok(rc, "config_set failed for mode[%u]: %d", (unsigned int)i, rc);

		rc = meshbus_meshcore_config_get(&got);
		zassert_ok(rc, "config_get failed for mode[%u]: %d", (unsigned int)i, rc);
		zassert_equal(got.loop_detect, modes[i],
			      "loop_detect mismatch for mode[%u]", (unsigned int)i);
		zassert_equal(got.client_repeat, cfg.client_repeat,
			      "client_repeat mismatch for mode[%u]", (unsigned int)i);
	}

	build_default_cfg(&cfg, 0x50);
	cfg.client_repeat = false;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set(false) failed: %d", rc);

	rc = meshbus_meshcore_config_get(&got);
	zassert_ok(rc, "config_get(false) failed: %d", rc);
	zassert_false(got.client_repeat, "client_repeat should be false");

	cfg.client_repeat = true;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set(true) failed: %d", rc);

	rc = meshbus_meshcore_config_get(&got);
	zassert_ok(rc, "config_get(true) failed: %d", rc);
	zassert_true(got.client_repeat, "client_repeat should be true");

	build_default_cfg(&cfg, 0x22);
	cfg.public_key.size = MESHBUS_CONTACT_PUBLIC_KEY_SIZE - 1U;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_equal(rc, -EINVAL, "invalid public_key size should fail, rc=%d", rc);

	build_default_cfg(&cfg, 0x23);
	cfg.loop_detect =
		(meshbus_meshcore_loop_detect)(MESHBUS_MESHCORE_LOOP_DETECT_STRICT + 1U);
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_equal(rc, -EINVAL,
		      "invalid loop_detect should fail, rc=%d", rc);

	build_default_cfg(&cfg, 0x24);
	cfg.telemetry_mode_base =
		(meshbus_MeshcoreConfig_TelemetryMode)(MESHBUS_MESHCORE_TELEMETRY_MODE_ALL + 1U);
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_equal(rc, -EINVAL,
		      "invalid telemetry_mode_base should fail, rc=%d", rc);

	build_default_cfg(&cfg, 0x25);
	cfg.telemetry_mode_locat =
		(meshbus_MeshcoreConfig_TelemetryMode)(MESHBUS_MESHCORE_TELEMETRY_MODE_ALL + 1U);
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_equal(rc, -EINVAL,
		      "invalid telemetry_mode_locat should fail, rc=%d", rc);

	build_default_cfg(&cfg, 0x26);
	cfg.telemetry_mode_environment =
		(meshbus_MeshcoreConfig_TelemetryMode)(MESHBUS_MESHCORE_TELEMETRY_MODE_ALL + 1U);
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_equal(rc, -EINVAL,
		      "invalid telemetry_mode_environment should fail, rc=%d", rc);
}

ZTEST(meshbus_contact_contract, test_config_set_get_all_fields)
{
	meshbus_meshcore_config cfg;
	meshbus_meshcore_config got = meshbus_MeshcoreConfig_init_zero;
	int rc;

	build_default_cfg(&cfg, 0x61);
	strncpy(cfg.name, "blob_config", sizeof(cfg.name) - 1U);
	cfg.name[sizeof(cfg.name) - 1U] = '\0';
	cfg.latitude = -1234567;
	cfg.longitude = 7654321;
	cfg.disable_fwd = true;
	cfg.flood_max = 7U;
	cfg.tx_delay_factor = 1.25f;
	cfg.direct_tx_delay_factor = 0.75f;
	cfg.multi_acks = 3U;
	cfg.advert_interval = 60U;
	cfg.flood_advert_interval = 120U;
	cfg.advert_position = true;
	cfg.add_contact_config = MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	cfg.telemetry_mode_base = MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS;
	cfg.telemetry_mode_locat = MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS;
	cfg.telemetry_mode_environment = MESHBUS_MESHCORE_TELEMETRY_MODE_ALL;
	cfg.path_hash_size = 3U;
	cfg.loop_detect = MESHBUS_MESHCORE_LOOP_DETECT_STRICT;
	cfg.client_repeat = true;
	cfg.add_contact_hops_limit = 4U;

	zassert_ok(meshbus_meshcore_config_set(&cfg));

	rc = meshbus_meshcore_config_get(&got);
	zassert_ok(rc, "config_get failed: %d", rc);
	zassert_equal(got.public_key.size, cfg.public_key.size, "public_key size mismatch");
	zassert_mem_equal(got.public_key.bytes, cfg.public_key.bytes, cfg.public_key.size,
			  "public_key mismatch");
	zassert_equal(got.private_key.size, cfg.private_key.size, "private_key size mismatch");
	zassert_mem_equal(got.private_key.bytes, cfg.private_key.bytes, cfg.private_key.size,
			  "private_key mismatch");
	zassert_true(strcmp(got.name, cfg.name) == 0, "name mismatch");
	zassert_equal(got.latitude, cfg.latitude, "latitude mismatch");
	zassert_equal(got.longitude, cfg.longitude, "longitude mismatch");
	zassert_true(got.disable_fwd, "disable_fwd mismatch");
	zassert_equal(got.flood_max, cfg.flood_max, "flood_max mismatch");
	zassert_equal(got.tx_delay_factor, cfg.tx_delay_factor, "tx_delay_factor mismatch");
	zassert_equal(got.direct_tx_delay_factor, cfg.direct_tx_delay_factor,
		      "direct_tx_delay_factor mismatch");
	zassert_equal(got.multi_acks, cfg.multi_acks, "multi_acks mismatch");
	zassert_equal(got.advert_interval, cfg.advert_interval, "advert_interval mismatch");
	zassert_equal(got.flood_advert_interval, cfg.flood_advert_interval,
		      "flood_advert_interval mismatch");
	zassert_true(got.advert_position, "advert_position mismatch");
	zassert_equal(got.add_contact_config, cfg.add_contact_config, "add_contact_config mismatch");
	zassert_equal(got.telemetry_mode_base, cfg.telemetry_mode_base,
		      "telemetry_mode_base mismatch");
	zassert_equal(got.telemetry_mode_locat, cfg.telemetry_mode_locat,
		      "telemetry_mode_locat mismatch");
	zassert_equal(got.telemetry_mode_environment, cfg.telemetry_mode_environment,
		      "telemetry_mode_environment mismatch");
	zassert_equal(got.path_hash_size, cfg.path_hash_size, "path_hash_size mismatch");
	zassert_equal(got.loop_detect, cfg.loop_detect, "loop_detect mismatch");
	zassert_true(got.client_repeat, "client_repeat mismatch");
	zassert_equal(got.add_contact_hops_limit, cfg.add_contact_hops_limit,
		      "add_contact_hops_limit mismatch");
}

ZTEST(meshbus_contact_contract, test_contact_crud_iteration_and_prefix_match)
{
	meshbus_contact contact_a;
	meshbus_contact contact_b;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_contact conflict;
	uint8_t hash[1];
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	size_t count = 0U;
	size_t slot_id = 0U;
	int rc;

	build_contact(&contact_a, 0x10, "contact_a");
	build_contact(&contact_b, 0x20, "contact_b");

	rc = create_contact(&contact_a);
	zassert_ok(rc, "contact_a set failed: %d", rc);
	rc = create_contact(&contact_a);
	zassert_ok(rc, "duplicate contact set should update: %d", rc);

	rc = meshbus_contact_find_by_key(contact_a.public_key.bytes, &got);
	zassert_ok(rc, "contact_a get_by_key failed: %d", rc);
	zassert_true(strcmp(got.name, "contact_a") == 0, "contact_a name mismatch");
	zassert_true(strcmp(got.alias, "contact_a") == 0, "contact_a alias default mismatch");

	build_key_prefix(&contact_a, prefix);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_ok(rc, "contact_a get_by_prefix failed: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact_a.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "contact_a key mismatch");

	conflict = contact_a;
	conflict.public_key.bytes[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] ^= 0x3c;
	rc = create_contact(&conflict);
	zassert_equal(rc, -EADDRINUSE, "prefix conflict should fail with -EADDRINUSE, rc=%d", rc);

	rc = create_contact(&contact_b);
	zassert_ok(rc, "contact_b set failed: %d", rc);

	strncpy(contact_b.name, "contact_b2", sizeof(contact_b.name) - 1U);
	contact_b.name[sizeof(contact_b.name) - 1U] = '\0';
	strncpy(contact_b.alias, "contact_b_alias", sizeof(contact_b.alias) - 1U);
	contact_b.alias[sizeof(contact_b.alias) - 1U] = '\0';
	contact_b.last_seen_timestamp = 123U;
	contact_b.last_seen_snr = -7;
	rc = update_contact_alias_by_key(contact_b.public_key.bytes, "contact_b_alias");
	zassert_ok(rc, "contact_b alias update failed: %d", rc);
	rc = update_contact_seen_by_key(contact_b.public_key.bytes, 123U, true, -7);
	zassert_ok(rc, "contact_b update failed: %d", rc);

	rc = meshbus_contact_find_by_key(contact_b.public_key.bytes, &got);
	zassert_ok(rc, "contact_b get_by_key failed: %d", rc);
	zassert_true(strcmp(got.name, "contact_b") == 0, "contact_b name should not be user-updated");
	zassert_true(strcmp(got.alias, "contact_b_alias") == 0, "contact_b alias update mismatch");

	hash[0] = contact_a.public_key.bytes[0];
	rc = meshbus_contact_next_by_hash(hash, 0U, &slot_id, &got);
	zassert_ok(rc, "next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 0U, "expected contact_a slot 0, slot=%u", (unsigned int)slot_id);
	zassert_mem_equal(got.public_key.bytes, contact_a.public_key.bytes,
			  MESHBUS_CONTACT_PUBLIC_KEY_SIZE, "contact bytes mismatch");
	rc = meshbus_contact_next_by_hash(hash, slot_id + 1U, &slot_id, &got);
	zassert_equal(rc, -ENOENT, "next_by_hash should stop after last hash match: %d", rc);

	rc = meshbus_contact_get(0U, &got);
	zassert_ok(rc, "contact_get_by_index(0) failed: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact_a.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "contact index 0 mismatch");

	rc = meshbus_contact_get(1U, &got);
	zassert_ok(rc, "contact_get_by_index(1) failed: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact_b.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "contact index 1 mismatch");

	for (size_t index = 0U; index < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; index++) {
		rc = meshbus_contact_get(index, &got);
		if (rc == -ENOENT) {
			continue;
		}
		zassert_ok(rc, "contact_get_by_index(%u) failed: %d", (unsigned int)index, rc);
		count++;
	}
	zassert_equal(count, 2U, "contact_get_by_index count mismatch: %u", (unsigned int)count);

	rc = meshbus_contact_reset(contact_a.public_key.bytes);
	zassert_ok(rc, "contact_a reset failed: %d", rc);
	rc = meshbus_contact_reset(contact_a.public_key.bytes);
	zassert_ok(rc, "reset missing contact should be idempotent: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_management_secret_is_explicit)
{
	static const uint8_t secret[] = "contact-pass-123";
	static const uint8_t next_secret[] = "next-pass-456";
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	int rc;

	build_contact(&contact, 0x18, "management");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get failed: %d", rc);
	zassert_equal(got.management_secret.size, 0U,
		      "new contact should not have management secret");

	got.management_secret.size =
		MESHBUS_CONTACT_MANAGEMENT_SECRET_MIN_LEN - 1U;
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_equal(rc, -EINVAL,
		      "undersized management secret should fail: %d", rc);

	got.management_secret.size =
		MESHBUS_CONTACT_MANAGEMENT_SECRET_MAX_LEN + 1U;
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_equal(rc, -EINVAL,
		      "oversized management secret should fail: %d", rc);

	got.management_secret.size = MESHBUS_CONTACT_MANAGEMENT_SECRET_MIN_LEN;
	memset(got.management_secret.bytes, '1', got.management_secret.size);
	got.management_secret.bytes[0] = ' ';
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_equal(rc, -EINVAL,
		      "management password containing whitespace should fail: %d", rc);

	memset(got.management_secret.bytes, '1', got.management_secret.size);
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "all-digit management password should pass: %d", rc);

	got.management_secret.size = sizeof(secret) - 1U;
	memcpy(got.management_secret.bytes, secret, got.management_secret.size);
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "contact secret set failed: %d", rc);

	got = (meshbus_contact)meshbus_Contact_init_zero;
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after secret set failed: %d", rc);
	zassert_equal(got.management_secret.size, sizeof(secret) - 1U,
		      "secret length mismatch");
	zassert_mem_equal(got.management_secret.bytes, secret, sizeof(secret) - 1U,
			  "secret bytes mismatch");

	got.management_secret.size = sizeof(next_secret) - 1U;
	memcpy(got.management_secret.bytes, next_secret, got.management_secret.size);
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "contact secret overwrite failed: %d", rc);

	got = (meshbus_contact)meshbus_Contact_init_zero;
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after secret overwrite failed: %d", rc);
	zassert_equal(got.management_secret.size, sizeof(next_secret) - 1U,
		      "overwritten secret length mismatch");
	zassert_mem_equal(got.management_secret.bytes, next_secret,
			  sizeof(next_secret) - 1U, "overwritten secret bytes mismatch");

	memset(&got.management_secret, 0, sizeof(got.management_secret));
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "contact secret clear failed: %d", rc);

	got = (meshbus_contact)meshbus_Contact_init_zero;
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after secret clear failed: %d", rc);
	zassert_equal(got.management_secret.size, 0U,
		      "empty management secret should clear persisted bytes");
}

ZTEST(meshbus_contact_contract, test_contact_management_secret_abi_reserve_is_stable)
{
	zassert_equal(sizeof(((meshbus_contact *)0)->management_secret.bytes),
		      MESHBUS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY,
		      "generated management-secret ABI reserve changed");
	zassert_true(MESHBUS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY >
		     MESHBUS_CONTACT_MANAGEMENT_SECRET_MAX_LEN,
		     "ABI reserve must remain separate from accepted password length");
}

ZTEST(meshbus_contact_contract, test_contact_next_by_hash_cursor)
{
	meshbus_contact contact0;
	meshbus_contact contact1;
	meshbus_contact contact2;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t hash[1];
	size_t slot_id = 0U;
	int rc;

	build_contact(&contact0, 0x30, "hash_contact0");
	build_contact(&contact1, 0x31, "hash_contact1");
	build_contact(&contact2, 0x32, "hash_contact2");
	contact1.public_key.bytes[0] = contact0.public_key.bytes[0];
	contact1.public_key.bytes[1] ^= 0x55U;
	contact2.public_key.bytes[0] = contact0.public_key.bytes[0];
	contact2.public_key.bytes[1] ^= 0xaaU;

	rc = create_contact(&contact0);
	zassert_ok(rc, "contact0 create failed: %d", rc);
	rc = create_contact(&contact1);
	zassert_ok(rc, "contact1 create failed: %d", rc);
	rc = create_contact(&contact2);
	zassert_ok(rc, "contact2 create failed: %d", rc);

	hash[0] = contact0.public_key.bytes[0];
	rc = meshbus_contact_next_by_hash(hash, 0U, &slot_id, &got);
	zassert_ok(rc, "first next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 0U, "first hash match slot mismatch: %u",
		      (unsigned int)slot_id);
	zassert_mem_equal(got.public_key.bytes, contact0.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "first hash match key mismatch");

	rc = meshbus_contact_next_by_hash(hash, slot_id + 1U, &slot_id, &got);
	zassert_ok(rc, "second next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 1U, "second hash match slot mismatch: %u",
		      (unsigned int)slot_id);
	zassert_mem_equal(got.public_key.bytes, contact1.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "second hash match key mismatch");

	rc = meshbus_contact_next_by_hash(hash, slot_id + 1U, &slot_id, &got);
	zassert_ok(rc, "third next_by_hash failed: %d", rc);
	zassert_equal(slot_id, 2U, "third hash match slot mismatch: %u",
		      (unsigned int)slot_id);
	zassert_mem_equal(got.public_key.bytes, contact2.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "third hash match key mismatch");

	rc = meshbus_contact_next_by_hash(hash, slot_id + 1U, &slot_id, &got);
	zassert_equal(rc, -ENOENT, "next_by_hash should return -ENOENT after cursor end: %d",
		      rc);

	hash[0] = 0xffU;
	rc = meshbus_contact_next_by_hash(hash, 0U, &slot_id, &got);
	zassert_equal(rc, -ENOENT, "next_by_hash should return -ENOENT for missing hash: %d",
		      rc);
	rc = meshbus_contact_next_by_hash(hash, CONFIG_MESHBUS_CONTACT_MAX_CONTACTS, &slot_id, &got);
	zassert_equal(rc, -ENOENT, "next_by_hash should return -ENOENT past store end: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_reset_clears_persisted_slot)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact_response_advert_event advert_evt = {0};
	static const uint8_t out_path[] = {0x42, 0x43, 0x44};
	static const uint8_t raw_advert[] = {0x51, 0x52, 0x53};
	int rc;

	build_contact(&contact, 0x78, "contact_reset_slot");
	strncpy(contact.alias, "reset_alias", sizeof(contact.alias) - 1U);
	contact.alias[sizeof(contact.alias) - 1U] = '\0';
	contact.role = MESHBUS_CONTACT_ROLE_SENSOR;
	contact.out_path.size = sizeof(out_path);
	memcpy(contact.out_path.bytes, out_path, sizeof(out_path));
	contact.is_neighbor = false;
	contact.first_seen_timestamp = 11U;
	contact.flags = MESHBUS_CONTACT_FLAG_FAVORITE;
	contact.latitude = 123;
	contact.longitude = -456;
	contact.last_seen_timestamp = 99U;
	contact.last_seen_snr = -7;
	contact.path_hash_size = 3U;

	rc = meshbus_contact_set(contact.public_key.bytes, &contact);
	zassert_ok(rc, "contact set failed: %d", rc);

	memcpy(advert_evt.public_key, contact.public_key.bytes, sizeof(advert_evt.public_key));
	memcpy(advert_evt.name, contact.name, sizeof(advert_evt.name));
	advert_evt.role = contact.role;
	advert_evt.raw_advert_len = sizeof(raw_advert);
	memcpy(advert_evt.raw_advert, raw_advert, sizeof(raw_advert));
	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert response raw persist publish failed: %d", rc);

	rc = meshbus_contact_reset(contact.public_key.bytes);
	zassert_ok(rc, "contact reset failed: %d", rc);

	assert_contact_blob_deleted(0U);
	assert_contact_advert_raw_blob_deleted(0U);
}

ZTEST(meshbus_contact_contract, test_contact_store_tracks_contact_lifecycle)
{
	meshbus_contact contact_a;
	meshbus_contact contact_b;
	meshbus_contact got = meshbus_Contact_init_zero;
	int rc;

	zassert_equal(meshbus_contact_store_count(), 0U, "initial contact_count should be 0");
	zassert_equal(meshbus_contact_store_size(), CONFIG_MESHBUS_CONTACT_MAX_CONTACTS,
		      "contact store size mismatch");

	build_contact(&contact_a, 0x70, "contact_count_a");
	contact_b = contact_a;
	for (size_t i = 1U; i < CONFIG_MESHBUS_CONTACT_PREFIX_BYTES; i++) {
		contact_b.public_key.bytes[i] ^= (uint8_t)(0x10U + i);
	}
	contact_b.public_key.bytes[MESHBUS_CONTACT_PUBLIC_KEY_SIZE - 1U] ^= 0x5aU;
	strncpy(contact_b.name, "contact_count_b", sizeof(contact_b.name) - 1U);
	contact_b.name[sizeof(contact_b.name) - 1U] = '\0';

	rc = create_contact(&contact_a);
	zassert_ok(rc, "contact_a set failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 1U,
		      "contact_count mismatch after contact_a set");

	rc = create_contact(&contact_a);
	zassert_ok(rc, "duplicate contact_a set should update: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 1U,
		      "contact_count should not change on duplicate set");

	rc = create_contact(&contact_b);
	zassert_ok(rc, "contact_b set failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 2U,
		      "contact_count mismatch after contact_b set");

	rc = update_contact_seen_by_key(contact_b.public_key.bytes, 456U, false, 0);
	zassert_ok(rc, "contact_b seen update failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 2U,
		      "contact_count should not change on update");
	rc = meshbus_contact_find_by_key(contact_b.public_key.bytes, &got);
	zassert_ok(rc, "contact_b get after seen update failed: %d", rc);
	zassert_equal(got.first_seen_timestamp, 0U,
		      "full contact update path should not invent first_seen");

	rc = meshbus_contact_reset(contact_a.public_key.bytes);
	zassert_ok(rc, "contact_a reset failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 1U,
		      "contact_count mismatch after contact_a reset");

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 0U,
		      "contact_count mismatch after clearing contacts");
}

ZTEST(meshbus_contact_contract, test_contact_set_updates_by_prefix_and_reset_is_idempotent)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact updated = meshbus_Contact_init_zero;
	meshbus_contact conflict = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	size_t first_index = CONFIG_MESHBUS_CONTACT_MAX_CONTACTS;
	size_t second_index = CONFIG_MESHBUS_CONTACT_MAX_CONTACTS;
	int rc;

	build_contact(&contact, 0x72, "contact_set");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact set failed: %d", rc);
	rc = find_contact_index_by_prefix(contact.public_key.bytes, &first_index);
	zassert_ok(rc, "contact set index lookup failed: %d", rc);
	zassert_true(first_index < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS,
		     "contact set used invalid index");

	updated = contact;
	strncpy(updated.alias, "contact_set_alias", sizeof(updated.alias) - 1U);
	updated.alias[sizeof(updated.alias) - 1U] = '\0';
	rc = create_contact(&updated);
	zassert_ok(rc, "contact set update failed: %d", rc);
	rc = find_contact_index_by_prefix(contact.public_key.bytes, &second_index);
	zassert_ok(rc, "contact set update index lookup failed: %d", rc);
	zassert_equal(second_index, first_index, "contact set should update the same slot");

	rc = meshbus_contact_get(first_index, &got);
	zassert_ok(rc, "contact get after set failed: %d", rc);
	zassert_mem_equal(got.public_key.bytes, updated.public_key.bytes,
			  MESHBUS_CONTACT_PUBLIC_KEY_SIZE, "contact set public key mismatch");
	zassert_equal(strcmp(got.alias, updated.alias), 0, "contact set alias mismatch");

	updated.flags = MESHBUS_CONTACT_FLAG_FAVORITE |
			MESHBUS_CONTACT_FLAG_TELEMETRY_BASE;
	rc = create_contact(&updated);
	zassert_ok(rc, "contact set flags failed: %d", rc);
	rc = meshbus_contact_get(first_index, &got);
	zassert_ok(rc, "contact get after flags set failed: %d", rc);
	zassert_equal(got.flags, updated.flags, "contact set should update flags");

	updated.flags = 0U;
	rc = create_contact(&updated);
	zassert_ok(rc, "contact clear flags failed: %d", rc);
	rc = meshbus_contact_get(first_index, &got);
	zassert_ok(rc, "contact get after flags clear failed: %d", rc);
	zassert_equal(got.flags, 0U, "contact set should clear flags");

	conflict = contact;
	conflict.public_key.bytes[MESHBUS_CONTACT_PUBLIC_KEY_SIZE - 1U] ^= 0x55U;
	rc = create_contact(&conflict);
	zassert_equal(rc, -EADDRINUSE, "prefix conflict should fail: %d", rc);

	rc = meshbus_contact_reset(contact.public_key.bytes);
	zassert_ok(rc, "contact reset failed: %d", rc);
	rc = meshbus_contact_get(first_index, &got);
	zassert_equal(rc, -ENOENT, "contact slot should be empty after reset: %d", rc);
	rc = meshbus_contact_reset(contact.public_key.bytes);
	zassert_ok(rc, "contact reset should be idempotent: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_insert_rejects_existing_and_preserves_fields)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	static const uint8_t out_path[] = {0x11, 0x22, 0x33};
	static const char secret[] = "operator-secret1";
	int rc;

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);

	build_contact(&contact, 0x91, "operator_old");
	contact.role = MESHBUS_CONTACT_ROLE_REPEATER;
	contact.management_secret.size = (pb_size_t)strlen(secret);
	memcpy(contact.management_secret.bytes, secret, strlen(secret));
	rc = create_contact(&contact);
	zassert_ok(rc, "operator contact create failed: %d", rc);
	rc = update_contact_alias_by_key(contact.public_key.bytes, "user_alias");
	zassert_ok(rc, "operator alias update failed: %d", rc);
	rc = update_contact_path_by_key(contact.public_key.bytes, out_path, sizeof(out_path));
	zassert_ok(rc, "operator path update failed: %d", rc);
	rc = update_contact_flags_by_key(contact.public_key.bytes, 0U,
				      MESHBUS_CONTACT_FLAG_FAVORITE |
					      MESHBUS_CONTACT_FLAG_TELEMETRY_BASE);
	zassert_ok(rc, "operator flags update failed: %d", rc);

	rc = meshbus_contact_insert(contact.public_key.bytes, "new_name",
				    MESHBUS_CONTACT_ROLE_CHAT);
	zassert_equal(rc, -EEXIST, "existing contact insert should fail: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "operator get after duplicate insert failed: %d", rc);
	zassert_equal(got.role, MESHBUS_CONTACT_ROLE_REPEATER,
		      "operator role should be preserved");
	zassert_true(strcmp(got.name, "operator_old") == 0, "operator name should be preserved");
	zassert_true(strcmp(got.alias, "user_alias") == 0,
		     "operator alias should be preserved");
	zassert_equal(got.flags,
		      MESHBUS_CONTACT_FLAG_FAVORITE | MESHBUS_CONTACT_FLAG_TELEMETRY_BASE,
		      "operator flags should be preserved");
	zassert_equal(got.out_path.size, sizeof(out_path), "operator path len mismatch");
	zassert_mem_equal(got.out_path.bytes, out_path, sizeof(out_path),
			  "operator path should be preserved");
	zassert_equal(got.management_secret.size, strlen(secret),
		      "operator secret length should be preserved");
	zassert_mem_equal(got.management_secret.bytes, secret, strlen(secret),
			  "operator secret should be preserved");
	zassert_equal(got.first_seen_timestamp, 0U, "operator first_seen should be preserved");
	zassert_true(got.last_seen_timestamp != 0U, "operator last_seen should be preserved");

	fill_bytes(contact.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE, 0x92);
	rc = meshbus_contact_insert(contact.public_key.bytes, "new_sensor",
				    MESHBUS_CONTACT_ROLE_SENSOR);
	zassert_ok(rc, "new contact insert failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "new contact get failed: %d", rc);
	zassert_equal(got.role, MESHBUS_CONTACT_ROLE_SENSOR, "new contact role mismatch");
	zassert_true(strcmp(got.name, "new_sensor") == 0, "new contact name mismatch");
	zassert_true(strcmp(got.alias, "new_sensor") == 0, "new contact alias mismatch");
	zassert_equal(got.management_secret.size, 0U,
		      "new contact should not get a management secret");
	zassert_true(got.first_seen_timestamp != 0U, "new contact first_seen should be set");
	zassert_true(got.last_seen_timestamp != 0U, "new contact last_seen should be set");
}

ZTEST(meshbus_contact_contract, test_contact_insert_full_table_overwrites_oldest_non_favorite)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t favorite_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t victim_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t extra_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	int rc;

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config = MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE |
				 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	for (size_t i = 0U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		build_contact(&contact, (uint8_t)(0xb0U + i), "operator_fill");
		rc = create_contact(&contact);
		zassert_ok(rc, "contact fill create failed at i=%u rc=%d",
			   (unsigned int)i, rc);
		rc = update_contact_seen_by_key(contact.public_key.bytes, (uint32_t)(10U + i),
					     false, 0);
		zassert_ok(rc, "contact seen update failed at i=%u rc=%d",
			   (unsigned int)i, rc);
		if (i == 0U) {
			memcpy(favorite_key, contact.public_key.bytes, sizeof(favorite_key));
			rc = update_contact_flags_by_key(contact.public_key.bytes, 0U,
						      MESHBUS_CONTACT_FLAG_FAVORITE);
			zassert_ok(rc, "favorite update failed: %d", rc);
		} else if (i == 1U) {
			memcpy(victim_key, contact.public_key.bytes, sizeof(victim_key));
		}
	}

	fill_bytes(extra_key, sizeof(extra_key), 0xe1);
	rc = meshbus_contact_insert(extra_key, "room_extra", MESHBUS_CONTACT_ROLE_ROOM);
	zassert_ok(rc, "extra contact insert failed: %d", rc);
	rc = meshbus_contact_find_by_key(favorite_key, &got);
	zassert_ok(rc, "favorite operator should not be evicted: %d", rc);
	rc = meshbus_contact_find_by_key(victim_key, &got);
	zassert_equal(rc, -ENOENT, "oldest non-favorite should be evicted: %d", rc);
	rc = meshbus_contact_find_by_key(extra_key, &got);
	zassert_ok(rc, "extra contact should exist: %d", rc);
	zassert_equal(got.role, MESHBUS_CONTACT_ROLE_ROOM, "extra contact role mismatch");
	zassert_true(strcmp(got.alias, "room_extra") == 0,
		     "extra contact alias mismatch");
	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts after operator eviction failed: %d", rc);
}

ZTEST(meshbus_contact_contract, test_failed_eviction_preserves_victim_record)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t victim_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t extra_key[MESHBUS_CONTACT_PUBLIC_KEY_SIZE];
	size_t victim_slot = 0U;
	int rc;

	zassert_ok(meshbus_meshcore_config_get(&cfg), "config_get failed");
	cfg.add_contact_config = MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE |
				 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	zassert_ok(meshbus_meshcore_config_set(&cfg), "config_set failed");

	for (size_t i = 0U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		build_contact(&contact, (uint8_t)(0xd0U + i), "failed_eviction_fill");
		zassert_ok(create_contact(&contact), "contact fill failed at i=%u",
			   (unsigned int)i);
		zassert_ok(update_contact_seen_by_key(contact.public_key.bytes,
					      (uint32_t)(10U + i), false, 0),
			   "contact timestamp update failed at i=%u", (unsigned int)i);
		if (i == 0U) {
			memcpy(victim_key, contact.public_key.bytes, sizeof(victim_key));
			zassert_ok(find_contact_index_by_prefix(victim_key, &victim_slot),
				   "victim slot lookup failed");
		}
	}

	fill_bytes(extra_key, sizeof(extra_key), 0xf0U);
	contact_blob_save_fail_once();
	rc = meshbus_contact_insert(extra_key, "failed_replacement", MESHBUS_CONTACT_ROLE_ROOM);
	contact_save_gate_disarm();
	zassert_equal(rc, -EIO, "injected replacement failure mismatch: %d", rc);
	zassert_equal(meshbus_contact_store_count(), CONFIG_MESHBUS_CONTACT_MAX_CONTACTS,
		      "failed replacement changed contact count");
	rc = meshbus_contact_find_by_key(victim_key, &got);
	zassert_ok(rc, "failed replacement lost the eviction victim: %d", rc);
	rc = meshbus_contact_find_by_key(extra_key, &got);
	zassert_equal(rc, -ENOENT, "failed replacement should not be indexed: %d", rc);
	reload_contact_settings_index();
	rc = meshbus_contact_find_by_key(victim_key, &got);
	zassert_ok(rc, "eviction victim missing after settings reload: %d", rc);
	rc = meshbus_contact_get(victim_slot, &got);
	zassert_ok(rc, "victim slot blob missing after failed replacement: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_rebuilds_slot_index)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] = {0};
	static const uint8_t out_path[] = {0x01, 0x23, 0x45};
	int rc;

	build_contact(&contact, 0x71, "contact_restore");
	strncpy(contact.alias, "restore_alias", sizeof(contact.alias) - 1U);
	contact.alias[sizeof(contact.alias) - 1U] = '\0';
	contact.role = MESHBUS_CONTACT_ROLE_SENSOR;
	contact.out_path.size = sizeof(out_path);
	memcpy(contact.out_path.bytes, out_path, sizeof(out_path));
	contact.is_neighbor = false;
	contact.first_seen_timestamp = 11U;
	contact.flags = MESHBUS_CONTACT_FLAG_FAVORITE;
	contact.latitude = 1234;
	contact.longitude = -5678;
	contact.last_seen_timestamp = 99U;
	contact.last_seen_snr = -7;
	contact.path_hash_size = 3U;

	save_contact_blob(0U, &contact);

	reload_contact_settings_index();

	zassert_equal(meshbus_contact_store_count(), 1U, "restored contact_count mismatch");

	build_key_prefix(&contact, prefix);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_ok(rc, "contact_find_by_prefix failed after restore: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "restored public_key mismatch");
	zassert_true(strcmp(got.name, contact.name) == 0, "restored name mismatch");
	zassert_true(strcmp(got.alias, contact.alias) == 0, "restored alias mismatch");
	zassert_equal(got.role, contact.role, "restored role mismatch");
	zassert_equal(got.out_path.size, contact.out_path.size, "restored out_path size mismatch");
	zassert_mem_equal(got.out_path.bytes, contact.out_path.bytes, contact.out_path.size,
			  "restored out_path mismatch");
	zassert_equal(got.first_seen_timestamp, contact.first_seen_timestamp,
		      "restored first_seen mismatch");
	zassert_equal(got.flags, contact.flags, "restored flags mismatch");
	zassert_equal(got.latitude, contact.latitude, "restored latitude mismatch");
	zassert_equal(got.longitude, contact.longitude, "restored longitude mismatch");
	zassert_equal(got.last_seen_timestamp, contact.last_seen_timestamp,
		      "restored last_seen timestamp mismatch");
	zassert_equal(got.last_seen_snr, contact.last_seen_snr, "restored last_seen snr mismatch");
	zassert_equal(got.path_hash_size, contact.path_hash_size, "restored path_hash_size mismatch");
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_sanitizes_legacy_management_secrets)
{
	static const size_t legacy_lengths[] = {
		MESHBUS_CONTACT_MANAGEMENT_SECRET_MAX_LEN + 1U,
		32U,
		MESHBUS_CONTACT_MANAGEMENT_SECRET_ABI_CAPACITY,
	};
	meshbus_contact contacts[ARRAY_SIZE(legacy_lengths)];
	meshbus_contact got = meshbus_Contact_init_zero;

	for (size_t i = 0U; i < ARRAY_SIZE(legacy_lengths); i++) {
		build_contact(&contacts[i], (uint8_t)(0x81U + i), "legacy_secret");
		contacts[i].management_secret.size = (pb_size_t)legacy_lengths[i];
		memset(contacts[i].management_secret.bytes, (int)('A' + i),
		       legacy_lengths[i]);
		save_contact_blob(i, &contacts[i]);
	}

	reload_contact_settings_index();
	zassert_equal(meshbus_contact_store_count(), ARRAY_SIZE(legacy_lengths),
		      "legacy contacts were not all restored");

	for (size_t i = 0U; i < ARRAY_SIZE(legacy_lengths); i++) {
		zassert_ok(meshbus_contact_get(i, &got),
			   "legacy contact index was not preserved: slot=%u", (unsigned int)i);
		zassert_mem_equal(got.public_key.bytes, contacts[i].public_key.bytes,
				  MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
				  "legacy contact key changed: slot=%u", (unsigned int)i);
		zassert_equal(got.management_secret.size, 0U,
			      "legacy password reached the public read path: slot=%u",
			      (unsigned int)i);

		load_contact_blob(i, &got);
		zassert_equal(got.management_secret.size, 0U,
			      "legacy password was not rewritten empty: slot=%u",
			      (unsigned int)i);
	}
}

ZTEST(meshbus_contact_contract,
	  test_contact_blob_restore_hides_legacy_secret_when_rewrite_fails)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	const size_t legacy_len = 32U;
	const size_t slot = 6U;

	build_contact(&contact, 0x89U, "legacy_retry");
	contact.management_secret.size = (pb_size_t)legacy_len;
	memset(contact.management_secret.bytes, 'L', legacy_len);
	save_contact_blob(slot, &contact);

	contact_blob_save_fail_once();
	reload_contact_settings_index();

	zassert_ok(meshbus_contact_get(slot, &got),
		   "legacy contact disappeared after cleanup failure");
	zassert_mem_equal(got.public_key.bytes, contact.public_key.bytes,
			  MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "cleanup failure changed the contact index identity");
	zassert_equal(got.management_secret.size, 0U,
		      "failed cleanup exposed the legacy password");

	load_contact_blob(slot, &got);
	zassert_equal(got.management_secret.size, legacy_len,
		      "fault injection did not leave the legacy record for retry");

	reload_contact_settings_index();
	load_contact_blob(slot, &got);
	zassert_equal(got.management_secret.size, 0U,
		      "legacy password cleanup was not retried successfully");
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_ignores_invalid_public_key)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact invalid = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] = {0};
	int rc;

	build_contact(&contact, 0x72, "contact_orphan");
	build_key_prefix(&contact, prefix);

	invalid = contact;
	invalid.public_key.size = MESHBUS_CONTACT_PUBLIC_KEY_SIZE - 1U;
	save_contact_blob(2U, &invalid);

	reload_contact_settings_index();

	zassert_equal(meshbus_contact_store_count(), 0U,
		      "invalid contact blob should not restore any contact");
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_equal(rc, -ENOENT, "invalid contact blob should not create contact, rc=%d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_missing_optional_fields_use_defaults)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] = {0};
	int rc;

	build_contact(&contact, 0x73, "contact_defaults");
	contact.role = MESHBUS_CONTACT_ROLE_ROOM;

	save_contact_blob(4U, &contact);

	reload_contact_settings_index();

	zassert_equal(meshbus_contact_store_count(), 1U, "restored contact_count mismatch");

	build_key_prefix(&contact, prefix);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_ok(rc, "contact_find_by_prefix failed after defaults restore: %d", rc);
	zassert_true(strcmp(got.name, contact.name) == 0, "restored name mismatch");
	zassert_equal(got.role, contact.role, "restored role mismatch");
	zassert_equal(got.out_path.size, 0U, "missing out_path should restore as empty");
	zassert_false(got.is_neighbor, "missing is_neighbor should restore as false");
	zassert_equal(got.first_seen_timestamp, 0U,
		      "missing first_seen_timestamp should restore as zero");
	zassert_equal(got.flags, 0U, "missing flags should restore as zero");
	zassert_equal(got.latitude, 0, "missing latitude should restore as zero");
	zassert_equal(got.longitude, 0, "missing longitude should restore as zero");
	zassert_equal(got.last_seen_timestamp, 0U,
		      "missing last_seen_timestamp should restore as zero");
	zassert_equal(got.last_seen_snr, 0, "missing last_seen_snr should restore as zero");
	zassert_equal(got.path_hash_size, 0U,
		      "missing path_hash_size should restore as zero");
	zassert_true(got.alias[0] == '\0', "missing alias should restore as empty");
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_rejects_invalid_blob)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] = {0};
	uint32_t invalid_path_hash_size = MESHBUS_CONTACT_PATH_HASH_SIZE_MAX + 1U;
	int rc;

	build_contact(&contact, 0x74, "contact_invalid_leaf");
	contact.role = MESHBUS_CONTACT_ROLE_SENSOR;
	strncpy(contact.alias, "valid_alias", sizeof(contact.alias) - 1U);
	contact.alias[sizeof(contact.alias) - 1U] = '\0';
	contact.path_hash_size = invalid_path_hash_size;

	save_contact_blob(5U, &contact);

	reload_contact_settings_index();

	zassert_equal(meshbus_contact_store_count(), 0U,
		      "invalid contact blob should not restore contact_count");

	build_key_prefix(&contact, prefix);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_equal(rc, -ENOENT, "invalid contact blob should not be indexed: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_blob_restore_sparse_slots_keep_index_identity)
{
	meshbus_contact contact0 = meshbus_Contact_init_zero;
	meshbus_contact contact3 = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	int rc;

	build_contact(&contact0, 0x75, "contact_slot0");
	contact0.role = MESHBUS_CONTACT_ROLE_CHAT;
	build_contact(&contact3, 0x76, "contact_slot3");
	contact3.role = MESHBUS_CONTACT_ROLE_SENSOR;

	save_contact_blob(0U, &contact0);
	save_contact_blob(3U, &contact3);

	reload_contact_settings_index();

	zassert_equal(meshbus_contact_store_count(), 2U, "restored sparse contact_count mismatch");

	rc = meshbus_contact_get(0U, &got);
	zassert_ok(rc, "contact_get(0) failed after sparse restore: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact0.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "slot 0 public_key mismatch");
	zassert_true(strcmp(got.name, contact0.name) == 0, "slot 0 name mismatch");

	rc = meshbus_contact_get(1U, &got);
	zassert_equal(rc, -ENOENT, "contact_get(1) should stay empty after sparse restore, rc=%d", rc);

	rc = meshbus_contact_get(3U, &got);
	zassert_ok(rc, "contact_get(3) failed after sparse restore: %d", rc);
	zassert_mem_equal(got.public_key.bytes, contact3.public_key.bytes, MESHBUS_CONTACT_PUBLIC_KEY_SIZE,
			  "slot 3 public_key mismatch");
	zassert_true(strcmp(got.name, contact3.name) == 0, "slot 3 name mismatch");
}

ZTEST(meshbus_contact_contract, test_contact_reset_and_missing_paths)
{
	meshbus_contact contact;
	meshbus_contact got = meshbus_Contact_init_zero;
	uint8_t hash[1];
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	size_t slot_id = 0U;
	size_t index = 0U;
	int rc;

	build_contact(&contact, 0x55, "contact_reset");
	build_key_prefix(&contact, prefix);
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
	reload_contact_settings_index();

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_equal(rc, -ENOENT, "get_by_key after reset should be -ENOENT, rc=%d", rc);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_equal(rc, -ENOENT, "get_by_prefix after reset should be -ENOENT, rc=%d", rc);

	hash[0] = contact.public_key.bytes[0];
	rc = meshbus_contact_next_by_hash(hash, 0U, &slot_id, &got);
	zassert_equal(rc, -ENOENT, "next_by_hash after reset should be -ENOENT, rc=%d", rc);

	rc = update_contact_alias_by_key(contact.public_key.bytes, "missing");
	zassert_equal(rc, -ENOENT, "update missing contact should be -ENOENT, rc=%d", rc);
	rc = meshbus_contact_reset(contact.public_key.bytes);
	zassert_ok(rc, "reset missing contact should be idempotent: %d", rc);
	rc = meshbus_contact_get(index, &got);
	zassert_equal(rc, -ENOENT, "contact_get_by_index on empty table should be -ENOENT, rc=%d", rc);
}

ZTEST(meshbus_contact_contract, test_config_change_keeps_runtime_contact_state)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES] = {0};
	int rc;

	build_contact(&contact, 0x56, "contact_repeater_clear");
	build_key_prefix(&contact, prefix);
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 1U, "contact count before repeater mismatch");

	build_default_cfg(&cfg, 0x57);
	cfg.disable_fwd = true;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	zassert_equal(meshbus_contact_store_count(), 1U,
		      "contact count should stay after config change");
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact_find_by_key should keep runtime contact before reboot, rc=%d", rc);
	rc = meshbus_contact_find_by_prefix(prefix, &got);
	zassert_ok(rc, "contact_find_by_prefix should keep runtime contact before reboot, rc=%d", rc);
	rc = meshbus_contact_get(0U, &got);
	zassert_ok(rc, "contact_get should keep runtime contact before reboot, rc=%d", rc);
}

ZTEST(meshbus_contact_contract, test_unchanged_update_ignores_unused_bytes_and_skips_write)
{
	meshbus_contact contact;
	meshbus_contact got;

	build_contact(&contact, 0x59, "unchanged");
	zassert_ok(create_contact(&contact));
	zassert_ok(meshbus_contact_find_by_key(contact.public_key.bytes, &contact));
	zassert_equal(contact.out_path.size, 0);
	zassert_equal(contact.management_secret.size, 0);
	memset(contact.out_path.bytes, 0xa5, sizeof(contact.out_path.bytes));
	memset(contact.management_secret.bytes, 0x5a, sizeof(contact.management_secret.bytes));

	contact_blob_save_fail_once();
	zassert_ok(meshbus_contact_set(contact.public_key.bytes, &contact));
	zassert_equal(atomic_get(&contact_blob_save_fail_next), 1,
		      "unchanged update must not attempt persistence");

	strcpy(contact.alias, "changed");
	zassert_equal(meshbus_contact_set(contact.public_key.bytes, &contact), -EIO);
	zassert_ok(meshbus_contact_find_by_key(contact.public_key.bytes, &got));
	zassert_equal(strcmp(got.alias, "unchanged"), 0);
	zassert_ok(meshbus_contact_set(contact.public_key.bytes, &contact));
	zassert_ok(meshbus_contact_find_by_key(contact.public_key.bytes, &got));
	zassert_equal(strcmp(got.alias, "changed"), 0);
}

ZTEST(meshbus_contact_contract, test_contact_alias_default_and_name_update_ignored)
{
	const uint64_t target_unix_ms = 1893456899000ULL;
	const uint32_t target_unix_s = (uint32_t)(target_unix_ms / MSEC_PER_SEC);
	const struct timespec target_time = {
		.tv_sec = (time_t)target_unix_s,
		.tv_nsec = (long)((target_unix_ms % MSEC_PER_SEC) * NSEC_PER_MSEC),
	};
	meshbus_contact contact;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_contact_response_advert_event advert_evt = {0};
	uint32_t first_seen_after_advert = 0U;
	int rc;

	build_contact(&contact, 0x5a, "contact_alias");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get failed: %d", rc);
	zassert_true(strcmp(got.alias, "contact_alias") == 0, "alias default should follow name");
	zassert_equal(got.first_seen_timestamp, 0U, "newly created contact should start with zero first_seen");

	rc = update_contact_alias_by_key(got.public_key.bytes, "user_alias");
	zassert_ok(rc, "contact user update failed: %d", rc);

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after user update failed: %d", rc);
	zassert_true(strcmp(got.name, "contact_alias") == 0, "user update should not change name");
	zassert_true(strcmp(got.alias, "user_alias") == 0, "user update should change alias");

	zassert_ok(sys_clock_settime(SYS_CLOCK_REALTIME, &target_time));
	zassert_true(meshbus_time_realtime_is_valid(), "test realtime should be valid");

	memcpy(advert_evt.public_key, got.public_key.bytes, sizeof(advert_evt.public_key));
	strncpy(advert_evt.name, "advert_name", sizeof(advert_evt.name) - 1U);
	advert_evt.name[sizeof(advert_evt.name) - 1U] = '\0';
	advert_evt.role = contact.role;
	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert update publish failed: %d", rc);

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after advert update failed: %d", rc);
	zassert_true(strcmp(got.name, "advert_name") == 0, "advert update should change name");
	zassert_true(strcmp(got.alias, "user_alias") == 0, "advert update should keep alias");
	zassert_equal(got.role, contact.role, "advert update should keep role");
	zassert_true(got.first_seen_timestamp != 0U,
		     "advert update should initialize first_seen");
	zassert_true(got.first_seen_timestamp >= target_unix_s,
		     "advert first_seen should use synchronized realtime");
	zassert_true(got.first_seen_timestamp < target_unix_s + 10U,
		     "advert first_seen drift too large");
	first_seen_after_advert = got.first_seen_timestamp;
	zassert_equal(got.last_seen_timestamp, first_seen_after_advert,
		      "first advert response should align first_seen and last_seen");
	zassert_equal(got.path_hash_size, 0U,
		      "advert without path metadata should not initialize path_hash_size");
	zassert_false(got.is_neighbor, "advert without path metadata should keep unknown path");
}

ZTEST(meshbus_contact_contract, test_advert_path_metadata_updates_neighbor_and_relay_path)
{
	meshbus_contact_response_advert_event advert_evt = {0};
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	static const uint8_t relay_path[] = {0x11, 0x22, 0x33, 0x44};
	int rc;

	fill_bytes(advert_evt.public_key, sizeof(advert_evt.public_key), 0x9b);
	strncpy(advert_evt.name, "relay_advert", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_CHAT;
	advert_evt.has_out_path = true;
	advert_evt.path_hash_size = 2U;
	advert_evt.out_path_len = sizeof(relay_path);
	memcpy(advert_evt.out_path, relay_path, sizeof(relay_path));
	drain_contact_notify_listener();

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "relayed advert publish failed: %d", rc);
	rc = wait_contact_notify_counts(1U, 1U);
	zassert_ok(rc, "relayed advert notify wait failed: %d", rc);
	rc = meshbus_contact_find_by_key(advert_evt.public_key, &got);
	zassert_ok(rc, "relayed advert contact lookup failed: %d", rc);
	zassert_false(got.is_neighbor, "relayed advert should not mark neighbor");
	zassert_equal(got.path_hash_size, 2U, "relayed advert path_hash_size mismatch");
	zassert_equal(got.out_path.size, sizeof(relay_path), "relayed advert path len mismatch");
	zassert_mem_equal(got.out_path.bytes, relay_path, sizeof(relay_path),
			  "relayed advert path mismatch");
	notify_evt = last_contact_advert_notify;
	notify_payload = decode_notify_event(&notify_evt);
	zassert_true(notify_payload.payload_variant.node_advert.has_out_path,
		     "relayed advert notify has_out_path mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.path_hash_size, 2U,
		      "relayed advert notify path_hash_size mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.out_path.size,
		      sizeof(relay_path), "relayed advert notify path len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_advert.out_path.bytes,
			  relay_path, sizeof(relay_path), "relayed advert notify path mismatch");

	memset(&advert_evt, 0, sizeof(advert_evt));
	memcpy(advert_evt.public_key, got.public_key.bytes, sizeof(advert_evt.public_key));
	strncpy(advert_evt.name, "direct_advert", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_CHAT;
	advert_evt.has_out_path = true;
	advert_evt.path_hash_size = 2U;
	drain_contact_notify_listener();

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "zero-hop advert publish failed: %d", rc);
	rc = wait_contact_notify_counts(1U, 1U);
	zassert_ok(rc, "zero-hop advert notify wait failed: %d", rc);
	rc = meshbus_contact_find_by_key(advert_evt.public_key, &got);
	zassert_ok(rc, "zero-hop advert contact lookup failed: %d", rc);
	zassert_true(got.is_neighbor, "zero-hop advert should mark neighbor");
	zassert_equal(got.path_hash_size, 2U, "zero-hop advert path_hash_size mismatch");
	zassert_equal(got.out_path.size, 0U, "zero-hop advert should clear out_path");
	notify_evt = last_contact_advert_notify;
	notify_payload = decode_notify_event(&notify_evt);
	zassert_true(notify_payload.payload_variant.node_advert.has_out_path,
		     "zero-hop advert notify has_out_path mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.path_hash_size, 2U,
		      "zero-hop advert notify path_hash_size mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.out_path.size, 0U,
		      "zero-hop advert notify out_path should be empty");
}

ZTEST(meshbus_contact_contract, test_contact_advert_request_reads_slot_raw_and_handles_cleanup)
{
	meshbus_contact contact0;
	meshbus_contact extra;
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact_share_request_event req_evt = {0};
	meshbus_contact_response_advert_event advert_evt = {0};
	uint8_t prefix0[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	uint8_t prefix_extra[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	static const uint8_t raw0[] = {0x11, 0x22, 0x33, 0x44};
	int rc;

	build_contact(&contact0, 0x80, "contact_raw_0");
	rc = create_contact(&contact0);
	zassert_ok(rc, "contact0 create failed: %d", rc);
	build_key_prefix(&contact0, prefix0);

	rc = meshbus_contact_share_request(prefix0);
	zassert_equal(rc, -ENODATA, "contact with no advert raw should return -ENODATA, rc=%d", rc);

	memcpy(advert_evt.public_key, contact0.public_key.bytes, sizeof(advert_evt.public_key));
	memcpy(advert_evt.name, contact0.name, sizeof(advert_evt.name));
	advert_evt.role = contact0.role;
	advert_evt.raw_advert_len = sizeof(raw0);
	memcpy(advert_evt.raw_advert, raw0, sizeof(raw0));
	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert response raw persist publish failed: %d", rc);

	rc = meshbus_contact_share_request(prefix0);
	zassert_ok(rc, "contact_advert_request failed: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_share_request_chan, &req_evt, K_NO_WAIT);
	zassert_ok(rc, "contact_advert_request channel read failed: %d", rc);
	zassert_mem_equal(req_evt.key_prefix, prefix0, sizeof(prefix0), "contact_advert_request prefix mismatch");
	zassert_equal(req_evt.raw_advert_len, sizeof(raw0), "contact_advert_request raw len mismatch");
	zassert_mem_equal(req_evt.raw_advert, raw0, sizeof(raw0), "contact_advert_request raw payload mismatch");

	rc = meshbus_contact_reset(contact0.public_key.bytes);
	zassert_ok(rc, "contact0 reset failed: %d", rc);
	rc = meshbus_contact_share_request(prefix0);
	zassert_equal(rc, -ENOENT, "deleted contact should return -ENOENT, rc=%d", rc);

	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config &= (uint8_t)~MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set (disable overwrite) failed: %d", rc);

	for (size_t i = 0U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		meshbus_contact contact_i;
		build_contact(&contact_i, (uint8_t)(0x80U + i), "contact_fill");
		rc = create_contact(&contact_i);
		zassert_ok(rc, "contact fill create failed at i=%u rc=%d", (unsigned int)i, rc);
	}

	build_contact(&extra, 0xf0, "contact_extra");
	rc = create_contact(&extra);
	zassert_equal(rc, -ENOSPC, "extra contact should fail when overwrite disabled: %d", rc);

	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config |= MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set (enable overwrite) failed: %d", rc);

	rc = create_contact(&extra);
	zassert_ok(rc, "extra contact create with overwrite failed: %d", rc);
	build_key_prefix(&extra, prefix_extra);

	rc = meshbus_contact_share_request(prefix_extra);
	zassert_equal(rc, -ENODATA,
		      "evicted slot reused contact should not inherit stale advert_raw, rc=%d", rc);

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_create_full_all_favorites_not_evicted)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact extra = meshbus_Contact_init_zero;
	int rc;

	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config |= MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	for (size_t i = 0U; i < CONFIG_MESHBUS_CONTACT_MAX_CONTACTS; i++) {
		build_contact(&contact, (uint8_t)(0xA0U + i), "contact_fav");
		rc = create_contact(&contact);
		zassert_ok(rc, "contact create failed at i=%u rc=%d", (unsigned int)i, rc);
		rc = update_contact_flags_by_key(contact.public_key.bytes, 0U,
					      MESHBUS_CONTACT_FLAG_FAVORITE);
		zassert_ok(rc, "contact favourite flag update failed at i=%u rc=%d",
			   (unsigned int)i, rc);
	}

	build_contact(&extra, 0xF1, "contact_extra");
	rc = create_contact(&extra);
	zassert_equal(rc, -ENOSPC, "all favorite contacts should not be evicted: rc=%d", rc);
}

ZTEST(meshbus_contact_contract, test_response_channels_update_contact_state)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_contact_response_advert_event advert_evt = {0};
	meshbus_contact_response_path_event path_evt = {0};
	meshbus_contact_response_trace_path_event trace_evt = {0};
	meshbus_contact_response_telemetry_event telemetry_evt = {0};
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	static const uint8_t telemetry_payload[] = {0x01, 0x02, 0x10, 0x20};
	uint32_t first_seen_after_advert = 0U;
	int rc;

	build_contact(&contact, 0x66, "contact_resp");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);
	build_key_prefix(&contact, prefix);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "initial get_by_key failed: %d", rc);
	zassert_equal(got.first_seen_timestamp, 0U, "newly created contact should start with zero first_seen");

	contact.role = MESHBUS_CONTACT_ROLE_SENSOR;
	strncpy(contact.name, "contact_resp_u", sizeof(contact.name) - 1U);
	contact.name[sizeof(contact.name) - 1U] = '\0';
	memcpy(advert_evt.public_key, contact.public_key.bytes, sizeof(advert_evt.public_key));
	memcpy(advert_evt.name, contact.name, sizeof(advert_evt.name));
	advert_evt.role = contact.role;

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert update publish failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "get_by_key failed: %d", rc);
	zassert_true(strcmp(got.name, "contact_resp_u") == 0, "advert update should update name");
	zassert_true(strcmp(got.alias, "contact_resp") == 0,
		     "advert update should not auto-update alias");
	zassert_equal(got.role, MESHBUS_CONTACT_ROLE_CHAT, "advert update should keep role");
	zassert_true(got.first_seen_timestamp != 0U,
		     "advert update should initialize first_seen");
	first_seen_after_advert = got.first_seen_timestamp;

	path_evt.is_discover = true;
	memcpy(path_evt.key_prefix, prefix, sizeof(prefix));
	path_evt.tag = 0x11223344U;
	path_evt.timestamp = 77U;
	path_evt.has_out_path = true;
	path_evt.out_path_len = 3U;
	path_evt.out_path[0] = 0x01;
	path_evt.out_path[1] = 0x02;
	path_evt.out_path[2] = 0x03;
	path_evt.has_response_snr = true;
	path_evt.response_snr = -8;
	path_evt.path_hash_size = 2U;
	rc = publish_contact_response(&meshbus_contact_path_response_chan, &path_evt);
	zassert_not_equal(rc, 0, "partial-hop contact_path response should fail");
	path_evt.path_hash_size = 0U;
	rc = publish_contact_response(&meshbus_contact_path_response_chan, &path_evt);
	zassert_ok(rc, "contact_path response publish failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "get_by_key failed after path response: %d", rc);
	zassert_equal(got.out_path.size, 3U, "path response out_path size mismatch");
	zassert_mem_equal(got.out_path.bytes, path_evt.out_path, path_evt.out_path_len,
			  "path response out_path mismatch");
	zassert_false(got.is_neighbor, "path response should set non-neighbor");
	zassert_equal(got.last_seen_timestamp, 77U, "path response timestamp mismatch");
	zassert_equal(got.last_seen_snr, -8, "path response snr mismatch");
	zassert_equal(got.first_seen_timestamp, first_seen_after_advert,
		      "path response should keep first_seen");
	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after discover response: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH,
		      "discover notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant,
		      MESHBUS_NOTIFY_TAG_NODE_DISCOVER_PATH, "discover notify tag mismatch");
	zassert_equal(
		notify_payload.payload_variant.node_discover_path.public_key_prefix.size,
		CONFIG_MESHBUS_CONTACT_PREFIX_BYTES,
		"discover notify prefix len mismatch");
	zassert_mem_equal(
		notify_payload.payload_variant.node_discover_path.public_key_prefix.bytes,
		prefix, sizeof(prefix), "discover notify prefix mismatch");
	zassert_equal(notify_payload.payload_variant.node_discover_path.tag,
		      path_evt.tag, "discover notify request tag mismatch");

	path_evt = (meshbus_contact_response_path_event){0};
	path_evt.is_discover = true;
	memcpy(path_evt.key_prefix, prefix, sizeof(prefix));
	path_evt.tag = 0x55667788U;
	path_evt.timestamp = 78U;
	path_evt.has_out_path = true;
	path_evt.out_path_len = 0U;
	path_evt.path_hash_size = 2U;
	path_evt.has_response_snr = true;
	path_evt.response_snr = -7;
	rc = publish_contact_response(&meshbus_contact_path_response_chan, &path_evt);
	zassert_ok(rc, "zero-hop contact_path response publish failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "get_by_key failed after zero-hop path response: %d", rc);
	zassert_equal(got.out_path.size, 0U, "zero-hop path should clear out_path bytes");
	zassert_true(got.is_neighbor, "zero-hop path response should set neighbor");
	zassert_equal(got.path_hash_size, 2U, "zero-hop path_hash_size mismatch");
	zassert_equal(got.last_seen_timestamp, 78U, "zero-hop timestamp mismatch");
	zassert_equal(got.last_seen_snr, -7, "zero-hop snr mismatch");
	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after zero-hop discover response: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_DISCOVER_PATH,
		      "zero-hop discover notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.payload_variant.node_discover_path.out_path.size, 0U,
		      "zero-hop discover notify out_path should be empty");
	zassert_equal(notify_payload.payload_variant.node_discover_path.tag,
		      path_evt.tag, "zero-hop discover notify request tag mismatch");

	memcpy(trace_evt.key_prefix, prefix, sizeof(prefix));
	trace_evt.timestamp = 88U;
	trace_evt.tag = 0x99aabbccU;
	trace_evt.state = 1U;
	trace_evt.has_response_snr = true;
	trace_evt.response_snr = -3;
	rc = publish_contact_response(&meshbus_contact_trace_path_response_chan, &trace_evt);
	zassert_ok(rc, "trace response publish failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "get_by_key failed after trace response: %d", rc);
	zassert_equal(got.last_seen_timestamp, 88U, "trace response timestamp mismatch");
	zassert_equal(got.last_seen_snr, -3, "trace response snr mismatch");
	zassert_equal(got.first_seen_timestamp, first_seen_after_advert,
		      "trace response should keep first_seen");
	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after trace response: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_TRACE_PATH,
		      "trace notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE_TRACE_PATH,
		      "trace notify tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_trace_path.public_key_prefix.size,
		      CONFIG_MESHBUS_CONTACT_PREFIX_BYTES,
		      "trace notify prefix len mismatch");
	zassert_mem_equal(
		notify_payload.payload_variant.node_trace_path.public_key_prefix.bytes,
		prefix, sizeof(prefix), "trace notify prefix mismatch");
	zassert_equal(notify_payload.payload_variant.node_trace_path.tag,
		      trace_evt.tag, "trace notify request tag mismatch");

	memcpy(telemetry_evt.key_prefix, prefix, sizeof(prefix));
	telemetry_evt.timestamp = 99U;
	telemetry_evt.tag = 0x11223344U;
	telemetry_evt.payload_len = sizeof(telemetry_payload);
	memcpy(telemetry_evt.payload, telemetry_payload, sizeof(telemetry_payload));
	rc = publish_contact_response(&meshbus_contact_telemetry_response_chan, &telemetry_evt);
	zassert_ok(rc, "telemetry response publish failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "get_by_key failed after telemetry response: %d", rc);
	zassert_equal(got.last_seen_timestamp, 99U, "telemetry response timestamp mismatch");
	zassert_equal(got.latitude, 0, "telemetry response should not update latitude in step 2");
	zassert_equal(got.longitude, 0,
		      "telemetry response should not update longitude in step 2");
	zassert_equal(got.first_seen_timestamp, first_seen_after_advert,
		      "telemetry response should keep first_seen");
	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after telemetry response: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY,
		      "telemetry notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE_TELEMETRY,
		      "telemetry notify tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_telemetry.tag, telemetry_evt.tag,
		      "telemetry notify request tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_telemetry.public_key_prefix.size,
		      CONFIG_MESHBUS_CONTACT_PREFIX_BYTES,
		      "telemetry notify prefix len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_telemetry.public_key_prefix.bytes,
			  prefix, sizeof(prefix), "telemetry notify prefix mismatch");
	zassert_equal(notify_payload.payload_variant.node_telemetry.payload.size,
		      sizeof(telemetry_payload), "telemetry notify payload len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_telemetry.payload.bytes,
			  telemetry_payload, sizeof(telemetry_payload),
			  "telemetry notify payload mismatch");
}

ZTEST(meshbus_contact_contract, test_advert_notify_publishes_nodes_changed_and_node_advert)
{
	meshbus_contact_response_advert_event advert_evt = {0};
	meshbus_contact_response_advert_event node_advert_evt = {0};
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	int rc;

	fill_bytes(advert_evt.public_key, sizeof(advert_evt.public_key), 0x91);
	memcpy(prefix, advert_evt.public_key, sizeof(prefix));
	strncpy(advert_evt.name, "notify_contact", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_CHAT;
	advert_evt.is_new = true;
	advert_evt.advert_timestamp = 1775000100U;
	advert_evt.has_response_snr = true;
	advert_evt.response_snr = -4;
	advert_evt.has_position = true;
	advert_evt.latitude = 1234567;
	advert_evt.longitude = -7654321;
	advert_evt.has_out_path = true;
	advert_evt.path_hash_size = 1U;
	drain_contact_notify_listener();

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert create publish failed: %d", rc);
	rc = wait_contact_notify_counts(1U, 1U);
	zassert_ok(rc, "notify listener wait failed after create advert: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_advert_chan, &node_advert_evt, K_NO_WAIT);
	zassert_ok(rc, "create advert event read failed: %d", rc);
	zassert_false(node_advert_evt.is_new,
		      "auto-added advert should match Arduino is_new=false");
	notify_evt = last_contacts_changed_notify;
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODES_CHANGED,
		      "create advert should publish NODES_CHANGED");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE,
		      "create advert notify tag mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node.public_key_prefix.bytes, prefix,
			  sizeof(prefix), "create advert prefix mismatch");
	notify_evt = last_contact_advert_notify;
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_ADVERT,
		      "create advert should publish NODE_ADVERT");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.which_payload_variant, MESHBUS_NOTIFY_TAG_NODE_ADVERT,
		      "create advert node_advert tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.public_key.size,
		      sizeof(advert_evt.public_key), "create advert public_key size mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_advert.public_key.bytes,
			  advert_evt.public_key, sizeof(advert_evt.public_key),
			  "create advert public_key mismatch");
	zassert_true(strcmp(notify_payload.payload_variant.node_advert.name, "notify_contact") ==
			     0,
		     "create advert name mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.role,
		      MESHBUS_CONTACT_ROLE_CHAT, "create advert role mismatch");
	zassert_false(notify_payload.payload_variant.node_advert.is_new,
		      "create advert is_new mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.advert_timestamp,
		      1775000100U, "create advert timestamp mismatch");
	zassert_true(notify_payload.payload_variant.node_advert.has_out_path,
		     "create advert has_out_path mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.path_hash_size, 1U,
		      "create advert path_hash_size mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.response_snr, -4,
		      "create advert response_snr mismatch");
	zassert_true(notify_payload.payload_variant.node_advert.has_position,
		     "create advert has_position mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.latitude, 1234567,
		      "create advert latitude mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.longitude, -7654321,
		      "create advert longitude mismatch");

	memset(&advert_evt, 0, sizeof(advert_evt));
	fill_bytes(advert_evt.public_key, sizeof(advert_evt.public_key), 0x91);
	strncpy(advert_evt.name, "notify_contact_u", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_CHAT;
	advert_evt.advert_timestamp = 1775000200U;
	advert_evt.has_response_snr = true;
	advert_evt.response_snr = -7;
	drain_contact_notify_listener();

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "advert update publish failed: %d", rc);
	rc = wait_contact_notify_counts(1U, 1U);
	zassert_ok(rc, "notify listener wait failed after update advert: %d", rc);
	notify_evt = last_contacts_changed_notify;
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODES_CHANGED,
		      "update advert should publish NODES_CHANGED");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_mem_equal(notify_payload.payload_variant.node.public_key_prefix.bytes, prefix,
			  sizeof(prefix), "update advert prefix mismatch");
	notify_evt = last_contact_advert_notify;
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_ADVERT,
		      "update advert should publish NODE_ADVERT");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.payload_variant.node_advert.public_key.size,
		      sizeof(advert_evt.public_key), "update advert public_key size mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_advert.public_key.bytes,
			  advert_evt.public_key, sizeof(advert_evt.public_key),
			  "update advert public_key mismatch");
	zassert_true(strcmp(notify_payload.payload_variant.node_advert.name,
			    "notify_contact_u") == 0,
		     "update advert name mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.role,
		      MESHBUS_CONTACT_ROLE_CHAT, "update advert role mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.advert_timestamp,
		      1775000200U, "update advert timestamp mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.response_snr, -7,
		      "update advert response_snr mismatch");
	zassert_false(notify_payload.payload_variant.node_advert.has_position,
		      "update advert has_position mismatch");
}

ZTEST(meshbus_contact_contract, test_new_advert_notify_publishes_without_auto_store)
{
	meshbus_contact_response_advert_event advert_evt = {0};
	meshbus_contact_response_advert_event node_advert_evt = {0};
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	int rc;

	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config = MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT |
			      MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	fill_bytes(advert_evt.public_key, sizeof(advert_evt.public_key), 0xa7);
	memcpy(prefix, advert_evt.public_key, sizeof(prefix));
	strncpy(advert_evt.name, "manual_sensor", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_SENSOR;
	advert_evt.is_new = true;
	advert_evt.advert_timestamp = 1775000300U;
	advert_evt.has_position = true;
	advert_evt.latitude = 42;
	advert_evt.longitude = -43;
	advert_evt.has_out_path = true;
	advert_evt.path_hash_size = 1U;
	drain_contact_notify_listener();

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "manual advert publish failed: %d", rc);
	rc = wait_contact_notify_counts(0U, 1U);
	zassert_ok(rc, "notify listener wait failed after manual advert: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_advert_chan, &node_advert_evt, K_NO_WAIT);
	zassert_ok(rc, "manual advert event read failed: %d", rc);
	zassert_true(node_advert_evt.is_new,
		     "unstored advert should match Arduino is_new=true");
	zassert_equal(contacts_changed_notify_count, 0U,
		      "manual-only advert should not publish NODES_CHANGED");
	rc = meshbus_contact_find_by_key(advert_evt.public_key, &got);
	zassert_equal(rc, -ENOENT, "manual-only advert should not auto-store contact: %d", rc);

	notify_evt = last_contact_advert_notify;
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_ADVERT,
		      "manual advert should publish NODE_ADVERT");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.payload_variant.node_advert.public_key.size,
		      sizeof(advert_evt.public_key), "manual advert public_key size mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_advert.public_key.bytes,
			  advert_evt.public_key, sizeof(advert_evt.public_key),
			  "manual advert public_key mismatch");
	zassert_true(strcmp(notify_payload.payload_variant.node_advert.name,
			    "manual_sensor") == 0,
		     "manual advert name mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.role,
		      MESHBUS_CONTACT_ROLE_SENSOR, "manual advert role mismatch");
	zassert_true(notify_payload.payload_variant.node_advert.is_new,
		     "manual advert is_new mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.advert_timestamp,
		      1775000300U, "manual advert timestamp mismatch");
	zassert_true(notify_payload.payload_variant.node_advert.has_out_path,
		     "manual advert has_out_path mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.path_hash_size, 1U,
		      "manual advert path_hash_size mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.response_snr, 0,
		      "manual advert response_snr mismatch");
	zassert_true(notify_payload.payload_variant.node_advert.has_position,
		     "manual advert position mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.latitude, 42,
		      "manual advert latitude mismatch");
	zassert_equal(notify_payload.payload_variant.node_advert.longitude, -43,
		      "manual advert longitude mismatch");
}

ZTEST(meshbus_contact_contract, test_manual_overwrite_policy_does_not_auto_store_adverts)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	meshbus_contact_response_advert_event advert_evt = {0};
	meshbus_contact got = meshbus_Contact_init_zero;
	int rc;

	rc = delete_all_contacts();
	zassert_ok(rc, "delete_all_contacts failed: %d", rc);
	rc = meshbus_meshcore_config_get(&cfg);
	zassert_ok(rc, "config_get failed: %d", rc);
	cfg.add_contact_config = MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE |
				 MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST;
	rc = meshbus_meshcore_config_set(&cfg);
	zassert_ok(rc, "config_set failed: %d", rc);

	fill_bytes(advert_evt.public_key, sizeof(advert_evt.public_key), 0xa8);
	strncpy(advert_evt.name, "manual_chat", sizeof(advert_evt.name) - 1U);
	advert_evt.role = MESHBUS_CONTACT_ROLE_CHAT;
	advert_evt.is_new = true;
	advert_evt.advert_timestamp = 1775000400U;

	rc = publish_contact_response(&meshbus_contact_advert_response_chan, &advert_evt);
	zassert_ok(rc, "manual overwrite advert publish failed: %d", rc);
	wait_for_async_contact_response();
	rc = meshbus_contact_find_by_key(advert_evt.public_key, &got);
	zassert_equal(rc, -ENOENT,
		      "manual overwrite policy should not auto-store adverts: %d", rc);
}

ZTEST(meshbus_contact_contract, test_contact_update_preserves_path_hash_and_updates_flags)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	static const uint8_t out_path[] = {0x21, 0x22, 0x23};
	int rc;

	build_contact(&contact, 0x67, "contact_update_flags");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get before path-hash setup failed: %d", rc);
	got.path_hash_size = 3U;
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "contact preset path_hash_size failed: %d", rc);

	rc = update_contact_path_by_key(contact.public_key.bytes, out_path, sizeof(out_path));
	zassert_ok(rc, "contact path update failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after path update failed: %d", rc);
	zassert_equal(got.path_hash_size, 3U, "path_hash_size should stay unchanged");
	zassert_equal(got.out_path.size, sizeof(out_path), "out_path size mismatch");

	got.path_hash_size = 0U;
	rc = meshbus_contact_set(got.public_key.bytes, &got);
	zassert_ok(rc, "contact force path_hash_size=0 failed: %d", rc);
	rc = update_contact_path_by_key(contact.public_key.bytes, NULL, 0U);
	zassert_ok(rc, "contact empty path update failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after empty path update failed: %d", rc);
	zassert_equal(got.path_hash_size, 1U, "path_hash_size should fall back to 1");
	zassert_true(got.is_neighbor, "empty path should mark contact as neighbor");

	rc = update_contact_flags_by_key(contact.public_key.bytes,
					 MESHBUS_CONTACT_FLAG_FAVORITE |
					 MESHBUS_CONTACT_FLAG_TELEMETRY_BASE |
					 MESHBUS_CONTACT_FLAG_TELEMETRY_LOCATION |
					 MESHBUS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT,
					 MESHBUS_CONTACT_FLAG_FAVORITE |
					 MESHBUS_CONTACT_FLAG_TELEMETRY_BASE |
					 MESHBUS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT);
	zassert_ok(rc, "contact flags update failed: %d", rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get after flags update failed: %d", rc);
	zassert_true((got.flags & MESHBUS_CONTACT_FLAG_FAVORITE) != 0U,
		     "favourite flag should be set");
	zassert_true((got.flags & MESHBUS_CONTACT_FLAG_TELEMETRY_BASE) != 0U,
		     "base telemetry flag should be set");
	zassert_false((got.flags & MESHBUS_CONTACT_FLAG_TELEMETRY_LOCATION) != 0U,
		      "location telemetry flag should be cleared");
	zassert_true((got.flags & MESHBUS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT) != 0U,
		     "environment telemetry flag should be set");
}

ZTEST(meshbus_contact_contract, test_telemetry_notify_carries_raw_payload)
{
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_contact_response_telemetry_event telemetry_evt = {0};
	meshbus_notify_event notify_evt = MESHBUS_NOTIFY_EVENT_INIT_ZERO;
	meshbus_notify notify_payload = meshbus_Notify_init_zero;
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	static const uint8_t payload[] = {0xaa, 0xbb, 0xcc};
	int rc;

	build_contact(&contact, 0x6c, "contact_telem");
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);
	build_key_prefix(&contact, prefix);

	memcpy(telemetry_evt.key_prefix, prefix, sizeof(prefix));
	telemetry_evt.timestamp = 100U;
	telemetry_evt.tag = 0x01020304U;
	telemetry_evt.payload_len = sizeof(payload);
	memcpy(telemetry_evt.payload, payload, sizeof(payload));
	rc = publish_contact_response(&meshbus_contact_telemetry_response_chan, &telemetry_evt);
	zassert_ok(rc, "telemetry response publish failed: %d", rc);

	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after telemetry publish: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY,
		      "telemetry notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.payload_variant.node_telemetry.tag, telemetry_evt.tag,
		      "telemetry notify request tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_telemetry.payload.size,
		      sizeof(payload), "telemetry notify payload len mismatch");
	zassert_mem_equal(notify_payload.payload_variant.node_telemetry.payload.bytes,
			  payload, sizeof(payload), "telemetry notify payload mismatch");

	memset(&telemetry_evt, 0, sizeof(telemetry_evt));
	memcpy(telemetry_evt.key_prefix, prefix, sizeof(prefix));
	telemetry_evt.timestamp = 101U;
	telemetry_evt.tag = 0x05060708U;
	rc = publish_contact_response(&meshbus_contact_telemetry_response_chan, &telemetry_evt);
	zassert_ok(rc, "empty telemetry response publish failed: %d", rc);

	rc = read_notify_event(&notify_evt);
	zassert_ok(rc, "notify read failed after empty telemetry publish: %d", rc);
	zassert_equal(notify_evt.type, MESHBUS_NOTIFY_TYPE_NODE_TELEMETRY,
		      "empty telemetry notify type mismatch");
	notify_payload = decode_notify_event(&notify_evt);
	zassert_equal(notify_payload.payload_variant.node_telemetry.tag, telemetry_evt.tag,
		      "empty telemetry notify request tag mismatch");
	zassert_equal(notify_payload.payload_variant.node_telemetry.payload.size, 0U,
		      "empty telemetry notify payload should be empty");

	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "contact get failed after empty telemetry response: %d", rc);
	zassert_equal(got.last_seen_timestamp, 101U,
		      "contact last_seen should still track latest telemetry response");
	zassert_equal(got.latitude, 0, "step 2 should not parse telemetry latitude yet");
	zassert_equal(got.longitude, 0, "step 2 should not parse telemetry longitude yet");
}

ZTEST(meshbus_contact_contract, test_unknown_contact_telemetry_response_does_not_create_contact)
{
	meshbus_contact_response_telemetry_event telemetry_evt = {0};
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	int rc;

	memset(prefix, 0xee, sizeof(prefix));
	memcpy(telemetry_evt.key_prefix, prefix, sizeof(prefix));
	telemetry_evt.timestamp = 55U;
	telemetry_evt.payload_len = 2U;
	telemetry_evt.payload[0] = 0x12;
	telemetry_evt.payload[1] = 0x34;

	rc = publish_contact_response(&meshbus_contact_telemetry_response_chan, &telemetry_evt);
	zassert_ok(rc, "unknown telemetry response publish failed: %d", rc);
	zassert_equal(meshbus_contact_store_count(), 0U,
		      "unknown telemetry response should not create contact");
}

ZTEST(meshbus_contact_contract, test_request_publish_and_inflight_release)
{
	meshbus_contact contact;
	meshbus_meshcore_advert_request_event advert_evt = {0};
	meshbus_meshcore_node_discover_request_event node_discover_req = {0};
	meshbus_meshcore_trace_request_event node_trace_req = {0};
	meshbus_contact_discover_path_request_event discover_req = {0};
	meshbus_contact_trace_path_request_event trace_req = {0};
	meshbus_contact_telemetry_request_event telemetry_req = {0};
	meshbus_contact_binary_request_event binary_req = {0};
	meshbus_contact_response_path_event discover_resp = {0};
	meshbus_contact_response_trace_path_event trace_resp = {0};
	meshbus_contact_response_telemetry_event telemetry_resp = {0};
	meshbus_contact_response_binary_event binary_resp = {0};
	uint8_t prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	uint8_t unknown_prefix[CONFIG_MESHBUS_CONTACT_PREFIX_BYTES];
	const uint8_t node_trace_path[] = {0x51U, 0x52U, 0x53U};
	const uint8_t binary_payload[] = {0x91U, 0x92U, 0x93U};
	uint32_t telemetry_tag = 0U;
	uint32_t binary_tag = 0U;
	uint32_t node_discover_tag = 0U;
	uint32_t node_trace_tag = 0U;
	uint32_t discover_tag = 0U;
	uint32_t trace_tag = 0U;
	int rc;

	build_contact(&contact, 0x40, "contact_req");
	contact.out_path.size = 1U;
	contact.out_path.bytes[0] = 0x7AU;
	contact.path_hash_size = 1U;
	contact.is_neighbor = false;
	rc = create_contact(&contact);
	zassert_ok(rc, "contact create failed: %d", rc);
	build_key_prefix(&contact, prefix);

	rc = meshbus_meshcore_advert_request(true);
	zassert_equal(rc, -ENODEV,
		      "runtime-disabled advert should be unavailable: %d", rc);
	rc = zbus_chan_read(&meshbus_meshcore_advert_request_chan, &advert_evt, K_NO_WAIT);
	zassert_ok(rc, "advert request read failed: %d", rc);
	zassert_true(advert_evt.flood, "advert flood flag mismatch");
	rc = meshbus_meshcore_advert_request(false);
	zassert_equal(rc, -ENODEV,
		      "runtime-disabled advert(false) should be unavailable: %d", rc);
	rc = zbus_chan_read(&meshbus_meshcore_advert_request_chan, &advert_evt, K_NO_WAIT);
	zassert_ok(rc, "advert request(false) read failed: %d", rc);
	zassert_false(advert_evt.flood, "advert flood=false mismatch");

	rc = meshbus_meshcore_node_discover_request(MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT |
					   MESHBUS_MESHCORE_DISCOVER_FILTER_SENSOR,
					   1775000500U, &node_discover_tag);
	zassert_equal(rc, -ENODEV,
		      "runtime-disabled node-discover should be unavailable: %d", rc);
	zassert_equal(node_discover_tag, 0U,
		      "rejected node-discover must not return a tag");
	rc = zbus_chan_read(&meshbus_meshcore_node_discover_request_chan, &node_discover_req,
			    K_NO_WAIT);
	zassert_ok(rc, "node_discover request read failed: %d", rc);
	zassert_equal(node_discover_req.filter,
		      MESHBUS_MESHCORE_DISCOVER_FILTER_CHAT | MESHBUS_MESHCORE_DISCOVER_FILTER_SENSOR,
		      "node_discover filter mismatch");
	zassert_equal(node_discover_req.since, 1775000500U, "node_discover since mismatch");
	zassert_not_equal(node_discover_req.tag, 0U,
			  "published node-discover tag should be nonzero");

	rc = meshbus_meshcore_trace_request(node_trace_path, sizeof(node_trace_path),
					    1U, &node_trace_tag);
	zassert_equal(rc, -ENODEV,
		      "runtime-disabled trace should be unavailable: %d", rc);
	zassert_equal(node_trace_tag, 0U,
		      "rejected trace must not return a tag");
	rc = zbus_chan_read(&meshbus_meshcore_trace_request_chan, &node_trace_req, K_NO_WAIT);
	zassert_ok(rc, "meshcore trace request read failed: %d", rc);
	zassert_equal(node_trace_req.path_hash_size, 1U, "meshcore trace hash size mismatch");
	zassert_equal(node_trace_req.path_len, sizeof(node_trace_path),
		      "meshcore trace path len mismatch");
	zassert_mem_equal(node_trace_req.path, node_trace_path, sizeof(node_trace_path),
			  "meshcore trace path mismatch");
	zassert_not_equal(node_trace_req.tag, 0U,
			  "published trace tag should be nonzero");

	rc = meshbus_contact_discover_path_request(prefix, &discover_tag);
	zassert_ok(rc, "discover request failed: %d", rc);
	zassert_not_equal(discover_tag, 0U, "discover out tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_discover_path_request_chan, &discover_req, K_NO_WAIT);
	zassert_ok(rc, "discover request read failed: %d", rc);
	zassert_mem_equal(discover_req.key_prefix, prefix, sizeof(prefix), "discover prefix mismatch");
	zassert_equal(discover_req.tag, discover_tag, "discover tag mismatch");
	rc = meshbus_contact_discover_path_request(prefix, NULL);
	zassert_equal(rc, -EBUSY, "second discover should be busy, rc=%d", rc);

	discover_resp.is_discover = true;
	memcpy(discover_resp.key_prefix, prefix, sizeof(prefix));
	discover_resp.tag = discover_tag;
	discover_resp.timestamp = 42U;
	discover_resp.has_out_path = true;
	discover_resp.out_path_len = 1U;
	discover_resp.out_path[0] = 0x7AU;
	rc = publish_contact_response(&meshbus_contact_path_response_chan, &discover_resp);
	zassert_ok(rc, "discover response publish failed: %d", rc);
	rc = meshbus_contact_discover_path_request(prefix, &discover_tag);
	zassert_ok(rc, "discover request should recover after response, rc=%d", rc);
	zassert_not_equal(discover_tag, 0U, "discover recovery out tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_discover_path_request_chan, &discover_req, K_NO_WAIT);
	zassert_ok(rc, "discover request(false) read failed: %d", rc);
	zassert_equal(discover_req.tag, discover_tag, "discover recovery tag mismatch");

	rc = meshbus_contact_trace_path_request(prefix, &trace_tag);
	zassert_ok(rc, "trace request failed: %d", rc);
	zassert_not_equal(trace_tag, 0U, "trace out tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_trace_path_request_chan, &trace_req, K_NO_WAIT);
	zassert_ok(rc, "trace request read failed: %d", rc);
	zassert_mem_equal(trace_req.key_prefix, prefix, sizeof(prefix), "trace prefix mismatch");
	zassert_equal(trace_req.tag, trace_tag, "trace tag mismatch");
	rc = meshbus_contact_trace_path_request(prefix, NULL);
	zassert_equal(rc, -EBUSY, "second trace should be busy, rc=%d", rc);

	trace_resp.state = 1U;
	memcpy(trace_resp.key_prefix, prefix, sizeof(prefix));
	trace_resp.tag = trace_tag;
	trace_resp.timestamp = 43U;
	rc = publish_contact_response(&meshbus_contact_trace_path_response_chan, &trace_resp);
	zassert_ok(rc, "trace response publish failed: %d", rc);
	rc = meshbus_contact_trace_path_request(prefix, &trace_tag);
	zassert_ok(rc, "trace request should recover after response, rc=%d", rc);
	zassert_not_equal(trace_tag, 0U, "trace recovery out tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_trace_path_request_chan, &trace_req, K_NO_WAIT);
	zassert_ok(rc, "trace request recovery read failed: %d", rc);
	zassert_mem_equal(trace_req.key_prefix, prefix, sizeof(prefix),
			  "trace recovery prefix mismatch");
	zassert_equal(trace_req.tag, trace_tag, "trace recovery tag mismatch");
	memcpy(trace_resp.key_prefix, prefix, sizeof(prefix));
	trace_resp.tag = trace_tag;
	trace_resp.timestamp = 44U;
	rc = publish_contact_response(&meshbus_contact_trace_path_response_chan, &trace_resp);
	zassert_ok(rc, "trace recovery response publish failed: %d", rc);

	rc = meshbus_contact_telemetry_request(prefix, NULL);
	zassert_ok(rc, "telemetry request failed: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_telemetry_request_chan, &telemetry_req, K_NO_WAIT);
	zassert_ok(rc, "telemetry request read failed: %d", rc);
	zassert_mem_equal(telemetry_req.key_prefix, prefix, sizeof(prefix),
			  "telemetry prefix mismatch");
	zassert_not_equal(telemetry_req.tag, 0U, "telemetry event tag should be nonzero");
	rc = meshbus_contact_telemetry_request(prefix, NULL);
	zassert_equal(rc, -EBUSY, "second telemetry should be busy, rc=%d", rc);

	memcpy(telemetry_resp.key_prefix, prefix, sizeof(prefix));
	telemetry_resp.tag = telemetry_req.tag;
	rc = publish_contact_response(&meshbus_contact_telemetry_response_chan, &telemetry_resp);
	zassert_ok(rc, "telemetry response publish failed: %d", rc);
	rc = meshbus_contact_telemetry_request(prefix, &telemetry_tag);
	zassert_ok(rc, "telemetry request should recover after response, rc=%d", rc);
	zassert_not_equal(telemetry_tag, 0U, "telemetry out tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_telemetry_request_chan, &telemetry_req, K_NO_WAIT);
	zassert_ok(rc, "telemetry request recovery read failed: %d", rc);
	zassert_mem_equal(telemetry_req.key_prefix, prefix, sizeof(prefix),
			  "telemetry recovery prefix mismatch");
	zassert_equal(telemetry_req.tag, telemetry_tag, "telemetry recovery tag mismatch");

	memset(unknown_prefix, 0xee, sizeof(unknown_prefix));
	rc = meshbus_contact_discover_path_request(unknown_prefix, NULL);
	zassert_equal(rc, -ENOENT, "unknown discover prefix should fail with -ENOENT, rc=%d", rc);
	rc = meshbus_contact_trace_path_request(unknown_prefix, NULL);
	zassert_equal(rc, -ENOENT, "unknown trace prefix should fail with -ENOENT, rc=%d", rc);
	rc = meshbus_contact_telemetry_request(unknown_prefix, NULL);
	zassert_equal(rc, -ENOENT, "unknown telemetry prefix should fail with -ENOENT, rc=%d", rc);

	rc = meshbus_contact_binary_request(prefix, binary_payload, sizeof(binary_payload),
					 &binary_tag);
	zassert_ok(rc, "binary request failed: %d", rc);
	zassert_not_equal(binary_tag, 0U, "binary tag should be nonzero");
	rc = zbus_chan_read(&meshbus_contact_binary_request_chan, &binary_req, K_NO_WAIT);
	zassert_ok(rc, "binary request read failed: %d", rc);
	zassert_mem_equal(binary_req.key_prefix, prefix, sizeof(prefix), "binary prefix mismatch");
	zassert_equal(binary_req.tag, binary_tag, "binary tag mismatch");
	zassert_equal(binary_req.payload_len, sizeof(binary_payload), "binary payload len");
	zassert_mem_equal(binary_req.payload, binary_payload, sizeof(binary_payload),
			  "binary payload mismatch");
	rc = meshbus_contact_binary_request(prefix, binary_payload, sizeof(binary_payload), NULL);
	zassert_equal(rc, -EBUSY, "second binary request should be busy, rc=%d", rc);

	memcpy(binary_resp.key_prefix, prefix, sizeof(prefix));
	binary_resp.timestamp = 44U;
	binary_resp.tag = binary_tag;
	rc = publish_contact_response(&meshbus_contact_binary_response_chan, &binary_resp);
	zassert_ok(rc, "binary response publish failed: %d", rc);
	rc = meshbus_contact_binary_request(prefix, binary_payload, sizeof(binary_payload),
					 &binary_tag);
	zassert_ok(rc, "binary request should recover after response, rc=%d", rc);
	rc = zbus_chan_read(&meshbus_contact_binary_request_chan, &binary_req, K_NO_WAIT);
	zassert_ok(rc, "binary request recovery read failed: %d", rc);
	zassert_mem_equal(binary_req.key_prefix, prefix, sizeof(prefix),
			  "binary recovery prefix mismatch");

	rc = meshbus_contact_binary_request(unknown_prefix, binary_payload, sizeof(binary_payload),
					 NULL);
	zassert_equal(rc, -ENOENT, "unknown binary prefix should fail with -ENOENT, rc=%d", rc);

	meshbus_contact neighbor = meshbus_Contact_init_zero;
	rc = meshbus_contact_find_by_prefix(prefix, &neighbor);
	zassert_ok(rc, "contact get by prefix failed: %d", rc);
	neighbor.out_path.size = 0U;
	neighbor.path_hash_size = 1U;
	neighbor.is_neighbor = false;
	rc = meshbus_contact_set(neighbor.public_key.bytes, &neighbor);
	zassert_ok(rc, "unknown-path contact update failed: %d", rc);
	rc = meshbus_contact_trace_path_request(prefix, NULL);
	zassert_equal(rc, -EINVAL, "trace request with unknown empty out_path should fail, rc=%d",
		      rc);

	neighbor.is_neighbor = true;
	rc = meshbus_contact_set(neighbor.public_key.bytes, &neighbor);
	zassert_ok(rc, "direct-neighbor contact update failed: %d", rc);
	rc = meshbus_contact_trace_path_request(prefix, &trace_tag);
	zassert_ok(rc, "trace request with zero-hop direct path failed: %d", rc);
	rc = zbus_chan_read(&meshbus_contact_trace_path_request_chan, &trace_req, K_NO_WAIT);
	zassert_ok(rc, "direct-neighbor trace request read failed: %d", rc);
	zassert_mem_equal(trace_req.key_prefix, prefix, sizeof(prefix),
			  "direct-neighbor trace prefix mismatch");
	zassert_equal(trace_req.tag, trace_tag, "direct-neighbor trace tag mismatch");
}

ZTEST(meshbus_contact_contract, test_concurrent_inserts_wait_for_one_store_transaction)
{
	struct contact_writer_task first;
	struct contact_writer_task second;
	meshbus_contact got = meshbus_Contact_init_zero;
	bool second_finished_early;
	atomic_val_t calls_while_blocked;
	int gate_rc;
	int rc;

	contact_writer_task_init(&first, CONTACT_WRITER_SET);
	contact_writer_task_init(&second, CONTACT_WRITER_SET);
	build_contact(&first.contact, 0xa0, "concurrent_insert_a");
	build_contact(&second.contact, 0xc0, "concurrent_insert_b");

	contact_save_gate_arm();
	contact_writer_start(&contact_writer_thread_a, contact_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_a), &first);
	gate_rc = k_sem_take(&contact_save_gate_entered, K_SECONDS(1));
	if (gate_rc == 0) {
		contact_writer_start(&contact_writer_thread_b, contact_writer_stack_b,
				     K_THREAD_STACK_SIZEOF(contact_writer_stack_b), &second);
		k_sleep(K_MSEC(20));
		second_finished_early = k_sem_take(&second.done, K_NO_WAIT) == 0;
		calls_while_blocked = atomic_get(&contact_blob_save_call_count);
		k_sem_give(&contact_save_gate_release);
		contact_writer_finish(&contact_writer_thread_b, &second, second_finished_early);
	} else {
		second_finished_early = false;
		calls_while_blocked = atomic_get(&contact_blob_save_call_count);
		k_sem_give(&contact_save_gate_release);
	}
	contact_writer_finish(&contact_writer_thread_a, &first, false);
	contact_save_gate_disarm();

	zassert_ok(gate_rc, "first insert did not reach contact persistence");
	zassert_false(second_finished_early, "second insert bypassed the active store transaction");
	zassert_equal(calls_while_blocked, 1,
		      "more than one contact blob write entered the transaction: %ld",
		      (long)calls_while_blocked);
	zassert_ok(first.rc, "first concurrent insert failed: %d", first.rc);
	zassert_ok(second.rc, "second concurrent insert failed: %d", second.rc);
	zassert_equal(meshbus_contact_store_count(), 2U, "concurrent inserts lost a contact");
	rc = meshbus_contact_find_by_key(first.contact.public_key.bytes, &got);
	zassert_ok(rc, "first concurrent contact missing: %d", rc);
	rc = meshbus_contact_find_by_key(second.contact.public_key.bytes, &got);
	zassert_ok(rc, "second concurrent contact missing: %d", rc);
}

ZTEST(meshbus_contact_contract, test_concurrent_response_updates_preserve_both_changes)
{
	struct contact_writer_task path_writer;
	struct contact_writer_task telemetry_writer;
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	meshbus_contact_response_path_event path_event = {0};
	meshbus_contact_response_telemetry_event telemetry_event = {0};
	bool telemetry_finished_early;
	atomic_val_t calls_while_blocked;
	int gate_rc;
	int rc;

	build_contact(&contact, 0xa1, "concurrent_updates");
	zassert_ok(create_contact(&contact), "contact create failed");

	memcpy(path_event.key_prefix, contact.public_key.bytes, sizeof(path_event.key_prefix));
	path_event.timestamp = 100U;
	path_event.has_out_path = true;
	path_event.out_path_len = 2U;
	path_event.out_path[0] = 0x41U;
	path_event.out_path[1] = 0x42U;
	path_event.path_hash_size = 1U;
	memcpy(telemetry_event.key_prefix, contact.public_key.bytes,
	       sizeof(telemetry_event.key_prefix));
	telemetry_event.timestamp = 200U;

	contact_writer_task_init(&path_writer, CONTACT_WRITER_PUBLISH);
	path_writer.chan = &meshbus_contact_path_response_chan;
	path_writer.event = &path_event;
	contact_writer_task_init(&telemetry_writer, CONTACT_WRITER_PUBLISH);
	telemetry_writer.chan = &meshbus_contact_telemetry_response_chan;
	telemetry_writer.event = &telemetry_event;

	contact_save_gate_arm();
	contact_writer_start(&contact_writer_thread_a, contact_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_a), &path_writer);
	gate_rc = k_sem_take(&contact_save_gate_entered, K_SECONDS(1));
	if (gate_rc == 0) {
		contact_writer_start(&contact_writer_thread_b, contact_writer_stack_b,
				     K_THREAD_STACK_SIZEOF(contact_writer_stack_b), &telemetry_writer);
		k_sleep(K_MSEC(20));
		telemetry_finished_early = k_sem_take(&telemetry_writer.done, K_NO_WAIT) == 0;
		calls_while_blocked = atomic_get(&contact_blob_save_call_count);
		k_sem_give(&contact_save_gate_release);
		contact_writer_finish(&contact_writer_thread_b, &telemetry_writer,
				      telemetry_finished_early);
	} else {
		telemetry_finished_early = false;
		calls_while_blocked = atomic_get(&contact_blob_save_call_count);
		k_sem_give(&contact_save_gate_release);
	}
	contact_writer_finish(&contact_writer_thread_a, &path_writer, false);
	contact_save_gate_disarm();

	zassert_ok(gate_rc, "path update did not reach contact persistence");
	zassert_false(telemetry_finished_early,
		      "telemetry update bypassed the active store transaction");
	zassert_equal(calls_while_blocked, 1,
		      "more than one response write entered the transaction: %ld",
		      (long)calls_while_blocked);
	zassert_ok(path_writer.rc, "path response publish failed: %d", path_writer.rc);
	zassert_ok(telemetry_writer.rc, "telemetry response publish failed: %d",
		   telemetry_writer.rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_ok(rc, "updated contact missing: %d", rc);
	zassert_equal(got.out_path.size, path_event.out_path_len, "path update was lost");
	zassert_mem_equal(got.out_path.bytes, path_event.out_path, path_event.out_path_len,
			  "path bytes were lost");
	zassert_equal(got.last_seen_timestamp, telemetry_event.timestamp,
		      "later telemetry timestamp was lost");
}

ZTEST(meshbus_contact_contract, test_concurrent_update_then_reset_does_not_resurrect)
{
	struct contact_writer_task update_writer;
	struct contact_writer_task reset_writer;
	meshbus_contact contact = meshbus_Contact_init_zero;
	meshbus_contact got = meshbus_Contact_init_zero;
	bool reset_finished_early;
	size_t slot = 0U;
	int gate_rc;
	int rc;

	build_contact(&contact, 0xa2, "concurrent_reset");
	zassert_ok(create_contact(&contact), "contact create failed");
	zassert_ok(find_contact_index_by_prefix(contact.public_key.bytes, &slot),
		   "contact slot lookup failed");
	zassert_ok(meshbus_contact_find_by_key(contact.public_key.bytes, &got),
		   "contact load failed");
	strncpy(got.alias, "update_before_reset", sizeof(got.alias) - 1U);

	contact_writer_task_init(&update_writer, CONTACT_WRITER_SET);
	update_writer.contact = got;
	contact_writer_task_init(&reset_writer, CONTACT_WRITER_RESET);
	memcpy(reset_writer.prefix, contact.public_key.bytes, sizeof(reset_writer.prefix));

	contact_save_gate_arm();
	contact_writer_start(&contact_writer_thread_a, contact_writer_stack_a,
			     K_THREAD_STACK_SIZEOF(contact_writer_stack_a), &update_writer);
	gate_rc = k_sem_take(&contact_save_gate_entered, K_SECONDS(1));
	if (gate_rc == 0) {
		contact_writer_start(&contact_writer_thread_b, contact_writer_stack_b,
				     K_THREAD_STACK_SIZEOF(contact_writer_stack_b), &reset_writer);
		k_sleep(K_MSEC(20));
		reset_finished_early = k_sem_take(&reset_writer.done, K_NO_WAIT) == 0;
		k_sem_give(&contact_save_gate_release);
		contact_writer_finish(&contact_writer_thread_b, &reset_writer,
				      reset_finished_early);
	} else {
		reset_finished_early = false;
		k_sem_give(&contact_save_gate_release);
	}
	contact_writer_finish(&contact_writer_thread_a, &update_writer, false);
	contact_save_gate_disarm();

	zassert_ok(gate_rc, "contact update did not reach persistence");
	zassert_false(reset_finished_early, "reset bypassed the active store transaction");
	zassert_ok(update_writer.rc, "concurrent update failed: %d", update_writer.rc);
	zassert_ok(reset_writer.rc, "concurrent reset failed: %d", reset_writer.rc);
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_equal(rc, -ENOENT, "contact should be absent after ordered reset: %d", rc);
	assert_contact_blob_deleted(slot);
	reload_contact_settings_index();
	rc = meshbus_contact_find_by_key(contact.public_key.bytes, &got);
	zassert_equal(rc, -ENOENT, "deleted contact resurrected after settings reload: %d", rc);
}

ZTEST_SUITE(meshbus_contact_contract, NULL, suite_setup, test_before, NULL, NULL);
