/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MESHBUS_COMMON_MB_SETTINGS_H_
#define FOBE_SUBSYS_MESHBUS_COMMON_MB_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <pb.h>

struct mb_settings_blob_load_state {
	bool seen;
	bool invalid;
};

struct mb_settings_blob_schema {
	const char *subtree;
	const char *key;
	const pb_msgdesc_t *fields;
	size_t message_size;
};

struct mb_settings_indexed_blob_schema {
	const char *subtree;
	const char *key_prefix;
	const pb_msgdesc_t *fields;
	size_t message_size;
};

struct mb_settings_indexed_raw_schema {
	const char *subtree;
	const char *key_prefix;
};

#define MB_SETTINGS_BLOB_RECORD_HEADER_SIZE 4U
#define MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(encoded_size) \
	((encoded_size) + MB_SETTINGS_BLOB_RECORD_HEADER_SIZE)

#define MB_SETTINGS_BLOB_SCHEMA_DEFINE(name, subtree_value, key_value, proto_msg, message_type) \
	static const struct mb_settings_blob_schema name = {                                      \
		.subtree = (subtree_value),                                                      \
		.key = (key_value),                                                              \
		.fields = proto_msg##_fields,                                                    \
		.message_size = sizeof(message_type),                                            \
	}

#define MB_SETTINGS_INDEXED_BLOB_SCHEMA_DEFINE(name, subtree_value, key_prefix_value,          \
					       proto_msg, message_type)                       \
	static const struct mb_settings_indexed_blob_schema name = {                              \
		.subtree = (subtree_value),                                                      \
		.key_prefix = (key_prefix_value),                                                \
		.fields = proto_msg##_fields,                                                    \
		.message_size = sizeof(message_type),                                            \
	}

#define MB_SETTINGS_INDEXED_RAW_SCHEMA_DEFINE(name, subtree_value, key_prefix_value)            \
	static const struct mb_settings_indexed_raw_schema name = {                               \
		.subtree = (subtree_value),                                                      \
		.key_prefix = (key_prefix_value),                                                \
	}

void mb_settings_blob_load_state_reset(struct mb_settings_blob_load_state *state,
				       void *staging_msg, size_t message_size);

int mb_settings_blob_key_assemble(char *buf, size_t buf_sz,
				  const struct mb_settings_blob_schema *schema);

int mb_settings_blob_encode_with_buffer(const struct mb_settings_blob_schema *schema,
					const void *message, uint8_t *buffer,
					size_t buffer_size, size_t *len_out);

int mb_settings_blob_save_with_buffer(const struct mb_settings_blob_schema *schema,
				      const void *message, uint8_t *buffer,
				      size_t buffer_size);

int mb_settings_blob_save_locked_with_buffer(const struct mb_settings_blob_schema *schema,
					     struct k_mutex *mutex, const void *message,
					     uint8_t *buffer, size_t buffer_size);

int mb_settings_blob_delete(const struct mb_settings_blob_schema *schema);

int mb_settings_blob_handle_set_with_buffer(const struct mb_settings_blob_schema *schema,
					    struct k_mutex *mutex,
					    struct mb_settings_blob_load_state *state,
					    void *staging_msg, const char *name, size_t len,
					    settings_read_cb read_cb, void *cb_arg,
					    uint8_t *buffer, size_t buffer_size);

bool mb_settings_blob_commit_prepare(const struct mb_settings_blob_schema *schema,
				     struct k_mutex *mutex,
				     struct mb_settings_blob_load_state *state,
				     void *staging_msg, void *out_msg,
				     const bool *settings_initial_apply, bool *out_force);

int mb_settings_blob_export_with_buffer(const struct mb_settings_blob_schema *schema,
					const void *message, uint8_t *buffer,
					size_t buffer_size,
					int (*export_func)(const char *name,
							   const void *val, size_t len));

int mb_settings_blob_export_locked_with_buffer(const struct mb_settings_blob_schema *schema,
					       struct k_mutex *mutex, const void *message,
					       uint8_t *buffer, size_t buffer_size,
					       int (*export_func)(const char *name,
								  const void *val,
								  size_t len));

#define MB_SETTINGS_BLOB_CONFIG_DEFINE_HANDLERS(schema_obj, mutex_obj, load_state_obj,           \
						load_msg_obj, current_msg_obj, initial_apply_obj, \
						message_type, encoded_size, apply_fn, label)      \
	static int settings_handle_set(const char *name, size_t len, settings_read_cb read_cb,      \
				       void *cb_arg)                                          \
	{                                                                                        \
		uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(encoded_size)];                \
		return mb_settings_blob_handle_set_with_buffer(&(schema_obj), &(mutex_obj),       \
							       &(load_state_obj),             \
							       &(load_msg_obj), name, len,    \
							       read_cb, cb_arg, buffer,       \
							       sizeof(buffer));               \
	}                                                                                        \
	static int settings_handle_commit(void)                                                   \
	{                                                                                        \
		message_type cfg;                                                                \
		bool force;                                                                      \
		int rc;                                                                          \
		if (!mb_settings_blob_commit_prepare(&(schema_obj), &(mutex_obj),                 \
						     &(load_state_obj), &(load_msg_obj), &cfg,   \
						     &(initial_apply_obj), &force)) {            \
			return 0;                                                                \
		}                                                                                \
		rc = (apply_fn)(&cfg, false, force);                                              \
		if (rc != 0) {                                                                    \
			LOG_WRN("Ignoring invalid persisted " label " config: %d", rc);          \
		}                                                                                \
		return 0;                                                                        \
	}                                                                                        \
	static int settings_handle_export(int (*export_func)(const char *name, const void *val,    \
							    size_t len))                        \
	{                                                                                        \
		uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(encoded_size)];                \
		return mb_settings_blob_export_locked_with_buffer(&(schema_obj), &(mutex_obj),    \
								  &(current_msg_obj), buffer,     \
								  sizeof(buffer), export_func);   \
	}

#define MB_SETTINGS_BLOB_CONFIG_DEFINE_PERSISTENCE_WORK(schema_obj, mutex_obj, current_msg_obj,  \
						       encoded_size, fail_log_fn)       \
	static void settings_persistence_work_handler(struct k_work *work)                         \
	{                                                                                        \
		uint8_t buffer[MB_SETTINGS_BLOB_RECORD_BUFFER_SIZE(encoded_size)];                \
		int rc;                                                                          \
		ARG_UNUSED(work);                                                                \
		rc = mb_settings_blob_save_locked_with_buffer(&(schema_obj), &(mutex_obj),        \
							      &(current_msg_obj), buffer,     \
							      sizeof(buffer));               \
		if (rc != 0) {                                                                   \
			fail_log_fn("Settings persistence failed: %d", rc);                    \
		} else {                                                                         \
			LOG_DBG("Settings persistence complete");                               \
		}                                                                                \
	}

#define MB_SETTINGS_BLOB_CONFIG_DEFINE(schema_obj, mutex_obj, load_state_obj, load_msg_obj,      \
				       current_msg_obj, initial_apply_obj, message_type,         \
				       encoded_size, apply_fn, label)                           \
	MB_SETTINGS_BLOB_CONFIG_DEFINE_PERSISTENCE_WORK(schema_obj, mutex_obj, current_msg_obj,   \
							encoded_size, LOG_ERR)                  \
	MB_SETTINGS_BLOB_CONFIG_DEFINE_HANDLERS(schema_obj, mutex_obj, load_state_obj,         \
						load_msg_obj, current_msg_obj,                 \
						initial_apply_obj, message_type, encoded_size, \
						apply_fn, label)

int mb_settings_indexed_blob_key_assemble(char *buf, size_t buf_sz,
					  const struct mb_settings_indexed_blob_schema *schema,
					  size_t idx);

int mb_settings_indexed_blob_parse_slot_name(
	const struct mb_settings_indexed_blob_schema *schema, const char *name, size_t *idx_out);

int mb_settings_indexed_blob_load_encoded(const struct mb_settings_indexed_blob_schema *schema,
					  size_t idx, uint8_t *buffer, size_t buffer_size,
					  size_t *len_out);

int mb_settings_indexed_blob_load_with_buffer(
	const struct mb_settings_indexed_blob_schema *schema, size_t idx, void *message,
	uint8_t *buffer, size_t buffer_size);

int mb_settings_indexed_blob_read_with_buffer(
	const struct mb_settings_indexed_blob_schema *schema, size_t len,
	settings_read_cb read_cb, void *cb_arg, void *message, uint8_t *buffer,
	size_t buffer_size);

int mb_settings_indexed_blob_read_slot_with_buffer(
	const struct mb_settings_indexed_blob_schema *schema, const char *name, size_t len,
	settings_read_cb read_cb, void *cb_arg, size_t max_slots, void *message,
	uint8_t *buffer, size_t buffer_size, size_t *idx_out);

int mb_settings_indexed_blob_save_with_buffer(
	const struct mb_settings_indexed_blob_schema *schema, size_t idx, const void *message,
	uint8_t *buffer, size_t buffer_size);

int mb_settings_indexed_blob_delete(const struct mb_settings_indexed_blob_schema *schema,
				    size_t idx);

int mb_settings_indexed_blob_export_encoded(const struct mb_settings_indexed_blob_schema *schema,
					    size_t idx,
					    int (*export_func)(const char *name,
							       const void *val, size_t len),
					    uint8_t *buffer, size_t buffer_size);

int mb_settings_indexed_raw_key_assemble(char *buf, size_t buf_sz,
					 const struct mb_settings_indexed_raw_schema *schema,
					 size_t idx);

int mb_settings_indexed_raw_load(const struct mb_settings_indexed_raw_schema *schema,
				 size_t idx, uint8_t *buffer, size_t buffer_size,
				 size_t *len_out);

int mb_settings_indexed_raw_save(const struct mb_settings_indexed_raw_schema *schema,
				 size_t idx, const void *value, size_t len);

int mb_settings_indexed_raw_delete(const struct mb_settings_indexed_raw_schema *schema,
				   size_t idx);

int mb_settings_indexed_raw_export(const struct mb_settings_indexed_raw_schema *schema,
				   size_t idx,
				   int (*export_func)(const char *name,
						      const void *val, size_t len),
				   uint8_t *buffer, size_t buffer_size);

#endif /* FOBE_SUBSYS_MESHBUS_COMMON_MB_SETTINGS_H_ */
