/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHBUS_SERVICES_MANAGEMENT_PRIV_H_
#define ZEPHYR_SUBSYS_MESHBUS_SERVICES_MANAGEMENT_PRIV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/meshbus/management.h>
#include <zephyr/meshbus/meshcore.h>
#include <zephyr/sys/util.h>

#define MANAGEMENT_OFF_MAGIC0 0U
#define MANAGEMENT_OFF_MAGIC1 1U
#define MANAGEMENT_OFF_VERSION 2U
#define MANAGEMENT_OFF_TYPE 3U
#define MANAGEMENT_OFF_TARGET_ID 4U
#define MANAGEMENT_OFF_SESSION_ID 12U
#define MANAGEMENT_OFF_SEQ 16U
#define MANAGEMENT_OFF_FRAG_INDEX 20U
#define MANAGEMENT_OFF_FRAG_COUNT 21U
#define MANAGEMENT_OFF_PAYLOAD_LEN 22U

#define MANAGEMENT_PLAINTEXT_MAX_LEN \
	(CONFIG_MESHBUS_MANAGEMENT_ANON_FRAME_MAX_LEN - \
	 MESHBUS_MANAGEMENT_HEADER_SIZE - MESHBUS_MANAGEMENT_AEAD_TAG_SIZE)
#define MANAGEMENT_REASSEMBLY_MAX_LEN \
	(MANAGEMENT_PLAINTEXT_MAX_LEN * CONFIG_MESHBUS_MANAGEMENT_MAX_FRAGMENTS)
#define MANAGEMENT_SMP_EFFECTIVE_MAX_LEN \
	MIN(MANAGEMENT_REASSEMBLY_MAX_LEN, \
	    MIN(CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE, \
		MIN(MESHBUS_MANAGEMENT_SMP_PACKET_MAX_LEN, \
		    MESHBUS_MANAGEMENT_SMP_RESPONSE_MAX_LEN)))
#define MANAGEMENT_SMP_FRAME_MAX_LEN CONFIG_MESHBUS_MANAGEMENT_ANON_FRAME_MAX_LEN
#define MANAGEMENT_SMP_PLAINTEXT_MAX_LEN MANAGEMENT_PLAINTEXT_MAX_LEN
#define MANAGEMENT_SMP_TIMEOUT_MS CONFIG_MESHBUS_MANAGEMENT_SMP_TIMEOUT_MS
#define MANAGEMENT_FRAGMENT_ACK_TIMEOUT_MS 30000U
#define MANAGEMENT_SMP_SEQ 7U
#define MESHBUS_MANAGEMENT_SETTINGS_SUBTREE "meshbus/management"
#define MESHBUS_MANAGEMENT_SETTINGS_KEY_CONFIG "config"

struct management_frame_header {
	uint8_t type;
	uint8_t target_id[MESHBUS_MANAGEMENT_TARGET_ID_SIZE];
	uint32_t session_id;
	uint32_t seq;
	uint8_t frag_index;
	uint8_t frag_count;
	uint16_t payload_len;
};

struct management_return_route {
	bool present;
	uint8_t path_byte_len;
	uint8_t path_hash_size;
	uint8_t path[MESHBUS_MESHCORE_PATH_MAX_LEN];
};

struct management_session_snapshot {
	uint8_t target_id[MESHBUS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t peer_public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t key[MESHBUS_MANAGEMENT_SESSION_KEY_SIZE];
	uint32_t session_id;
	bool direct_only;
	struct management_return_route return_route;
};

struct management_reassembly {
	bool active;
	uint32_t seq;
	uint8_t frag_count;
	uint8_t received_count;
	uint8_t received_mask;
	size_t len[CONFIG_MESHBUS_MANAGEMENT_MAX_FRAGMENTS];
	uint8_t data[MANAGEMENT_SMP_EFFECTIVE_MAX_LEN];
};

void management_secure_wipe(void *ptr, size_t len);
bool management_password_is_valid(const uint8_t *password, size_t password_len);
int management_random_get(uint8_t *out, size_t len);
void management_frame_header_write(uint8_t *frame,
				   const struct management_frame_header *hdr);
int management_frame_header_parse(const uint8_t *frame, size_t len,
				  struct management_frame_header *hdr,
				  const uint8_t **payload);
int management_aead_encrypt(const uint8_t key[MESHBUS_MANAGEMENT_SESSION_KEY_SIZE],
			    uint8_t *frame, const uint8_t *plain, size_t plain_len);
int management_aead_decrypt(const uint8_t key[MESHBUS_MANAGEMENT_SESSION_KEY_SIZE],
			    const uint8_t *frame,
			    const struct management_frame_header *hdr,
			    const uint8_t *cipher, uint8_t *plain,
			    size_t *plain_len);
int management_session_key_derive(
	const uint8_t *secret, size_t secret_len,
	const uint8_t target_id[MESHBUS_MANAGEMENT_TARGET_ID_SIZE],
	const uint8_t client_nonce[MESHBUS_MANAGEMENT_NONCE_SIZE],
	const uint8_t server_nonce[MESHBUS_MANAGEMENT_NONCE_SIZE],
	uint32_t session_id, const struct management_return_route *return_route,
	uint8_t out[MESHBUS_MANAGEMENT_SESSION_KEY_SIZE]);
int management_target_id_from_public_key(
	const uint8_t public_key[MESHBUS_MESHCORE_PUBLIC_KEY_SIZE],
	uint8_t out[MESHBUS_MANAGEMENT_TARGET_ID_SIZE]);
void management_sessions_clear(void);

#if defined(CONFIG_MESHBUS_MANAGEMENT_ENDPOINT)
void management_session_init(void);
void management_session_anon_data_response_handle(
	const struct meshbus_meshcore_anon_data_response_event *event);
#endif

#if defined(CONFIG_MESHBUS_MANAGEMENT_OPERATOR)
void management_smp_init(void);
void management_smp_session_clear(void);
void management_smp_anon_data_response_handle(
	const struct meshbus_meshcore_anon_data_response_event *event);
#endif

#endif /* ZEPHYR_SUBSYS_MESHBUS_SERVICES_MANAGEMENT_PRIV_H_ */
