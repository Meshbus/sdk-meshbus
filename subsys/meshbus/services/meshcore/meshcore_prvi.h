/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_PRVI_H_
#define FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_PRVI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/meshbus/meshcore.h>
#include <zephyr/sys/util.h>

#include "meshcore/types.h"

#define MESHBUS_MESHCORE_ACK_HANDOFF_QUEUE_DEPTH 8U

struct meshbus_message_ack_response_event;

BUILD_ASSERT(MESHBUS_MESHCORE_PATH_MAX_LEN == MESHCORE_MAX_PATH_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_CHANNEL_DATA_PAYLOAD_MAX_LEN ==
	     MESHCORE_MAX_CHANNEL_DATA_PAYLOAD_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_BINARY_REQUEST_PAYLOAD_MAX_LEN ==
	     MESHCORE_MAX_SERVICE_REQUEST_PAYLOAD_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_BINARY_RESPONSE_PAYLOAD_MAX_LEN ==
	     MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_RAW_DATA_PAYLOAD_MAX_LEN ==
	     MESHCORE_MAX_RAW_DATA_PAYLOAD_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_CONTROL_DATA_PAYLOAD_MAX_LEN ==
	     MESHCORE_MAX_CONTROL_DATA_PAYLOAD_LEN);
BUILD_ASSERT(MESHBUS_MESHCORE_OUT_PATH_UNKNOWN == MESHCORE_OUT_PATH_UNKNOWN);
BUILD_ASSERT(MESHBUS_MESHCORE_CHANNEL_DATA_TYPE_RESERVED ==
	     MESHCORE_CHANNEL_DATA_TYPE_RESERVED);
BUILD_ASSERT(MESHBUS_MESHCORE_CHANNEL_DATA_TYPE_DEV ==
	     MESHCORE_CHANNEL_DATA_TYPE_DEV);

/* Configuration activation serializes engine execution on its owning workqueue. */
bool meshbus_meshcore_activation_pending(void);
int meshbus_meshcore_active_config_get(meshbus_meshcore_config *cfg);
void meshbus_meshcore_config_activate(const meshbus_meshcore_config *cfg);
int meshbus_meshcore_config_commit(const meshbus_meshcore_config *cfg, bool force);
void meshbus_meshcore_activation_complete(int result);
#if defined(CONFIG_MESHBUS_MESHCORE_RUNTIME)
void meshbus_meshcore_config_init_identity(meshbus_meshcore_config *cfg);
int meshbus_meshcore_runtime_apply(const meshbus_meshcore_config *cfg);
bool meshbus_meshcore_radio_tx_begin(void);
void meshbus_meshcore_radio_tx_abort(void);
void meshbus_meshcore_ack_handoff_reset(void);
#endif

int meshbus_meshcore_request_publish_accepted(
	const struct zbus_channel *chan, const void *msg);
void meshbus_meshcore_request_acceptance_report(
	const struct zbus_channel *chan, int result);
int meshbus_meshcore_ack_handoff_publish(
	const struct meshbus_message_ack_response_event *event);

static inline bool meshbus_meshcore_path_len_to_bytes(uint8_t path_len,
						      size_t *out_len,
						      uint8_t *out_hash_size)
{
	uint8_t hash_size;
	uint8_t hash_count;
	size_t len;

	if (out_len == NULL) {
		return false;
	}

	hash_size = (uint8_t)((path_len >> 6) + 1U);
	hash_count = (uint8_t)(path_len & 0x3fU);
	if (hash_size == 4U) {
		return false;
	}

	len = (size_t)hash_size * (size_t)hash_count;
	if (len > MESHCORE_MAX_PATH_LEN) {
		return false;
	}

	*out_len = len;
	if (out_hash_size != NULL) {
		*out_hash_size = hash_size;
	}
	return true;
}

static inline bool meshbus_meshcore_path_valid(const uint8_t *path,
					       uint8_t path_len,
					       bool allow_unknown)
{
	size_t path_bytes = 0U;

	if (path_len == MESHCORE_OUT_PATH_UNKNOWN) {
		return allow_unknown;
	}
	if (!meshbus_meshcore_path_len_to_bytes(path_len, &path_bytes, NULL)) {
		return false;
	}

	return path_bytes == 0U || path != NULL;
}

#if defined(CONFIG_MESHBUS_CONTACT)
void meshbus_meshcore_contact_trace_pending_clear(void);
void meshbus_meshcore_contact_trace_pending_register(uint32_t tag,
						     const uint8_t *key_prefix);
bool meshbus_meshcore_contact_trace_response_claim(
	const meshbus_meshcore_trace_response_event *event);
#endif

#endif /* FOBE_SUBSYS_MESHBUS_SERVICES_MESHCORE_PRVI_H_ */
