/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <psa/crypto.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "management_priv.h"

BUILD_ASSERT(MBS_MANAGEMENT_HEADER_SIZE == 24U);
BUILD_ASSERT(MANAGEMENT_SMP_FRAME_MAX_LEN <= MBS_MANAGEMENT_FRAME_MAX_LEN);
BUILD_ASSERT(MANAGEMENT_SMP_FRAME_MAX_LEN <= MBS_MESHCORE_ANON_DATA_PAYLOAD_MAX_LEN);
BUILD_ASSERT(MANAGEMENT_PLAINTEXT_MAX_LEN > MGMT_HDR_SIZE);
BUILD_ASSERT(MANAGEMENT_SMP_PLAINTEXT_MAX_LEN > MGMT_HDR_SIZE);
BUILD_ASSERT(MANAGEMENT_SMP_EFFECTIVE_MAX_LEN >= MGMT_HDR_SIZE);
BUILD_ASSERT(MANAGEMENT_SMP_EFFECTIVE_MAX_LEN <=
	     MBS_MANAGEMENT_SMP_PACKET_MAX_LEN);
BUILD_ASSERT(MANAGEMENT_SMP_EFFECTIVE_MAX_LEN <=
	     MBS_MANAGEMENT_SMP_RESPONSE_MAX_LEN);
BUILD_ASSERT(MANAGEMENT_SMP_EFFECTIVE_MAX_LEN <=
	     CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE);

static atomic_t psa_inited = ATOMIC_INIT(0);

void management_secure_wipe(void *ptr, size_t len)
{
	volatile uint8_t *p = ptr;

	while (p != NULL && len > 0U) {
		*p++ = 0U;
		len--;
	}
}

void management_frame_header_write(uint8_t *frame,
				   const struct management_frame_header *hdr)
{
	frame[MANAGEMENT_OFF_MAGIC0] = MBS_MANAGEMENT_MAGIC0;
	frame[MANAGEMENT_OFF_MAGIC1] = MBS_MANAGEMENT_MAGIC1;
	frame[MANAGEMENT_OFF_VERSION] = MBS_MANAGEMENT_VERSION;
	frame[MANAGEMENT_OFF_TYPE] = hdr->type;
	memcpy(&frame[MANAGEMENT_OFF_TARGET_ID], hdr->target_id,
	       MBS_MANAGEMENT_TARGET_ID_SIZE);
	sys_put_le32(hdr->session_id, &frame[MANAGEMENT_OFF_SESSION_ID]);
	sys_put_le32(hdr->seq, &frame[MANAGEMENT_OFF_SEQ]);
	frame[MANAGEMENT_OFF_FRAG_INDEX] = hdr->frag_index;
	frame[MANAGEMENT_OFF_FRAG_COUNT] = hdr->frag_count;
	sys_put_le16(hdr->payload_len, &frame[MANAGEMENT_OFF_PAYLOAD_LEN]);
}

int management_frame_header_parse(const uint8_t *frame, size_t len,
				  struct management_frame_header *hdr,
				  const uint8_t **payload)
{
	size_t expected_len;
	uint16_t payload_len;

	if (frame == NULL || hdr == NULL || payload == NULL ||
	    len < MBS_MANAGEMENT_HEADER_SIZE) {
		return -EINVAL;
	}
	if (frame[MANAGEMENT_OFF_MAGIC0] != MBS_MANAGEMENT_MAGIC0 ||
	    frame[MANAGEMENT_OFF_MAGIC1] != MBS_MANAGEMENT_MAGIC1 ||
	    frame[MANAGEMENT_OFF_VERSION] != MBS_MANAGEMENT_VERSION) {
		return -EINVAL;
	}

	payload_len = sys_get_le16(&frame[MANAGEMENT_OFF_PAYLOAD_LEN]);
	if (payload_len > len - MBS_MANAGEMENT_HEADER_SIZE) {
		return -EINVAL;
	}
	expected_len = MBS_MANAGEMENT_HEADER_SIZE + payload_len;
	for (size_t i = expected_len; i < len; i++) {
		if (frame[i] != 0U) {
			return -EINVAL;
		}
	}

	hdr->type = frame[MANAGEMENT_OFF_TYPE];
	memcpy(hdr->target_id, &frame[MANAGEMENT_OFF_TARGET_ID],
	       MBS_MANAGEMENT_TARGET_ID_SIZE);
	hdr->session_id = sys_get_le32(&frame[MANAGEMENT_OFF_SESSION_ID]);
	hdr->seq = sys_get_le32(&frame[MANAGEMENT_OFF_SEQ]);
	hdr->frag_index = frame[MANAGEMENT_OFF_FRAG_INDEX];
	hdr->frag_count = frame[MANAGEMENT_OFF_FRAG_COUNT];
	hdr->payload_len = payload_len;
	*payload = &frame[MBS_MANAGEMENT_HEADER_SIZE];

	if (hdr->frag_count == 0U ||
	    hdr->frag_count > CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS ||
	    hdr->frag_index >= hdr->frag_count) {
		return -EINVAL;
	}

	return 0;
}

static void psa_init_once(void)
{
	if (atomic_cas(&psa_inited, 0, 1)) {
		(void)psa_crypto_init();
	}
}

int management_random_get(uint8_t *out, size_t len)
{
	psa_status_t status;

	if (out == NULL) {
		return -EINVAL;
	}

	psa_init_once();
	status = psa_generate_random(out, len);
	if (status == PSA_SUCCESS) {
		return 0;
	}

	sys_rand_get(out, len);
	return 0;
}

static int aes_key_import(const uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE],
			  psa_key_usage_t usage, psa_key_id_t *key_id)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	if (key == NULL || key_id == NULL) {
		return -EINVAL;
	}

	psa_set_key_usage_flags(&attributes, usage);
	psa_set_key_algorithm(&attributes,
			      PSA_ALG_AEAD_WITH_SHORTENED_TAG(
				      PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, MBS_MANAGEMENT_SESSION_KEY_SIZE * 8U);
	status = psa_import_key(&attributes, key, MBS_MANAGEMENT_SESSION_KEY_SIZE,
				key_id);
	psa_reset_key_attributes(&attributes);

	return status == PSA_SUCCESS ? 0 : -EIO;
}

static void aead_nonce_build(const struct management_frame_header *hdr,
			     uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE])
{
	sys_put_le32(hdr->session_id, &nonce[0]);
	sys_put_le32(hdr->seq, &nonce[4]);
	nonce[8] = hdr->type;
	nonce[9] = hdr->frag_index;
	nonce[10] = hdr->frag_count;
	nonce[11] = MBS_MANAGEMENT_VERSION;
}

int management_aead_encrypt(const uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE],
			    uint8_t *frame, const uint8_t *plain, size_t plain_len)
{
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE];
	uint8_t out[MANAGEMENT_PLAINTEXT_MAX_LEN + MBS_MANAGEMENT_AEAD_TAG_SIZE];
	struct management_frame_header hdr;
	const uint8_t *payload;
	size_t out_len = 0U;
	psa_status_t status;
	int rc;

	if (frame == NULL) {
		return -EINVAL;
	}

	rc = management_frame_header_parse(
		frame,
		MBS_MANAGEMENT_HEADER_SIZE +
			sys_get_le16(&frame[MANAGEMENT_OFF_PAYLOAD_LEN]),
		&hdr, &payload);
	if (rc != 0 || plain == NULL || plain_len > MANAGEMENT_PLAINTEXT_MAX_LEN ||
	    hdr.payload_len != plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		return -EINVAL;
	}

	psa_init_once();
	rc = aes_key_import(key, PSA_KEY_USAGE_ENCRYPT, &key_id);
	if (rc != 0) {
		return rc;
	}

	aead_nonce_build(&hdr, nonce);
	status = psa_aead_encrypt(key_id,
				  PSA_ALG_AEAD_WITH_SHORTENED_TAG(
					  PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE),
				  nonce, sizeof(nonce), frame,
				  MBS_MANAGEMENT_HEADER_SIZE, plain, plain_len,
				  out, sizeof(out), &out_len);
	(void)psa_destroy_key(key_id);
	if (status != PSA_SUCCESS ||
	    out_len != plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		management_secure_wipe(out, sizeof(out));
		return -EIO;
	}

	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE], out, out_len);
	management_secure_wipe(out, sizeof(out));
	return 0;
}

int management_aead_decrypt(const uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE],
			    const uint8_t *frame,
			    const struct management_frame_header *hdr,
			    const uint8_t *cipher, uint8_t *plain, size_t *plain_len)
{
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE];
	size_t out_len = 0U;
	psa_status_t status;
	int rc;

	if (key == NULL || frame == NULL || hdr == NULL || cipher == NULL ||
	    plain == NULL || plain_len == NULL ||
	    hdr->payload_len < MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		return -EINVAL;
	}

	psa_init_once();
	rc = aes_key_import(key, PSA_KEY_USAGE_DECRYPT, &key_id);
	if (rc != 0) {
		return rc;
	}

	aead_nonce_build(hdr, nonce);
	status = psa_aead_decrypt(key_id,
				  PSA_ALG_AEAD_WITH_SHORTENED_TAG(
					  PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE),
				  nonce, sizeof(nonce), frame,
				  MBS_MANAGEMENT_HEADER_SIZE, cipher,
				  hdr->payload_len, plain,
				  hdr->payload_len - MBS_MANAGEMENT_AEAD_TAG_SIZE,
				  &out_len);
	(void)psa_destroy_key(key_id);
	if (status != PSA_SUCCESS) {
		return -EACCES;
	}

	*plain_len = out_len;
	return 0;
}

bool management_password_is_valid(const uint8_t *password, size_t password_len)
{
	if (password == NULL || password_len < MBS_MANAGEMENT_SECRET_MIN_LEN ||
	    password_len > MBS_MANAGEMENT_SECRET_MAX_LEN) {
		return false;
	}

	for (size_t i = 0U; i < password_len; i++) {
		if (password[i] < 0x21U || password[i] > 0x7eU) {
			return false;
		}
	}

	return true;
}

int management_session_key_derive(
	const uint8_t *secret, size_t secret_len,
	const uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE],
	const uint8_t client_nonce[MBS_MANAGEMENT_NONCE_SIZE],
	const uint8_t server_nonce[MBS_MANAGEMENT_NONCE_SIZE],
	uint32_t session_id, const struct management_return_route *return_route,
	uint8_t out[MBS_MANAGEMENT_SESSION_KEY_SIZE])
{
	static const uint8_t label[] = "meshbus-management-v1";
	uint8_t input[MBS_MANAGEMENT_SECRET_MAX_LEN + sizeof(label) +
		      MBS_MANAGEMENT_TARGET_ID_SIZE +
		      (2U * MBS_MANAGEMENT_NONCE_SIZE) + sizeof(uint32_t) +
		      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE +
		      MBS_MESHCORE_PATH_MAX_LEN];
	uint8_t digest[32];
	size_t input_len = 0U;
	size_t out_len = 0U;
	psa_status_t status;

	if (!management_password_is_valid(secret, secret_len) || target_id == NULL ||
	    client_nonce == NULL || server_nonce == NULL ||
	    return_route == NULL || out == NULL ||
	    (return_route->present &&
	     (return_route->path_hash_size == 0U ||
	      return_route->path_hash_size > 3U ||
	      return_route->path_byte_len > sizeof(return_route->path) ||
	      return_route->path_byte_len % return_route->path_hash_size != 0U))) {
		return -EINVAL;
	}

	memcpy(&input[input_len], label, sizeof(label));
	input_len += sizeof(label);
	memcpy(&input[input_len], secret, secret_len);
	input_len += secret_len;
	memcpy(&input[input_len], target_id, MBS_MANAGEMENT_TARGET_ID_SIZE);
	input_len += MBS_MANAGEMENT_TARGET_ID_SIZE;
	memcpy(&input[input_len], client_nonce, MBS_MANAGEMENT_NONCE_SIZE);
	input_len += MBS_MANAGEMENT_NONCE_SIZE;
	memcpy(&input[input_len], server_nonce, MBS_MANAGEMENT_NONCE_SIZE);
	input_len += MBS_MANAGEMENT_NONCE_SIZE;
	sys_put_le32(session_id, &input[input_len]);
	input_len += sizeof(uint32_t);
	input[input_len++] = return_route->present
		? MBS_MANAGEMENT_SESSION_ROUTE_PRESENT : 0U;
	input[input_len++] = return_route->path_hash_size;
	input[input_len++] = return_route->path_byte_len;
	if (return_route->path_byte_len > 0U) {
		memcpy(&input[input_len], return_route->path,
		       return_route->path_byte_len);
		input_len += return_route->path_byte_len;
	}

	psa_init_once();
	status = psa_hash_compute(PSA_ALG_SHA_256, input, input_len, digest,
				  sizeof(digest), &out_len);
	management_secure_wipe(input, sizeof(input));
	if (status != PSA_SUCCESS || out_len != sizeof(digest)) {
		management_secure_wipe(digest, sizeof(digest));
		return -EIO;
	}

	memcpy(out, digest, MBS_MANAGEMENT_SESSION_KEY_SIZE);
	management_secure_wipe(digest, sizeof(digest));
	return 0;
}

int management_target_id_from_public_key(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	uint8_t out[MBS_MANAGEMENT_TARGET_ID_SIZE])
{
	static const uint8_t label[] = "meshbus-management-target-id-v1";
	uint8_t input[sizeof(label) + MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t digest[32];
	size_t out_len = 0U;
	psa_status_t status;

	if (public_key == NULL || out == NULL) {
		return -EINVAL;
	}

	memcpy(input, label, sizeof(label));
	memcpy(&input[sizeof(label)], public_key, MBS_MESHCORE_PUBLIC_KEY_SIZE);

	psa_init_once();
	status = psa_hash_compute(PSA_ALG_SHA_256, input, sizeof(input), digest,
				  sizeof(digest), &out_len);
	management_secure_wipe(input, sizeof(input));
	if (status != PSA_SUCCESS || out_len != sizeof(digest)) {
		management_secure_wipe(digest, sizeof(digest));
		return -EIO;
	}

	memcpy(out, digest, MBS_MANAGEMENT_TARGET_ID_SIZE);
	management_secure_wipe(digest, sizeof(digest));
	return 0;
}
