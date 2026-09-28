/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "mbs_settings_internal.h"

LOG_MODULE_REGISTER(mbs_settings, CONFIG_MBS_LOG_LEVEL);

#define MBS_SETTINGS_BLOB_RECORD_MAGIC_0 'M'
#define MBS_SETTINGS_BLOB_RECORD_MAGIC_1 'B'
#define MBS_SETTINGS_BLOB_RECORD_MAGIC_2 'S'
#define MBS_SETTINGS_BLOB_RECORD_VERSION 0x01U

static int settings_key_vsnprintk(char *buf, size_t buf_sz, const char *fmt, va_list ap)
{
	int rc;

	if (buf == NULL || buf_sz == 0U || fmt == NULL) {
		return -EINVAL;
	}

	rc = vsnprintk(buf, buf_sz, fmt, ap);
	if (rc < 0) {
		return rc;
	}
	if ((size_t)rc >= buf_sz) {
		return -ENAMETOOLONG;
	}

	return rc;
}

static int settings_key_snprintk(char *buf, size_t buf_sz, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = settings_key_vsnprintk(buf, buf_sz, fmt, ap);
	va_end(ap);

	return rc;
}

void mbs_settings_blob_load_state_reset(struct mbs_settings_blob_load_state *state,
				       void *staging_msg, size_t message_size)
{
	if (state == NULL || staging_msg == NULL || message_size == 0U) {
		return;
	}

	memset(staging_msg, 0, message_size);
	state->seen = false;
	state->invalid = false;
}

static void mbs_settings_blob_load_state_mark_invalid(struct k_mutex *mutex,
					      struct mbs_settings_blob_load_state *state)
{
	k_mutex_lock(mutex, K_FOREVER);
	state->invalid = true;
	k_mutex_unlock(mutex);
}

int mbs_settings_blob_key_assemble(char *buf, size_t buf_sz,
				  const struct mbs_settings_blob_schema *schema)
{
	if (buf == NULL || buf_sz == 0U || schema == NULL || schema->subtree == NULL ||
	    schema->key == NULL || schema->key[0] == '\0') {
		return -EINVAL;
	}

	return settings_key_snprintk(buf, buf_sz, "%s/%s", schema->subtree, schema->key);
}

static int mbs_settings_blob_encode_record(const pb_msgdesc_t *fields, const void *message,
					  uint8_t *buffer, size_t buffer_size,
					  size_t *len_out, const char *label)
{
	pb_ostream_t stream;

	if (fields == NULL || message == NULL || buffer == NULL || len_out == NULL) {
		return -EINVAL;
	}
	if (buffer_size < MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE) {
		return -ERANGE;
	}

	stream = pb_ostream_from_buffer(buffer + MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE,
				       buffer_size - MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE);
	if (!pb_encode(&stream, fields, message)) {
		LOG_WRN("Failed to encode settings blob %s: %s", label != NULL ? label : "",
			PB_GET_ERROR(&stream));
		return -EINVAL;
	}

	buffer[0] = MBS_SETTINGS_BLOB_RECORD_MAGIC_0;
	buffer[1] = MBS_SETTINGS_BLOB_RECORD_MAGIC_1;
	buffer[2] = MBS_SETTINGS_BLOB_RECORD_MAGIC_2;
	buffer[3] = MBS_SETTINGS_BLOB_RECORD_VERSION;

	*len_out = MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE + stream.bytes_written;
	return 0;
}

static int mbs_settings_blob_decode_record(const pb_msgdesc_t *fields, void *message,
					  size_t message_size, const uint8_t *buffer, size_t len,
					  const char *label)
{
	pb_istream_t stream;

	if (fields == NULL || message == NULL || message_size == 0U || buffer == NULL) {
		return -EINVAL;
	}
	if (len < MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE ||
	    buffer[0] != MBS_SETTINGS_BLOB_RECORD_MAGIC_0 ||
	    buffer[1] != MBS_SETTINGS_BLOB_RECORD_MAGIC_1 ||
	    buffer[2] != MBS_SETTINGS_BLOB_RECORD_MAGIC_2 ||
	    buffer[3] != MBS_SETTINGS_BLOB_RECORD_VERSION) {
		LOG_WRN("Invalid settings blob record header %s: len=%zu",
			label != NULL ? label : "", len);
		return -EINVAL;
	}

	memset(message, 0, message_size);
	stream = pb_istream_from_buffer(buffer + MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE,
				       len - MBS_SETTINGS_BLOB_RECORD_HEADER_SIZE);
	if (!pb_decode(&stream, fields, message)) {
		LOG_WRN("Invalid settings blob record %s: %s", label != NULL ? label : "",
			PB_GET_ERROR(&stream));
		return -EINVAL;
	}

	return 0;
}

int mbs_settings_blob_encode_with_buffer(const struct mbs_settings_blob_schema *schema,
					const void *message, uint8_t *buffer,
					size_t buffer_size, size_t *len_out)
{
	if (schema == NULL || schema->fields == NULL || message == NULL || buffer == NULL ||
	    buffer_size == 0U || len_out == NULL) {
		return -EINVAL;
	}

	return mbs_settings_blob_encode_record(schema->fields, message, buffer, buffer_size, len_out,
					      schema->key);
}

int mbs_settings_blob_save_with_buffer(const struct mbs_settings_blob_schema *schema,
				      const void *message, uint8_t *buffer,
				      size_t buffer_size)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	rc = mbs_settings_blob_key_assemble(key_buf, sizeof(key_buf), schema);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_blob_encode_with_buffer(schema, message, buffer, buffer_size, &len);
	if (rc != 0) {
		return rc;
	}

	return settings_save_one(key_buf, buffer, len);
}

int mbs_settings_blob_save_locked_with_buffer(const struct mbs_settings_blob_schema *schema,
					     struct k_mutex *mutex, const void *message,
					     uint8_t *buffer, size_t buffer_size)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (mutex == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_blob_key_assemble(key_buf, sizeof(key_buf), schema);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(mutex, K_FOREVER);
	rc = mbs_settings_blob_encode_with_buffer(schema, message, buffer, buffer_size, &len);
	k_mutex_unlock(mutex);
	if (rc != 0) {
		return rc;
	}

	return settings_save_one(key_buf, buffer, len);
}

int mbs_settings_blob_delete(const struct mbs_settings_blob_schema *schema)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	rc = mbs_settings_blob_key_assemble(key_buf, sizeof(key_buf), schema);
	if (rc < 0) {
		return rc;
	}

	rc = settings_delete(key_buf);
	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Settings blob delete failed (%s): %d", key_buf, rc);
	}

	return rc;
}

int mbs_settings_blob_handle_set_with_buffer(const struct mbs_settings_blob_schema *schema,
					    struct k_mutex *mutex,
					    struct mbs_settings_blob_load_state *state,
					    void *staging_msg, const char *name, size_t len,
					    settings_read_cb read_cb, void *cb_arg,
					    uint8_t *buffer, size_t buffer_size)
{
	const char *next = NULL;
	ssize_t bytes_read;

	if (schema == NULL || schema->fields == NULL || mutex == NULL || state == NULL ||
	    staging_msg == NULL || name == NULL || read_cb == NULL || buffer == NULL ||
	    buffer_size == 0U || schema->message_size == 0U) {
		return -EINVAL;
	}

	if (!settings_name_steq(name, schema->key, &next) || next != NULL) {
		return -ENOENT;
	}
	if (len == 0U) {
		return 0;
	}

	if (len > buffer_size) {
		LOG_WRN("Ignore oversized settings blob at %s/%s: %zu", schema->subtree,
			schema->key, len);
		mbs_settings_blob_load_state_mark_invalid(mutex, state);
		return 0;
	}

	bytes_read = read_cb(cb_arg, buffer, len);
	if (bytes_read < 0) {
		LOG_WRN("Ignore unreadable settings blob at %s/%s: %zd", schema->subtree,
			schema->key, bytes_read);
		mbs_settings_blob_load_state_mark_invalid(mutex, state);
		return 0;
	}
	if ((size_t)bytes_read != len) {
		LOG_WRN("Ignore short settings blob at %s/%s: expected %zu got %zd",
			schema->subtree, schema->key, len, bytes_read);
		mbs_settings_blob_load_state_mark_invalid(mutex, state);
		return 0;
	}

	if (mbs_settings_blob_decode_record(schema->fields, staging_msg, schema->message_size,
					   buffer, (size_t)bytes_read, schema->key) != 0) {
		LOG_WRN("Ignore invalid settings blob at %s/%s", schema->subtree, schema->key);
		mbs_settings_blob_load_state_mark_invalid(mutex, state);
		return 0;
	}

	k_mutex_lock(mutex, K_FOREVER);
	state->seen = true;
	k_mutex_unlock(mutex);
	return 0;
}

bool mbs_settings_blob_commit_prepare(const struct mbs_settings_blob_schema *schema,
				     struct k_mutex *mutex,
				     struct mbs_settings_blob_load_state *state,
				     void *staging_msg, void *out_msg,
				     const bool *settings_initial_apply, bool *out_force)
{
	bool seen;

	if (schema == NULL || mutex == NULL || state == NULL || staging_msg == NULL ||
	    out_msg == NULL || schema->message_size == 0U) {
		return false;
	}

	k_mutex_lock(mutex, K_FOREVER);
	seen = state->seen;
	if (seen) {
		memcpy(out_msg, staging_msg, schema->message_size);
	}
	if (out_force != NULL) {
		*out_force = settings_initial_apply != NULL ? !(*settings_initial_apply) : false;
	}
	mbs_settings_blob_load_state_reset(state, staging_msg, schema->message_size);
	k_mutex_unlock(mutex);

	return seen;
}

int mbs_settings_blob_export_with_buffer(const struct mbs_settings_blob_schema *schema,
					const void *message, uint8_t *buffer,
					size_t buffer_size,
					int (*export_func)(const char *name,
							   const void *val, size_t len))
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (export_func == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_blob_key_assemble(key_buf, sizeof(key_buf), schema);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_blob_encode_with_buffer(schema, message, buffer, buffer_size, &len);
	if (rc != 0) {
		return rc;
	}

	return export_func(key_buf, buffer, len);
}

int mbs_settings_blob_export_locked_with_buffer(const struct mbs_settings_blob_schema *schema,
					       struct k_mutex *mutex, const void *message,
					       uint8_t *buffer, size_t buffer_size,
					       int (*export_func)(const char *name,
								  const void *val, size_t len))
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (mutex == NULL || export_func == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_blob_key_assemble(key_buf, sizeof(key_buf), schema);
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(mutex, K_FOREVER);
	rc = mbs_settings_blob_encode_with_buffer(schema, message, buffer, buffer_size, &len);
	k_mutex_unlock(mutex);
	if (rc != 0) {
		return rc;
	}

	return export_func(key_buf, buffer, len);
}

static int mbs_settings_indexed_key_assemble(char *buf, size_t buf_sz, const char *subtree,
					    const char *key_prefix, size_t idx)
{
	if (buf == NULL || buf_sz == 0U || subtree == NULL) {
		return -EINVAL;
	}

	if (key_prefix != NULL && key_prefix[0] != '\0') {
		return settings_key_snprintk(buf, buf_sz, "%s/%s/%u", subtree, key_prefix,
					     (unsigned int)idx);
	}

	return settings_key_snprintk(buf, buf_sz, "%s/%u", subtree, (unsigned int)idx);
}

static int mbs_settings_indexed_parse_name(const char *key_prefix, const char *name,
					  size_t *idx_out)
{
	const char *slot_str;
	char *end = NULL;
	unsigned long idx_ul;

	if (name == NULL || idx_out == NULL) {
		return -EINVAL;
	}

	slot_str = name;
	if (key_prefix != NULL && key_prefix[0] != '\0') {
		size_t prefix_len = strlen(key_prefix);

		if (strncmp(name, key_prefix, prefix_len) != 0 || name[prefix_len] != '/') {
			return -ENOENT;
		}
		slot_str = name + prefix_len + 1U;
	}

	errno = 0;
	idx_ul = strtoul(slot_str, &end, 10);
	if (end == slot_str || end == NULL || *end != '\0' || errno == ERANGE ||
	    idx_ul > (unsigned long)SIZE_MAX) {
		return -ENOENT;
	}

	*idx_out = (size_t)idx_ul;
	return 0;
}

static int mbs_settings_indexed_load_raw_key(const char *key, uint8_t *buffer, size_t buffer_size,
					    size_t *len_out)
{
	ssize_t len;
	ssize_t loaded;

	if (key == NULL || buffer == NULL || len_out == NULL) {
		return -EINVAL;
	}

	len = settings_get_val_len(key);
	if (len < 0) {
		return (int)len;
	}
	if ((size_t)len > buffer_size) {
		return -ERANGE;
	}

	loaded = settings_load_one(key, buffer, buffer_size);
	if (loaded < 0) {
		return (int)loaded;
	}
	if (loaded != len) {
		return -EINVAL;
	}

	*len_out = (size_t)len;
	return 0;
}

static int mbs_settings_indexed_delete_key(const char *key)
{
	int rc;

	if (key == NULL) {
		return -EINVAL;
	}

	rc = settings_delete(key);
	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		LOG_WRN("Indexed settings delete failed (%s): %d", key, rc);
	}

	return rc;
}

int mbs_settings_indexed_blob_key_assemble(char *buf, size_t buf_sz,
					  const struct mbs_settings_indexed_blob_schema *schema,
					  size_t idx)
{
	if (schema == NULL) {
		return -EINVAL;
	}

	return mbs_settings_indexed_key_assemble(buf, buf_sz, schema->subtree, schema->key_prefix,
						idx);
}

int mbs_settings_indexed_blob_parse_slot_name(
	const struct mbs_settings_indexed_blob_schema *schema, const char *name, size_t *idx_out)
{
	if (schema == NULL) {
		return -EINVAL;
	}

	return mbs_settings_indexed_parse_name(schema->key_prefix, name, idx_out);
}

int mbs_settings_indexed_blob_load_encoded(const struct mbs_settings_indexed_blob_schema *schema,
					  size_t idx, uint8_t *buffer, size_t buffer_size,
					  size_t *len_out)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	rc = mbs_settings_indexed_blob_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_indexed_load_raw_key(key_buf, buffer, buffer_size, len_out);
	if (rc == 0 && *len_out == 0U) {
		return -ENOENT;
	}

	return rc;
}

int mbs_settings_indexed_blob_load_with_buffer(
	const struct mbs_settings_indexed_blob_schema *schema, size_t idx, void *message,
	uint8_t *buffer, size_t buffer_size)
{
	size_t len;
	int rc;

	if (schema == NULL || schema->fields == NULL || message == NULL || buffer == NULL ||
	    buffer_size == 0U || schema->message_size == 0U) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_blob_load_encoded(schema, idx, buffer, buffer_size, &len);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_settings_blob_decode_record(schema->fields, message, schema->message_size, buffer,
					    len, schema->key_prefix);
	if (rc != 0) {
		LOG_WRN("Invalid indexed settings blob at %s/%s/%u", schema->subtree,
			schema->key_prefix != NULL ? schema->key_prefix : "", (unsigned int)idx);
		return -EINVAL;
	}

	return 0;
}

int mbs_settings_indexed_blob_read_with_buffer(
	const struct mbs_settings_indexed_blob_schema *schema, size_t len,
	settings_read_cb read_cb, void *cb_arg, void *message, uint8_t *buffer,
	size_t buffer_size)
{
	ssize_t bytes_read;

	if (schema == NULL || schema->fields == NULL || read_cb == NULL || message == NULL ||
	    buffer == NULL || buffer_size == 0U || schema->message_size == 0U) {
		return -EINVAL;
	}
	if (len == 0U) {
		return -ENOENT;
	}
	if (len > buffer_size) {
		return -ERANGE;
	}

	bytes_read = read_cb(cb_arg, buffer, len);
	if (bytes_read < 0) {
		return (int)bytes_read;
	}
	if ((size_t)bytes_read != len) {
		return -EINVAL;
	}

	if (mbs_settings_blob_decode_record(schema->fields, message, schema->message_size, buffer,
					   (size_t)bytes_read, schema->key_prefix) != 0) {
		LOG_WRN("Invalid indexed settings blob under %s/%s", schema->subtree,
			schema->key_prefix != NULL ? schema->key_prefix : "");
		return -EINVAL;
	}

	return 0;
}

int mbs_settings_indexed_blob_read_slot_with_buffer(
	const struct mbs_settings_indexed_blob_schema *schema, const char *name, size_t len,
	settings_read_cb read_cb, void *cb_arg, size_t max_slots, void *message,
	uint8_t *buffer, size_t buffer_size, size_t *idx_out)
{
	size_t idx;
	int rc;

	if (idx_out == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_blob_parse_slot_name(schema, name, &idx);
	if (rc != 0) {
		return rc;
	}
	*idx_out = idx;
	if (idx >= max_slots) {
		return -ERANGE;
	}

	rc = mbs_settings_indexed_blob_read_with_buffer(schema, len, read_cb, cb_arg, message,
						      buffer, buffer_size);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

int mbs_settings_indexed_blob_save_with_buffer(
	const struct mbs_settings_indexed_blob_schema *schema, size_t idx, const void *message,
	uint8_t *buffer, size_t buffer_size)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (schema == NULL || schema->fields == NULL || message == NULL || buffer == NULL ||
	    buffer_size == 0U) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_blob_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_blob_encode_record(schema->fields, message, buffer, buffer_size, &len,
					    key_buf);
	if (rc != 0) {
		return rc;
	}

	return settings_save_one(key_buf, buffer, len);
}

int mbs_settings_indexed_blob_delete(const struct mbs_settings_indexed_blob_schema *schema,
				    size_t idx)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	rc = mbs_settings_indexed_blob_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	return mbs_settings_indexed_delete_key(key_buf);
}

int mbs_settings_indexed_blob_export_encoded(const struct mbs_settings_indexed_blob_schema *schema,
					    size_t idx,
					    int (*export_func)(const char *name,
							       const void *val, size_t len),
					    uint8_t *buffer, size_t buffer_size)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (schema == NULL || export_func == NULL || buffer == NULL || buffer_size == 0U) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_blob_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_indexed_load_raw_key(key_buf, buffer, buffer_size, &len);
	if (rc == -ENOENT || (rc == 0 && len == 0U)) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}

	return export_func(key_buf, buffer, len);
}

int mbs_settings_indexed_raw_key_assemble(char *buf, size_t buf_sz,
					 const struct mbs_settings_indexed_raw_schema *schema,
					 size_t idx)
{
	if (schema == NULL) {
		return -EINVAL;
	}

	return mbs_settings_indexed_key_assemble(buf, buf_sz, schema->subtree, schema->key_prefix,
						idx);
}

int mbs_settings_indexed_raw_load(const struct mbs_settings_indexed_raw_schema *schema,
				 size_t idx, uint8_t *buffer, size_t buffer_size,
				 size_t *len_out)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	rc = mbs_settings_indexed_raw_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	return mbs_settings_indexed_load_raw_key(key_buf, buffer, buffer_size, len_out);
}

int mbs_settings_indexed_raw_save(const struct mbs_settings_indexed_raw_schema *schema,
				 size_t idx, const void *value, size_t len)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	if (value == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_raw_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	return settings_save_one(key_buf, value, len);
}

int mbs_settings_indexed_raw_delete(const struct mbs_settings_indexed_raw_schema *schema,
				   size_t idx)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	int rc;

	if (schema == NULL) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_raw_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	return mbs_settings_indexed_delete_key(key_buf);
}

int mbs_settings_indexed_raw_export(const struct mbs_settings_indexed_raw_schema *schema,
				   size_t idx,
				   int (*export_func)(const char *name,
						      const void *val, size_t len),
				   uint8_t *buffer, size_t buffer_size)
{
	char key_buf[SETTINGS_FULL_NAME_LEN];
	size_t len;
	int rc;

	if (schema == NULL || export_func == NULL || buffer == NULL || buffer_size == 0U) {
		return -EINVAL;
	}

	rc = mbs_settings_indexed_raw_key_assemble(key_buf, sizeof(key_buf), schema, idx);
	if (rc < 0) {
		return rc;
	}

	rc = mbs_settings_indexed_load_raw_key(key_buf, buffer, buffer_size, &len);
	if (rc == -ENOENT) {
		return 0;
	}
	if (rc != 0) {
		return rc;
	}

	return export_func(key_buf, buffer, len);
}

static int settings_init_status = -EAGAIN;

int mbs_settings_init_status(void)
{
	return settings_init_status;
}

static int mbs_settings_init(void)
{
	int rc = settings_subsys_init();

	settings_init_status = rc;

	if (rc != 0) {
		LOG_ERR("Settings subsystem init failed: %d", rc);
		return rc;
	}

	LOG_DBG("Settings subsystem initialized");
	return 0;
}

/*
 * Run early in APPLICATION init so Settings backends are ready before Meshbus modules that
 * call settings_load_subtree()/settings_save_one().
 *
 * Note: SYS_INIT priority must be a literal or a single macro expanding to a literal;
 * expressions are not permitted.
 */
SYS_INIT(mbs_settings_init, APPLICATION, 0);
