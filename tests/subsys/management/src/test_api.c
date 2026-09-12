/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>

#include <psa/crypto.h>

#include <zephyr/sys/util.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/zephyr/zephyr_basic.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include <contact/contact.h>
#include <meshcore/meshcore.h>
#include <management/management.h>

#include "meshcore_prvi.h"
#include "meshbus/firmware.pb.h"
#include "meshbus/meshcore.pb.h"
#include "meshbus/management.pb.h"

/* Public Management API, protocol, and ZBus contract coverage. */

#define TEST_PLAINTEXT_MAX_LEN \
	(CONFIG_MBS_MANAGEMENT_ANON_FRAME_MAX_LEN - \
	 MBS_MANAGEMENT_HEADER_SIZE - MBS_MANAGEMENT_AEAD_TAG_SIZE)
#define TEST_SMP_EFFECTIVE_MAX_LEN \
	MIN(TEST_PLAINTEXT_MAX_LEN * CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS, \
	    MIN(CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE, \
		MIN(MBS_MANAGEMENT_SMP_PACKET_MAX_LEN, \
		    MBS_MANAGEMENT_SMP_RESPONSE_MAX_LEN)))
#define TEST_SECRET "test-pass-123"
#define TEST_SECRET_ROTATED "next-pass-456"

#define TEST_OFF_MAGIC0 0U
#define TEST_OFF_MAGIC1 1U
#define TEST_OFF_VERSION 2U
#define TEST_OFF_TYPE 3U
#define TEST_OFF_TARGET_ID 4U
#define TEST_OFF_SESSION_ID 12U
#define TEST_OFF_SEQ 16U
#define TEST_OFF_FRAG_INDEX 20U
#define TEST_OFF_FRAG_COUNT 21U
#define TEST_OFF_PAYLOAD_LEN 22U

struct test_header {
	uint8_t type;
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint32_t session_id;
	uint32_t seq;
	uint8_t frag_index;
	uint8_t frag_count;
	uint16_t payload_len;
};

struct test_session {
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t client_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t server_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	uint32_t session_id;
	bool return_route_present;
	uint8_t return_path_byte_len;
	uint8_t return_path_hash_size;
	uint8_t return_path[MBS_MESHCORE_PATH_MAX_LEN];
};

static uint8_t test_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];

static void test_secret_set(const char *secret);

ZBUS_MSG_SUBSCRIBER_DEFINE(test_smp_response_sub);
ZBUS_CHAN_ADD_OBS(mbs_management_smp_response_chan,
		  test_smp_response_sub, 0);

ZBUS_MSG_SUBSCRIBER_DEFINE(test_anon_data_request_sub);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_anon_data_send_request_chan,
		  test_anon_data_request_sub, 0);

static void test_anon_data_acceptance_cb(const struct zbus_channel *chan)
{
	mbs_meshcore_request_acceptance_report(chan, 0);
}

ZBUS_LISTENER_DEFINE(test_anon_data_acceptance_listener,
		     test_anon_data_acceptance_cb);
ZBUS_CHAN_ADD_OBS(mbs_meshcore_anon_data_send_request_chan,
		  test_anon_data_acceptance_listener, 1);

static void secure_wipe(void *ptr, size_t len)
{
	volatile uint8_t *p = ptr;

	while (p != NULL && len > 0U) {
		*p++ = 0U;
		len--;
	}
}

static void test_frame_header_write(uint8_t *frame, const struct test_header *hdr)
{
	frame[TEST_OFF_MAGIC0] = MBS_MANAGEMENT_MAGIC0;
	frame[TEST_OFF_MAGIC1] = MBS_MANAGEMENT_MAGIC1;
	frame[TEST_OFF_VERSION] = MBS_MANAGEMENT_VERSION;
	frame[TEST_OFF_TYPE] = hdr->type;
	memcpy(&frame[TEST_OFF_TARGET_ID], hdr->target_id,
	       MBS_MANAGEMENT_TARGET_ID_SIZE);
	sys_put_le32(hdr->session_id, &frame[TEST_OFF_SESSION_ID]);
	sys_put_le32(hdr->seq, &frame[TEST_OFF_SEQ]);
	frame[TEST_OFF_FRAG_INDEX] = hdr->frag_index;
	frame[TEST_OFF_FRAG_COUNT] = hdr->frag_count;
	sys_put_le16(hdr->payload_len, &frame[TEST_OFF_PAYLOAD_LEN]);
}

static void test_frame_header_parse(const uint8_t *frame, size_t len,
				    struct test_header *hdr)
{
	zassert_true(len >= MBS_MANAGEMENT_HEADER_SIZE);
	zassert_equal(frame[TEST_OFF_MAGIC0], MBS_MANAGEMENT_MAGIC0);
	zassert_equal(frame[TEST_OFF_MAGIC1], MBS_MANAGEMENT_MAGIC1);
	zassert_equal(frame[TEST_OFF_VERSION], MBS_MANAGEMENT_VERSION);

	hdr->type = frame[TEST_OFF_TYPE];
	memcpy(hdr->target_id, &frame[TEST_OFF_TARGET_ID],
	       MBS_MANAGEMENT_TARGET_ID_SIZE);
	hdr->session_id = sys_get_le32(&frame[TEST_OFF_SESSION_ID]);
	hdr->seq = sys_get_le32(&frame[TEST_OFF_SEQ]);
	hdr->frag_index = frame[TEST_OFF_FRAG_INDEX];
	hdr->frag_count = frame[TEST_OFF_FRAG_COUNT];
	hdr->payload_len = sys_get_le16(&frame[TEST_OFF_PAYLOAD_LEN]);
	zassert_equal(len, MBS_MANAGEMENT_HEADER_SIZE + hdr->payload_len);
}

static void test_key_derive(const char *secret, const struct test_session *session,
			    uint8_t out[MBS_MANAGEMENT_SESSION_KEY_SIZE])
{
	static const uint8_t label[] = "meshbus-management-v2";
	uint8_t input[MBS_MANAGEMENT_SECRET_MAX_LEN + sizeof(label) +
		      MBS_MANAGEMENT_TARGET_ID_SIZE +
		      2U * MBS_MANAGEMENT_NONCE_SIZE + sizeof(uint32_t) +
		      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE +
		      MBS_MESHCORE_PATH_MAX_LEN];
	uint8_t digest[32];
	size_t input_len = 0U;
	size_t digest_len = 0U;
	size_t secret_len = strlen(secret);
	psa_status_t status;

	zassert_true(secret_len >= MBS_MANAGEMENT_SECRET_MIN_LEN);
	zassert_true(secret_len <= MBS_MANAGEMENT_SECRET_MAX_LEN);

	memcpy(&input[input_len], label, sizeof(label));
	input_len += sizeof(label);
	memcpy(&input[input_len], secret, secret_len);
	input_len += secret_len;
	memcpy(&input[input_len], session->target_id, sizeof(session->target_id));
	input_len += sizeof(session->target_id);
	memcpy(&input[input_len], session->client_nonce, sizeof(session->client_nonce));
	input_len += sizeof(session->client_nonce);
	memcpy(&input[input_len], session->server_nonce, sizeof(session->server_nonce));
	input_len += sizeof(session->server_nonce);
	sys_put_le32(session->session_id, &input[input_len]);
	input_len += sizeof(uint32_t);
	input[input_len++] = session->return_route_present
		? MBS_MANAGEMENT_SESSION_ROUTE_PRESENT : 0U;
	input[input_len++] = session->return_path_hash_size;
	input[input_len++] = session->return_path_byte_len;
	if (session->return_path_byte_len > 0U) {
		memcpy(&input[input_len], session->return_path,
		       session->return_path_byte_len);
		input_len += session->return_path_byte_len;
	}

	status = psa_hash_compute(PSA_ALG_SHA_256, input, input_len, digest,
				  sizeof(digest), &digest_len);
	zassert_equal(status, PSA_SUCCESS);
	zassert_equal(digest_len, sizeof(digest));
	memcpy(out, digest, MBS_MANAGEMENT_SESSION_KEY_SIZE);
	secure_wipe(input, sizeof(input));
	secure_wipe(digest, sizeof(digest));
}

static void test_aead_nonce_build(const struct test_header *hdr,
				  uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE])
{
	sys_put_le32(hdr->session_id, &nonce[0]);
	sys_put_le32(hdr->seq, &nonce[4]);
	nonce[8] = hdr->type;
	nonce[9] = hdr->frag_index;
	nonce[10] = hdr->frag_count;
	nonce[11] = MBS_MANAGEMENT_VERSION;
}

static void test_aead_encrypt(const uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE],
			      uint8_t *frame, const uint8_t *plain, size_t plain_len)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE];
	uint8_t out[TEST_PLAINTEXT_MAX_LEN + MBS_MANAGEMENT_AEAD_TAG_SIZE];
	struct test_header hdr;
	size_t out_len = 0U;
	psa_status_t status;

	test_frame_header_parse(
		frame, MBS_MANAGEMENT_HEADER_SIZE + plain_len +
			       MBS_MANAGEMENT_AEAD_TAG_SIZE,
		&hdr);
	zassert_equal(hdr.payload_len,
		      plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE);

	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attributes,
			      PSA_ALG_AEAD_WITH_SHORTENED_TAG(
				      PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, MBS_MANAGEMENT_SESSION_KEY_SIZE * 8U);
	status = psa_import_key(&attributes, key,
				MBS_MANAGEMENT_SESSION_KEY_SIZE, &key_id);
	psa_reset_key_attributes(&attributes);
	zassert_equal(status, PSA_SUCCESS);

	test_aead_nonce_build(&hdr, nonce);
	status = psa_aead_encrypt(key_id,
				  PSA_ALG_AEAD_WITH_SHORTENED_TAG(
					  PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE),
				  nonce, sizeof(nonce), frame,
				  MBS_MANAGEMENT_HEADER_SIZE, plain, plain_len,
				  out, sizeof(out), &out_len);
	(void)psa_destroy_key(key_id);
	zassert_equal(status, PSA_SUCCESS);
	zassert_equal(out_len, plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE);
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE], out, out_len);
	secure_wipe(out, sizeof(out));
}

static void test_aead_decrypt(const uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE],
			      const uint8_t *frame, size_t frame_len, uint8_t *plain,
			      size_t *plain_len)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key_id = PSA_KEY_ID_NULL;
	uint8_t nonce[MBS_MANAGEMENT_AEAD_NONCE_SIZE];
	struct test_header hdr;
	size_t out_len = 0U;
	psa_status_t status;

	test_frame_header_parse(frame, frame_len, &hdr);
	zassert_true(hdr.payload_len > MBS_MANAGEMENT_AEAD_TAG_SIZE);

	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&attributes,
			      PSA_ALG_AEAD_WITH_SHORTENED_TAG(
				      PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE));
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, MBS_MANAGEMENT_SESSION_KEY_SIZE * 8U);
	status = psa_import_key(&attributes, key,
				MBS_MANAGEMENT_SESSION_KEY_SIZE, &key_id);
	psa_reset_key_attributes(&attributes);
	zassert_equal(status, PSA_SUCCESS);

	test_aead_nonce_build(&hdr, nonce);
	status = psa_aead_decrypt(key_id,
				  PSA_ALG_AEAD_WITH_SHORTENED_TAG(
					  PSA_ALG_CCM, MBS_MANAGEMENT_AEAD_TAG_SIZE),
				  nonce, sizeof(nonce), frame,
				  MBS_MANAGEMENT_HEADER_SIZE,
				  &frame[MBS_MANAGEMENT_HEADER_SIZE],
				  hdr.payload_len, plain,
				  hdr.payload_len - MBS_MANAGEMENT_AEAD_TAG_SIZE,
				  &out_len);
	(void)psa_destroy_key(key_id);
	zassert_equal(status, PSA_SUCCESS);
	*plain_len = out_len;
}

static void test_identity_prepare_unprovisioned(void)
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;

	zassert_ok(mbs_meshcore_config_get(&cfg));
	for (size_t i = 0; i < sizeof(test_public_key); i++) {
		test_public_key[i] = 0x30U + i;
	}

	zassert_ok(mbs_management_config_reset());
	cfg.public_key.size = sizeof(test_public_key);
	memcpy(cfg.public_key.bytes, test_public_key, sizeof(test_public_key));
	zassert_ok(mbs_meshcore_config_set(&cfg));
}

static void test_identity_prepare(void)
{
	test_identity_prepare_unprovisioned();
	test_secret_set(TEST_SECRET);
}

static void test_target_id_derive_from_public_key(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	uint8_t out[MBS_MANAGEMENT_TARGET_ID_SIZE])
{
	static const uint8_t label[] = "meshbus-management-target-id-v1";
	uint8_t input[sizeof(label) + MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t digest[32];
	size_t digest_len = 0U;
	psa_status_t status;

	zassert_not_null(public_key);
	zassert_not_null(out);
	memcpy(input, label, sizeof(label));
	memcpy(&input[sizeof(label)], public_key, MBS_MESHCORE_PUBLIC_KEY_SIZE);

	status = psa_hash_compute(PSA_ALG_SHA_256, input, sizeof(input), digest,
				  sizeof(digest), &digest_len);
	zassert_equal(status, PSA_SUCCESS);
	zassert_equal(digest_len, sizeof(digest));
	memcpy(out, digest, MBS_MANAGEMENT_TARGET_ID_SIZE);
	secure_wipe(input, sizeof(input));
	secure_wipe(digest, sizeof(digest));
}

static void test_target_id_derive(uint8_t out[MBS_MANAGEMENT_TARGET_ID_SIZE])
{
	test_target_id_derive_from_public_key(test_public_key, out);
}

static void test_secret_set(const char *secret)
{
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	size_t secret_len = strlen(secret);

	zassert_ok(mbs_management_config_get(&cfg));
	cfg.secret.size = (pb_size_t)secret_len;
	memcpy(cfg.secret.bytes, secret, secret_len);
	zassert_ok(mbs_management_config_set(&cfg));
	secure_wipe(&cfg, sizeof(cfg));
}

static void test_smp_response_drain(void)
{
	const struct zbus_channel *chan;
	mbs_management_smp_response_event event;

	while (zbus_sub_wait_msg(&test_smp_response_sub, &chan, &event,
				 K_NO_WAIT) == 0) {
	}
}

static bool test_wait_smp_response(
	mbs_management_smp_response_event *event)
{
	const struct zbus_channel *chan;
	struct mbs_meshcore_anon_data_send_request_event ack = {0};
	struct test_header hdr;
	int rc;

	rc = zbus_sub_wait_msg(&test_smp_response_sub, &chan, event,
			       K_MSEC(200));
	if (rc != 0 || event->status != 0 || event->response_len == 0U) {
		return rc == 0;
	}

	/* A successful response is complete only after its final fragment ACK. */
	rc = zbus_sub_wait_msg(&test_anon_data_request_sub, &chan, &ack,
			       K_MSEC(200));
	zassert_ok(rc, "final DATA_RESPONSE_ACK was not published");
	test_frame_header_parse(ack.payload, ack.payload_len, &hdr);
	zassert_equal(hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK,
		      "unexpected post-response frame type=%u", hdr.type);
	zassert_equal(hdr.frag_index + 1U, hdr.frag_count,
		      "response completion ACK must name the final fragment");

	return true;
}

static bool test_wait_smp_response_timeout(
	mbs_management_smp_response_event *event, k_timeout_t timeout)
{
	const struct zbus_channel *chan;

	return zbus_sub_wait_msg(&test_smp_response_sub, &chan, event, timeout) == 0;
}

static void test_anon_data_request_drain(void)
{
	const struct zbus_channel *chan;
	struct mbs_meshcore_anon_data_send_request_event event;

	while (zbus_sub_wait_msg(&test_anon_data_request_sub, &chan, &event,
				 K_NO_WAIT) == 0) {
	}
}

static bool test_wait_anon_data_request(
	struct mbs_meshcore_anon_data_send_request_event *event)
{
	const struct zbus_channel *chan;

	return zbus_sub_wait_msg(&test_anon_data_request_sub, &chan, event,
				 K_MSEC(200)) == 0;
}

static bool test_wait_anon_data_request_timeout(
	struct mbs_meshcore_anon_data_send_request_event *event,
	k_timeout_t timeout)
{
	const struct zbus_channel *chan;

	return zbus_sub_wait_msg(&test_anon_data_request_sub, &chan, event,
				 timeout) == 0;
}

static void test_expect_no_anon_data_request(void)
{
	const struct zbus_channel *chan;
	struct mbs_meshcore_anon_data_send_request_event event = {0};
	int rc;

	rc = zbus_sub_wait_msg(&test_anon_data_request_sub, &chan, &event,
			       K_MSEC(50));
	zassert_not_equal(
		rc, 0,
		"unexpected anon frame type=%u len=%u peer=%02x%02x session=%u status=%d",
		event.payload_len > 3U ? event.payload[3] : 0xffU,
		event.payload_len, event.public_key[0], event.public_key[1],
		event.payload_len >= MBS_MANAGEMENT_HEADER_SIZE
			? sys_get_le32(&event.payload[TEST_OFF_SESSION_ID])
			: 0U,
		event.payload_len >= MBS_MANAGEMENT_HEADER_SIZE + sizeof(uint32_t)
			? (int32_t)sys_get_le32(
				  &event.payload[MBS_MANAGEMENT_HEADER_SIZE])
			: 0);
}

static void test_expect_session_reject_from(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	uint32_t session_id, int status)
{
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header hdr;

	zassert_true(test_wait_anon_data_request(&tx),
		     "SESSION_REJECT was not published");
	zassert_mem_equal(tx.public_key, public_key,
			  MBS_MESHCORE_PUBLIC_KEY_SIZE,
			  "SESSION_REJECT target mismatch");
	test_frame_header_parse(tx.payload, tx.payload_len, &hdr);
	zassert_equal(hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT);
	zassert_equal(hdr.session_id, session_id);
	zassert_equal(hdr.payload_len, sizeof(uint32_t));
	zassert_equal((int32_t)sys_get_le32(
			      &tx.payload[MBS_MANAGEMENT_HEADER_SIZE]),
		      status);
}

static void test_expect_session_reject(uint32_t session_id, int status)
{
	test_expect_session_reject_from(test_public_key, session_id, status);
}

static void test_contact_prepare(mbs_contact *contact, uint8_t seed,
				 bool include_secret)
{
	size_t secret_len = strlen(TEST_SECRET);

	zassert_not_null(contact);
	*contact = (mbs_contact)meshbus_Contact_init_zero;
	contact->role = MBS_CONTACT_ROLE_REPEATER;
	contact->public_key.size = MBS_CONTACT_PUBLIC_KEY_SIZE;
	for (size_t i = 0; i < contact->public_key.size; i++) {
		contact->public_key.bytes[i] = seed + i;
	}
	if (include_secret) {
		contact->management_secret.size = (pb_size_t)secret_len;
		memcpy(contact->management_secret.bytes, TEST_SECRET, secret_len);
	}
}

static void test_anon_data_response_publish_route(const uint8_t *public_key,
						  const uint8_t *payload,
						  size_t payload_len,
						  uint8_t route, uint8_t path_len)
{
	struct mbs_meshcore_anon_data_response_event response = {0};

	zassert_not_null(public_key);
	zassert_true(payload_len <= sizeof(response.payload));
	response.route = route;
	response.path_len = path_len;
	memcpy(response.public_key, public_key, sizeof(response.public_key));
	response.payload_len = (uint8_t)payload_len;
	if (payload_len > 0U) {
		zassert_not_null(payload);
		memcpy(response.payload, payload, payload_len);
	}
	zassert_ok(zbus_chan_pub(&mbs_meshcore_anon_data_response_chan, &response,
				 K_NO_WAIT));
	k_sleep(K_MSEC(20));
}

static void test_anon_data_response_publish(const uint8_t *public_key,
					    const uint8_t *payload,
					    size_t payload_len)
{
	test_anon_data_response_publish_route(public_key, payload, payload_len,
					      MBS_MESHCORE_ROUTE_FLOOD, 0U);
}

static void test_response_fragment_ack_publish(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const struct test_session *session, const struct test_header *response_hdr);

static void test_anon_read_data_response(
	const uint8_t *public_key, const struct test_session *session,
	uint8_t *plain, size_t *plain_len)
{
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header hdr;

	zassert_true(test_wait_anon_data_request(&tx),
		     "anon DATA_RESPONSE was not published");
	zassert_mem_equal(tx.public_key, public_key, MBS_MESHCORE_PUBLIC_KEY_SIZE,
			  "anon DATA_RESPONSE target mismatch");
	zassert_equal(tx.delay_ms, CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "anon DATA_RESPONSE turn-around delay mismatch");
	test_frame_header_parse(tx.payload, tx.payload_len, &hdr);
	zassert_equal(hdr.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_mem_equal(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_aead_decrypt(session->key, tx.payload, tx.payload_len, plain, plain_len);
	test_response_fragment_ack_publish(public_key, session, &hdr);
}

static void test_anon_endpoint_receive_from(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *frame, size_t frame_len)
{
	test_anon_data_response_publish(public_key, frame, frame_len);
}

static void test_anon_endpoint_receive(const uint8_t *frame, size_t frame_len)
{
	test_anon_endpoint_receive_from(test_public_key, frame, frame_len);
}

static void test_session_start_from_route(
	struct test_session *session, const char *secret,
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	uint8_t client_nonce_seed, const uint8_t *return_path,
	uint8_t return_path_byte_len, uint8_t return_path_hash_size,
	uint8_t inbound_route)
{
	uint8_t session_init[MBS_MANAGEMENT_FRAME_MAX_LEN];
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT,
		.session_id = 0U,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = MBS_MANAGEMENT_NONCE_SIZE +
			       MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE,
	};
	struct test_header session_challenge;

	memset(session, 0, sizeof(*session));
	if (return_path_hash_size != 0U) {
		session->return_route_present = true;
		session->return_path_byte_len = return_path_byte_len;
		session->return_path_hash_size = return_path_hash_size;
		if (return_path_byte_len > 0U) {
			zassert_not_null(return_path);
			memcpy(session->return_path, return_path,
			       return_path_byte_len);
		}
	}
	hdr.payload_len += return_path_byte_len;
	test_target_id_derive(session->target_id);
	memcpy(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	for (size_t i = 0; i < sizeof(session->client_nonce); i++) {
		session->client_nonce[i] = client_nonce_seed + i;
	}

	test_frame_header_write(session_init, &hdr);
	memcpy(&session_init[MBS_MANAGEMENT_HEADER_SIZE], session->client_nonce,
	       sizeof(session->client_nonce));
	memset(&session_init[MBS_MANAGEMENT_HEADER_SIZE +
			     MBS_MANAGEMENT_NONCE_SIZE],
	       0, MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE);
	if (session->return_route_present) {
		size_t route_off = MBS_MANAGEMENT_HEADER_SIZE +
				   MBS_MANAGEMENT_NONCE_SIZE;

		session_init[route_off] =
			MBS_MANAGEMENT_SESSION_ROUTE_PRESENT;
		session_init[route_off + 1U] = return_path_hash_size;
		session_init[route_off + 2U] = return_path_byte_len;
		if (return_path_byte_len > 0U) {
			memcpy(&session_init[route_off +
					     MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE],
			       return_path, return_path_byte_len);
		}
	}

	test_anon_data_request_drain();
	test_anon_data_response_publish_route(public_key, session_init,
		MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len,
		inbound_route, 0U);
	zassert_true(test_wait_anon_data_request(&tx),
		     "anon SESSION_CHALLENGE was not published");
	zassert_mem_equal(tx.public_key, public_key,
			  MBS_MESHCORE_PUBLIC_KEY_SIZE,
			  "SESSION_CHALLENGE target mismatch");
	zassert_equal(tx.delay_ms, CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "SESSION_CHALLENGE turn-around delay mismatch");

	test_frame_header_parse(tx.payload, tx.payload_len, &session_challenge);
	zassert_equal(session_challenge.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE);
	zassert_mem_equal(session_challenge.target_id, session->target_id,
			  sizeof(session->target_id));
	zassert_not_equal(session_challenge.session_id, 0U);
	zassert_equal(session_challenge.payload_len,
		      2U * MBS_MANAGEMENT_NONCE_SIZE);
	zassert_mem_equal(&tx.payload[MBS_MANAGEMENT_HEADER_SIZE],
			  session->client_nonce, sizeof(session->client_nonce));

	session->session_id = session_challenge.session_id;
	memcpy(session->server_nonce,
	       &tx.payload[MBS_MANAGEMENT_HEADER_SIZE +
			   MBS_MANAGEMENT_NONCE_SIZE],
	       sizeof(session->server_nonce));
	test_key_derive(secret, session, session->key);
}

static void test_session_start_from(
	struct test_session *session, const char *secret,
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	uint8_t client_nonce_seed)
{
	test_session_start_from_route(
		session, secret, public_key, client_nonce_seed, NULL, 0U, 0U,
		MBS_MESHCORE_ROUTE_FLOOD);
}

static void test_session_start(struct test_session *session, const char *secret)
{
	test_session_start_from(session, secret, test_public_key, 0x80U);
}

static void test_smp_build(uint16_t group, uint8_t id, uint8_t op,
			   const uint8_t *payload, size_t payload_len,
			   uint8_t *out, size_t *out_len)
{
	out[0] = op;
	out[1] = 0U;
	sys_put_be16(payload_len, &out[2]);
	sys_put_be16(group, &out[4]);
	out[6] = 7U;
	out[7] = id;
	memcpy(&out[8], payload, payload_len);
	*out_len = 8U + payload_len;
}

static void test_smp_request_build(mbs_management_smp_request_event *request,
				   const uint8_t *contact_prefix, uint16_t group,
				   uint8_t id, uint8_t op, const uint8_t *payload,
				   size_t payload_len)
{
	size_t smp_len = 0U;

	zassert_not_null(request);
	zassert_not_null(contact_prefix);
	*request = (mbs_management_smp_request_event){0};
	memcpy(request->contact_prefix, contact_prefix, sizeof(request->contact_prefix));
	test_smp_build(group, id, op, payload, payload_len, request->packet,
		       &smp_len);
	zassert_true(smp_len <= UINT16_MAX);
	request->packet_len = (uint16_t)smp_len;
}

static void test_cbor_data_envelope_build(const uint8_t *proto, size_t proto_len,
					  uint8_t *out, size_t *out_len)
{
	size_t pos = 0U;

	zassert_true(proto != NULL || proto_len == 0U);
	zassert_true(proto_len <= 255U);

	out[pos++] = 0xa1U;
	out[pos++] = 0x64U;
	out[pos++] = 'd';
	out[pos++] = 'a';
	out[pos++] = 't';
	out[pos++] = 'a';
	if (proto_len <= 23U) {
		out[pos++] = 0x40U | (uint8_t)proto_len;
	} else {
		out[pos++] = 0x58U;
		out[pos++] = (uint8_t)proto_len;
	}
	if (proto_len > 0U) {
		memcpy(&out[pos], proto, proto_len);
		pos += proto_len;
	}
	*out_len = pos;
}

static void test_management_secret_set_envelope_build(const char *secret,
						      uint8_t *out,
						      size_t *out_len)
{
	uint8_t proto[2U + MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len = strlen(secret);
	size_t proto_len = 0U;

	zassert_true(secret_len <= MBS_MANAGEMENT_SECRET_MAX_LEN);
	proto[proto_len++] = 0x0aU;
	proto[proto_len++] = (uint8_t)secret_len;
	memcpy(&proto[proto_len], secret, secret_len);
	proto_len += secret_len;

	test_cbor_data_envelope_build(proto, proto_len, out, out_len);
	secure_wipe(proto, sizeof(proto));
}

static void test_empty_mbs_envelope_build(uint8_t *out, size_t *out_len)
{
	test_cbor_data_envelope_build(NULL, 0U, out, out_len);
}

static size_t test_data_frame_build(const struct test_session *session, uint32_t seq,
				    uint8_t frag_index, uint8_t frag_count,
				    const uint8_t *plain, size_t plain_len,
				    uint8_t *frame)
{
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST,
		.session_id = session->session_id,
		.seq = seq,
		.frag_index = frag_index,
		.frag_count = frag_count,
		.payload_len = plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE,
	};

	memcpy(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	test_aead_encrypt(session->key, frame, plain, plain_len);
	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static size_t test_session_init_frame_build(const uint8_t *target_id, uint8_t *frame)
{
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT,
		.session_id = 0U,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = MBS_MANAGEMENT_NONCE_SIZE +
			       MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE,
	};

	zassert_not_null(target_id);
	memcpy(hdr.target_id, target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	for (size_t i = 0; i < MBS_MANAGEMENT_NONCE_SIZE; i++) {
		frame[MBS_MANAGEMENT_HEADER_SIZE + i] = 0x40U + i;
	}
	memset(&frame[MBS_MANAGEMENT_HEADER_SIZE +
		      MBS_MANAGEMENT_NONCE_SIZE],
	       0, MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE);

	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static size_t test_smp_session_challenge_frame_build(
	const struct test_header *session_init, const uint8_t *client_nonce,
	const char *secret, struct test_session *session, uint8_t *frame)
{
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE,
		.session_id = 0x12345678U,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = 2U * MBS_MANAGEMENT_NONCE_SIZE,
	};

	zassert_not_null(session_init);
	zassert_not_null(client_nonce);
	zassert_not_null(secret);
	zassert_not_null(session);
	zassert_not_null(frame);

	memset(session, 0, sizeof(*session));
	memcpy(session->target_id, session_init->target_id, sizeof(session->target_id));
	memcpy(session->client_nonce, client_nonce, sizeof(session->client_nonce));
	zassert_true(session_init->payload_len >=
		     MBS_MANAGEMENT_NONCE_SIZE +
			     MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE);
	{
		const uint8_t *route =
			&client_nonce[MBS_MANAGEMENT_NONCE_SIZE];

		session->return_route_present =
			(route[0] & MBS_MANAGEMENT_SESSION_ROUTE_PRESENT) != 0U;
		session->return_path_hash_size = route[1];
		session->return_path_byte_len = route[2];
		zassert_equal(session_init->payload_len,
			      MBS_MANAGEMENT_NONCE_SIZE +
				      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE +
				      session->return_path_byte_len);
		zassert_true(session->return_path_byte_len <=
			     sizeof(session->return_path));
		if (session->return_path_byte_len > 0U) {
			memcpy(session->return_path,
			       &route[MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE],
			       session->return_path_byte_len);
		}
	}
	session->session_id = hdr.session_id;
	for (size_t i = 0; i < sizeof(session->server_nonce); i++) {
		session->server_nonce[i] = 0xd0U + i;
	}

	memcpy(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE], session->client_nonce,
	       sizeof(session->client_nonce));
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE +
		      MBS_MANAGEMENT_NONCE_SIZE],
	       session->server_nonce, sizeof(session->server_nonce));
	test_key_derive(secret, session, session->key);

	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static size_t test_session_reject_frame_build(
	const struct test_header *data_request, int32_t status, uint8_t *frame)
{
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = sizeof(uint32_t),
	};

	zassert_not_null(data_request);
	zassert_not_null(frame);
	zassert_equal(data_request->type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_true(status < 0);

	hdr.session_id = data_request->session_id;
	memcpy(hdr.target_id, data_request->target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	sys_put_le32((uint32_t)status,
		     &frame[MBS_MANAGEMENT_HEADER_SIZE]);

	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static size_t test_data_response_fragment_build(
	const struct test_session *session, uint32_t seq, uint8_t frag_index,
	uint8_t frag_count, const uint8_t *plain, size_t plain_len, uint8_t *frame)
{
	struct test_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE,
		.session_id = session->session_id,
		.seq = seq,
		.frag_index = frag_index,
		.frag_count = frag_count,
		.payload_len = plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE,
	};

	memcpy(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	test_aead_encrypt(session->key, frame, plain, plain_len);
	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static size_t test_data_response_frame_build(
	const struct test_session *session, uint32_t seq, const uint8_t *plain,
	size_t plain_len, uint8_t *frame)
{
	return test_data_response_fragment_build(session, seq, 0U, 1U, plain,
						 plain_len, frame);
}

static size_t test_ack_frame_build(
	const struct test_session *session, uint8_t type, uint32_t seq,
	uint8_t frag_index, uint8_t frag_count, uint8_t *frame)
{
	struct test_header hdr = {
		.type = type,
		.session_id = session->session_id,
		.seq = seq,
		.frag_index = frag_index,
		.frag_count = frag_count,
		.payload_len = 1U + MBS_MANAGEMENT_AEAD_TAG_SIZE,
	};
	uint8_t plain = frag_index;

	zassert_true(type == MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK ||
		     type == MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK);
	memcpy(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_frame_header_write(frame, &hdr);
	test_aead_encrypt(session->key, frame, &plain, sizeof(plain));
	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static void test_request_fragment_ack_publish(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const struct test_session *session, const struct test_header *request_hdr)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + 1U +
		      MBS_MANAGEMENT_AEAD_TAG_SIZE];
	size_t frame_len;

	frame_len = test_ack_frame_build(
		session, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK,
		request_hdr->seq, request_hdr->frag_index,
		request_hdr->frag_count, frame);
	test_anon_data_response_publish(public_key, frame, frame_len);
}

static void test_response_fragment_ack_publish(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const struct test_session *session, const struct test_header *response_hdr)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + 1U +
		      MBS_MANAGEMENT_AEAD_TAG_SIZE];
	size_t frame_len;

	frame_len = test_ack_frame_build(
		session, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK,
		response_hdr->seq, response_hdr->frag_index,
		response_hdr->frag_count, frame);
	test_anon_data_response_publish(public_key, frame, frame_len);
}

static void test_expect_fragment_ack(
	const struct test_session *session, uint8_t type, uint32_t seq,
	uint8_t frag_index, uint8_t frag_count)
{
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header hdr;
	uint8_t plain[1];
	size_t plain_len = 0U;

	zassert_true(test_wait_anon_data_request(&tx),
		     "request fragment ACK was not published");
	test_frame_header_parse(tx.payload, tx.payload_len, &hdr);
	zassert_equal(hdr.type, type);
	zassert_equal(hdr.session_id, session->session_id);
	zassert_equal(hdr.seq, seq);
	zassert_equal(hdr.frag_index, frag_index);
	zassert_equal(hdr.frag_count, frag_count);
	zassert_equal(tx.delay_ms, CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "fragment ACK turn-around delay mismatch");
	test_aead_decrypt(session->key, tx.payload, tx.payload_len, plain,
			  &plain_len);
	zassert_equal(plain_len, sizeof(plain));
	zassert_equal(plain[0], frag_index);
}

static void test_expect_request_fragment_ack(
	const struct test_session *session, uint32_t seq, uint8_t frag_index,
	uint8_t frag_count)
{
	test_expect_fragment_ack(
		session, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK, seq,
		frag_index, frag_count);
}

static void test_expect_response_fragment_ack(
	const struct test_session *session, uint32_t seq, uint8_t frag_index,
	uint8_t frag_count)
{
	test_expect_fragment_ack(
		session, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK, seq,
		frag_index, frag_count);
}

static void test_read_response(const struct test_session *session, uint8_t *plain,
			       size_t *plain_len)
{
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header hdr;

	zassert_true(test_wait_anon_data_request(&tx),
		     "anon DATA_RESPONSE was not published");
	zassert_equal(tx.delay_ms, CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "anon DATA_RESPONSE turn-around delay mismatch");
	test_frame_header_parse(tx.payload, tx.payload_len, &hdr);
	zassert_equal(hdr.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_mem_equal(hdr.target_id, session->target_id, sizeof(hdr.target_id));
	test_aead_decrypt(session->key, tx.payload, tx.payload_len, plain, plain_len);
	test_response_fragment_ack_publish(test_public_key, session, &hdr);
}

static bool bytes_contains(const uint8_t *buf, size_t len, const uint8_t *needle,
			   size_t needle_len)
{
	if (buf == NULL || needle == NULL || needle_len == 0U || len < needle_len) {
		return false;
	}

	for (size_t i = 0; i <= len - needle_len; i++) {
		if (memcmp(&buf[i], needle, needle_len) == 0) {
			return true;
		}
	}

	return false;
}

ZTEST(mbs_management_contract,
	  test_firmware_fragment_ack_uses_authenticated_return_route)
{
	static const uint8_t return_path[] = {0x31U, 0x42U};
	static const uint8_t cbor_empty_map[] = {0xa0U};
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_session session;
	struct test_header ack_hdr;
	uint8_t smp[16];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t plain[1];
	size_t smp_len = 0U;
	size_t frame_len;
	size_t plain_len = 0U;

	test_identity_prepare();
	test_session_start_from_route(
		&session, TEST_SECRET, test_public_key, 0x90U, return_path,
		sizeof(return_path), 1U, MBS_MESHCORE_ROUTE_DIRECT);
	test_smp_build(
		meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE,
		meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS,
		MGMT_OP_READ, cbor_empty_map, sizeof(cbor_empty_map), smp,
		&smp_len);
	zassert_equal(smp_len, MGMT_HDR_SIZE + sizeof(cbor_empty_map));
	frame_len = test_data_frame_build(&session, 1U, 0U, 2U, smp,
					   MGMT_HDR_SIZE, frame);

	test_anon_data_request_drain();
	test_anon_data_response_publish_route(
		test_public_key, frame, frame_len, MBS_MESHCORE_ROUTE_DIRECT,
		0U);
	zassert_true(test_wait_anon_data_request(&tx),
		     "Firmware fragment ACK was not published");
	zassert_true(tx.direct_only);
	zassert_true(tx.has_explicit_path);
	zassert_equal(tx.path_hash_size, 1U);
	zassert_equal(tx.path_byte_len, sizeof(return_path));
	zassert_mem_equal(tx.path, return_path, sizeof(return_path));
	test_frame_header_parse(tx.payload, tx.payload_len, &ack_hdr);
	zassert_equal(ack_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK);
	zassert_equal(ack_hdr.session_id, session.session_id);
	zassert_equal(ack_hdr.seq, 1U);
	zassert_equal(ack_hdr.frag_index, 0U);
	zassert_equal(ack_hdr.frag_count, 2U);
	test_aead_decrypt(session.key, tx.payload, tx.payload_len, plain,
			  &plain_len);
	zassert_equal(plain_len, sizeof(plain));
	zassert_equal(plain[0], 0U);
}

ZTEST(mbs_management_contract,
	  test_firmware_operator_binds_reverse_route_and_sends_explicit)
{
	static const uint8_t forward_path[] = {0x21U, 0x32U, 0x43U};
	static const uint8_t reverse_path[] = {0x43U, 0x32U, 0x21U};
	static const uint8_t cbor_empty_map[] = {0xa0U};
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	struct mbs_meshcore_anon_data_send_request_event session_init = {0};
	struct mbs_meshcore_anon_data_send_request_event data_request = {0};
	mbs_management_smp_response_event smp_response = {0};
	struct test_header session_init_hdr;
	struct test_header data_hdr;
	struct test_session session;
	uint8_t challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t response[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t response_smp[16];
	uint8_t request_plain[16];
	size_t challenge_len;
	size_t response_len;
	size_t response_smp_len = 0U;
	size_t request_plain_len = 0U;
	size_t route_off;
	uint32_t tag = 0U;

	test_contact_prepare(&contact, 0xe4U, true);
	contact.path_hash_size = 1U;
	contact.out_path.size = sizeof(forward_path);
	memcpy(contact.out_path.bytes, forward_path, sizeof(forward_path));
	test_smp_request_build(
		&request, contact.public_key.bytes,
		meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE,
		meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS,
		MGMT_OP_READ, cbor_empty_map, sizeof(cbor_empty_map));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));
	zassert_ok(mbs_management_smp_request(&request, &tag));

	zassert_true(test_wait_anon_data_request(&session_init));
	test_frame_header_parse(session_init.payload, session_init.payload_len,
				&session_init_hdr);
	zassert_equal(session_init_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	route_off = MBS_MANAGEMENT_HEADER_SIZE +
		    MBS_MANAGEMENT_NONCE_SIZE;
	zassert_equal(session_init.payload[route_off],
		      MBS_MANAGEMENT_SESSION_ROUTE_PRESENT);
	zassert_equal(session_init.payload[route_off + 1U], 1U);
	zassert_equal(session_init.payload[route_off + 2U],
		      sizeof(reverse_path));
	zassert_mem_equal(
		&session_init.payload[route_off +
				      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE],
		reverse_path, sizeof(reverse_path));

	challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr,
		&session_init.payload[MBS_MANAGEMENT_HEADER_SIZE], TEST_SECRET,
		&session, challenge);
	test_anon_data_response_publish(contact.public_key.bytes, challenge,
					challenge_len);
	zassert_true(test_wait_anon_data_request(&data_request));
	zassert_true(data_request.direct_only);
	zassert_true(data_request.has_explicit_path);
	zassert_equal(data_request.path_hash_size, 1U);
	zassert_equal(data_request.path_byte_len, sizeof(forward_path));
	zassert_mem_equal(data_request.path, forward_path, sizeof(forward_path));
	test_frame_header_parse(data_request.payload, data_request.payload_len,
				&data_hdr);
	test_aead_decrypt(session.key, data_request.payload,
			  data_request.payload_len, request_plain,
			  &request_plain_len);
	zassert_equal(sys_get_be16(&request_plain[4]),
		      meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE);

	test_smp_build(
		meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE,
		meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS,
		MGMT_OP_READ_RSP, cbor_empty_map, sizeof(cbor_empty_map),
		response_smp, &response_smp_len);
	response_len = test_data_response_frame_build(
		&session, data_hdr.seq, response_smp, response_smp_len, response);
	test_anon_data_response_publish_route(
		contact.public_key.bytes, response, response_len,
		MBS_MESHCORE_ROUTE_DIRECT, 0U);
	zassert_true(test_wait_smp_response(&smp_response));
	zassert_equal(smp_response.tag, tag);
	zassert_equal(smp_response.status, 0);
	zassert_equal(smp_response.response_len, response_smp_len);
	zassert_mem_equal(smp_response.response, response_smp, response_smp_len);
}

ZTEST(mbs_management_contract,
	  test_firmware_request_fails_closed_on_flood_route)
{
	static const uint8_t return_path[] = {0x51U, 0x62U};
	static const uint8_t cbor_empty_map[] = {0xa0U};
	struct mbs_meshcore_anon_data_send_request_event reject = {0};
	struct test_session session;
	struct test_header reject_hdr;
	uint8_t smp[16];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t smp_len = 0U;
	size_t frame_len;

	test_identity_prepare();
	test_session_start_from_route(
		&session, TEST_SECRET, test_public_key, 0xa0U, return_path,
		sizeof(return_path), 1U, MBS_MESHCORE_ROUTE_DIRECT);
	test_smp_build(
		meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE,
		meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS,
		MGMT_OP_READ, cbor_empty_map, sizeof(cbor_empty_map), smp,
		&smp_len);
	frame_len = test_data_frame_build(&session, 1U, 0U, 2U, smp,
					   MGMT_HDR_SIZE, frame);

	test_anon_data_request_drain();
	test_anon_data_response_publish_route(
		test_public_key, frame, frame_len, MBS_MESHCORE_ROUTE_FLOOD,
		0U);
	zassert_true(test_wait_anon_data_request(&reject),
		     "Firmware flood rejection was not published");
	zassert_true(reject.direct_only);
	zassert_true(reject.has_explicit_path);
	zassert_equal(reject.path_hash_size, 1U);
	zassert_equal(reject.path_byte_len, sizeof(return_path));
	zassert_mem_equal(reject.path, return_path, sizeof(return_path));
	test_frame_header_parse(reject.payload, reject.payload_len, &reject_hdr);
	zassert_equal(reject_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT);
	zassert_equal(reject_hdr.session_id, session.session_id);
	zassert_equal((int32_t)sys_get_le32(
		      &reject.payload[MBS_MANAGEMENT_HEADER_SIZE]),
		      -EHOSTUNREACH);
}

ZTEST(mbs_management_contract, test_echo_request_round_trip)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	const uint8_t expected_echo_pair[] = {0x61, 0x72, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);

	frame_len = test_data_frame_build(&session, 1U, 0U, 1U, smp, smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, rsp, &rsp_len);

	zassert_true(rsp_len >= 8U + sizeof(echo_payload));
	zassert_equal(rsp[0] & 0x07U, MGMT_OP_WRITE_RSP);
	zassert_equal(sys_get_be16(&rsp[4]), MGMT_GROUP_ID_OS);
	zassert_equal(rsp[7], 0U);
	zassert_true(bytes_contains(&rsp[8], rsp_len - 8U, expected_echo_pair,
				    sizeof(expected_echo_pair)));
}

ZTEST(mbs_management_contract, test_completed_response_keeps_session_reusable)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t smp_len = 0U;
	size_t frame_len;
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_ECHO, MGMT_OP_WRITE,
		       echo_payload, sizeof(echo_payload), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 1U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE);

	rsp_len = 0U;
	frame_len = test_data_frame_build(&session, 2U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE);
}

ZTEST(mbs_management_contract, test_authentication_failure_rejects_before_smp)
{
	struct test_session session;
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header challenge;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t replacement_init[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;
	size_t replacement_init_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 1U, 0U, 1U, smp, smp_len, frame);
	frame[frame_len - 1U] ^= 0x40U;

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_session_reject(session.session_id, -EACCES);

	replacement_init_len =
		test_session_init_frame_build(session.target_id, replacement_init);
	test_anon_endpoint_receive(replacement_init, replacement_init_len);
	zassert_true(test_wait_anon_data_request(&tx),
		     "failed authentication must not establish the pending session");
	test_frame_header_parse(tx.payload, tx.payload_len, &challenge);
	zassert_equal(challenge.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE);
	zassert_not_equal(challenge.session_id, session.session_id);
}

ZTEST(mbs_management_contract, test_replay_resends_cached_response)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 1U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, smp, &smp_len);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, smp, &smp_len);
	zassert_true(smp_len >= MGMT_HDR_SIZE);
}

ZTEST(mbs_management_contract, test_fragmented_replay_waits_for_last_fragment)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	uint8_t frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t smp_len = 0U;
	size_t frame0_len;
	size_t frame1_len;
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_ECHO, MGMT_OP_WRITE,
		       echo_payload, sizeof(echo_payload), smp, &smp_len);
	frame0_len = test_data_frame_build(&session, 1U, 0U, 2U, smp,
				   MGMT_HDR_SIZE,
				   frame0);
	frame1_len = test_data_frame_build(&session, 1U, 1U, 2U,
				   &smp[MGMT_HDR_SIZE],
				   smp_len - MGMT_HDR_SIZE, frame1);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame0, frame0_len);
	test_expect_request_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_endpoint_receive(frame1, frame1_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame0, frame0_len);
	test_expect_request_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_endpoint_receive(frame1, frame1_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE);
}

ZTEST(mbs_management_contract, test_oversize_response_rejects_promptly)
{
	struct test_session session;
	uint8_t echo_payload[TEST_SMP_EFFECTIVE_MAX_LEN - MGMT_HDR_SIZE];
	uint8_t smp[TEST_SMP_EFFECTIVE_MAX_LEN];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t echo_data_len = sizeof(echo_payload) - 6U;
	size_t smp_len = 0U;
	size_t offset = 0U;
	uint8_t frag_count;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);

	/* A fixed-map request at the 608-byte SMP limit gains one byte when the
	 * OS echo response is encoded as an indefinite map. The endpoint must
	 * report the 609-byte response overflow instead of turning the retry into
	 * an unrelated stale-session error.
	 */
	echo_payload[0] = 0xa1U;
	echo_payload[1] = 0x61U;
	echo_payload[2] = 'd';
	echo_payload[3] = 0x79U;
	sys_put_be16(echo_data_len, &echo_payload[4]);
	memset(&echo_payload[6], 'a', echo_data_len);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_ECHO, MGMT_OP_WRITE,
		       echo_payload, sizeof(echo_payload), smp, &smp_len);
	zassert_equal(smp_len, TEST_SMP_EFFECTIVE_MAX_LEN);

	frag_count = DIV_ROUND_UP(smp_len, TEST_PLAINTEXT_MAX_LEN);
	zassert_true(frag_count <= CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS);
	test_anon_data_request_drain();
	for (uint8_t frag_index = 0U; frag_index < frag_count; frag_index++) {
		size_t frag_len = MIN(smp_len - offset,
				      (size_t)TEST_PLAINTEXT_MAX_LEN);
		size_t frame_len = test_data_frame_build(
			&session, 1U, frag_index, frag_count, &smp[offset],
			frag_len, frame);

		test_anon_endpoint_receive(frame, frame_len);
		if (frag_index + 1U < frag_count) {
			test_expect_request_fragment_ack(
				&session, 1U, frag_index, frag_count);
		}
		offset += frag_len;
	}

	test_expect_session_reject(session.session_id, -EMSGSIZE);
}

ZTEST(mbs_management_contract, test_fragmented_request_reassembles_once)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame0_len;
	size_t frame1_len;
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);

	frame0_len = test_data_frame_build(&session, 2U, 0U, 2U, smp,
				   MGMT_HDR_SIZE, frame0);
	frame1_len = test_data_frame_build(&session, 2U, 1U, 2U,
				   &smp[MGMT_HDR_SIZE],
				   smp_len - MGMT_HDR_SIZE, frame1);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame0, frame0_len);
	test_expect_request_fragment_ack(&session, 2U, 0U, 2U);
	test_anon_endpoint_receive(frame1, frame1_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= 8U);
	zassert_equal(rsp[0] & 0x07U, MGMT_OP_WRITE_RSP);
}

ZTEST(mbs_management_contract, test_fragmented_response_waits_for_ack)
{
	struct test_session session;
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header response_hdr;
	uint8_t echo_payload[101U];
	uint8_t smp[128U];
	uint8_t request_frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t request_frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t ack_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t response[128U];
	size_t smp_len = 0U;
	size_t request_frame0_len;
	size_t request_frame1_len;
	size_t ack_frame_len;
	size_t response_len = 0U;
	size_t plain_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	echo_payload[0] = 0xa1U;
	echo_payload[1] = 0x61U;
	echo_payload[2] = 'd';
	echo_payload[3] = 0x78U;
	echo_payload[4] = sizeof(echo_payload) - 5U;
	memset(&echo_payload[5], 'x', sizeof(echo_payload) - 5U);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_ECHO, MGMT_OP_WRITE,
		       echo_payload, sizeof(echo_payload), smp, &smp_len);
	zassert_true(smp_len > TEST_PLAINTEXT_MAX_LEN);

	request_frame0_len = test_data_frame_build(
		&session, 1U, 0U, 2U, smp, TEST_PLAINTEXT_MAX_LEN,
		request_frame0);
	request_frame1_len = test_data_frame_build(
		&session, 1U, 1U, 2U, &smp[TEST_PLAINTEXT_MAX_LEN],
		smp_len - TEST_PLAINTEXT_MAX_LEN, request_frame1);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(request_frame0, request_frame0_len);
	test_expect_request_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_endpoint_receive(request_frame1, request_frame1_len);

	zassert_true(test_wait_anon_data_request(&tx),
		     "first DATA_RESPONSE fragment was not published");
	test_frame_header_parse(tx.payload, tx.payload_len, &response_hdr);
	zassert_equal(response_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_equal(response_hdr.frag_index, 0U);
	zassert_equal(response_hdr.frag_count, 2U);
	test_aead_decrypt(session.key, tx.payload, tx.payload_len, response,
			  &plain_len);
	response_len += plain_len;
	test_expect_no_anon_data_request();

	ack_frame_len = test_ack_frame_build(
		&session, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK,
		response_hdr.seq, response_hdr.frag_index,
		response_hdr.frag_count, ack_frame);
	test_anon_endpoint_receive(ack_frame, ack_frame_len);
	zassert_true(test_wait_anon_data_request(&tx),
		     "second DATA_RESPONSE fragment was not published");
	test_frame_header_parse(tx.payload, tx.payload_len, &response_hdr);
	zassert_equal(response_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_equal(response_hdr.frag_index, 1U);
	zassert_equal(response_hdr.frag_count, 2U);
	test_aead_decrypt(session.key, tx.payload, tx.payload_len,
			  &response[response_len], &plain_len);
	response_len += plain_len;
	zassert_true(response_len > TEST_PLAINTEXT_MAX_LEN);
	zassert_equal(response[0] & 0x07U, MGMT_OP_WRITE_RSP);
	zassert_equal(sys_get_be16(&response[4]), MGMT_GROUP_ID_OS);
	zassert_equal(response[7], OS_MGMT_ID_ECHO);
	test_response_fragment_ack_publish(test_public_key, &session,
					   &response_hdr);
}

ZTEST(mbs_management_contract,
      test_replayed_request_restarts_response_waiting_for_ack)
{
	struct test_session session;
	struct mbs_meshcore_anon_data_send_request_event first_response = {0};
	struct mbs_meshcore_anon_data_send_request_event replayed_response = {0};
	struct mbs_meshcore_anon_data_send_request_event final_response = {0};
	struct test_header response_hdr;
	uint8_t echo_payload[101U];
	uint8_t smp[128U];
	uint8_t request_frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t request_frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t ack_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t smp_len = 0U;
	size_t request_frame0_len;
	size_t request_frame1_len;
	size_t ack_frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	echo_payload[0] = 0xa1U;
	echo_payload[1] = 0x61U;
	echo_payload[2] = 'd';
	echo_payload[3] = 0x78U;
	echo_payload[4] = sizeof(echo_payload) - 5U;
	memset(&echo_payload[5], 'r', sizeof(echo_payload) - 5U);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_ECHO, MGMT_OP_WRITE,
		       echo_payload, sizeof(echo_payload), smp, &smp_len);
	zassert_true(smp_len > TEST_PLAINTEXT_MAX_LEN);

	request_frame0_len = test_data_frame_build(
		&session, 1U, 0U, 2U, smp, TEST_PLAINTEXT_MAX_LEN,
		request_frame0);
	request_frame1_len = test_data_frame_build(
		&session, 1U, 1U, 2U, &smp[TEST_PLAINTEXT_MAX_LEN],
		smp_len - TEST_PLAINTEXT_MAX_LEN, request_frame1);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(request_frame0, request_frame0_len);
	test_expect_request_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_endpoint_receive(request_frame1, request_frame1_len);
	zassert_true(test_wait_anon_data_request(&first_response),
		     "first DATA_RESPONSE fragment was not published");
	test_frame_header_parse(first_response.payload, first_response.payload_len,
				&response_hdr);
	zassert_equal(response_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_equal(response_hdr.frag_index, 0U);
	zassert_equal(response_hdr.frag_count, 2U);

	/* Drop the first response ACK, then replay the complete request sequence. */
	test_anon_endpoint_receive(request_frame0, request_frame0_len);
	test_expect_request_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_endpoint_receive(request_frame1, request_frame1_len);
	zassert_true(test_wait_anon_data_request(&replayed_response),
		     "cached response did not restart after request replay");
	test_frame_header_parse(replayed_response.payload,
				replayed_response.payload_len, &response_hdr);
	zassert_equal(response_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_equal(response_hdr.frag_index, 0U);
	zassert_equal(response_hdr.frag_count, 2U);
	zassert_equal(replayed_response.payload_len, first_response.payload_len);
	zassert_mem_equal(replayed_response.payload, first_response.payload,
			  first_response.payload_len,
			  "cached response replay changed the first fragment");

	ack_frame_len = test_ack_frame_build(
		&session, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK,
		response_hdr.seq, response_hdr.frag_index,
		response_hdr.frag_count, ack_frame);
	test_anon_endpoint_receive(ack_frame, ack_frame_len);
	zassert_true(test_wait_anon_data_request(&final_response),
		     "cached response replay did not finish");
	test_frame_header_parse(final_response.payload, final_response.payload_len,
				&response_hdr);
	zassert_equal(response_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE);
	zassert_equal(response_hdr.frag_index, 1U);
	zassert_equal(response_hdr.frag_count, 2U);
	test_response_fragment_ack_publish(test_public_key, &session,
					   &response_hdr);
}

ZTEST(mbs_management_contract, test_unauthenticated_init_cannot_replace_established_session)
{
	struct test_session session;
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	uint8_t frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t hostile_init[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t smp_len = 0U;
	size_t frame0_len;
	size_t frame1_len;
	size_t hostile_init_len;
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	frame0_len = test_data_frame_build(&session, 2U, 0U, 2U, smp,
				   MGMT_HDR_SIZE, frame0);
	frame1_len = test_data_frame_build(&session, 2U, 1U, 2U,
				   &smp[MGMT_HDR_SIZE],
				   smp_len - MGMT_HDR_SIZE, frame1);
	hostile_init_len =
		test_session_init_frame_build(session.target_id, hostile_init);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame0, frame0_len);
	test_expect_request_fragment_ack(&session, 2U, 0U, 2U);
	test_anon_endpoint_receive(hostile_init, hostile_init_len);
	zassert_true(test_wait_anon_data_request(&tx),
		     "challenger did not receive SESSION_CHALLENGE");
	{
		struct test_header challenge;

		test_frame_header_parse(tx.payload, tx.payload_len, &challenge);
		zassert_equal(challenge.type,
			      MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE);
	}

	test_anon_endpoint_receive(frame1, frame1_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= 8U);
	zassert_equal(rsp[0] & 0x07U, MGMT_OP_WRITE_RSP);
}

ZTEST(mbs_management_contract, test_wrong_password_candidate_keeps_active_session)
{
	struct test_session active;
	struct test_session candidate;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t candidate_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t smp[32];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t smp_len = 0U;
	size_t frame_len;
	size_t rsp_len = 0U;

	for (size_t i = 0U; i < sizeof(candidate_public_key); i++) {
		candidate_public_key[i] = 0xa0U + i;
	}
	test_identity_prepare();
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);

	test_session_start_from(&active, TEST_SECRET, test_public_key, 0x80U);
	frame_len = test_data_frame_build(&active, 1U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive_from(test_public_key, frame, frame_len);
	test_anon_read_data_response(test_public_key, &active, rsp, &rsp_len);

	test_session_start_from(&candidate, "wrong-pass-1", candidate_public_key,
				0x90U);
	frame_len = test_data_frame_build(&candidate, 1U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive_from(candidate_public_key, frame, frame_len);
	test_expect_session_reject_from(candidate_public_key,
					candidate.session_id, -EACCES);

	rsp_len = 0U;
	frame_len = test_data_frame_build(&active, 2U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_endpoint_receive_from(test_public_key, frame, frame_len);
	test_anon_read_data_response(test_public_key, &active, rsp, &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE,
		     "active session did not survive wrong password");
}

ZTEST(mbs_management_contract, test_authenticated_candidate_takes_over_active_session)
{
	struct test_session previous;
	struct test_session replacement;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t replacement_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t smp[32];
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t smp_len = 0U;
	size_t frame_len;
	size_t rsp_len = 0U;

	for (size_t i = 0U; i < sizeof(replacement_public_key); i++) {
		replacement_public_key[i] = 0xb0U + i;
	}
	test_identity_prepare();
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);

	test_session_start_from(&previous, TEST_SECRET, test_public_key, 0x80U);
	frame_len = test_data_frame_build(&previous, 1U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive_from(test_public_key, frame, frame_len);
	test_anon_read_data_response(test_public_key, &previous, rsp, &rsp_len);

	test_session_start_from(&replacement, TEST_SECRET, replacement_public_key,
				0xa0U);
	frame_len = test_data_frame_build(&replacement, 1U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive_from(replacement_public_key, frame, frame_len);
	rsp_len = 0U;
	test_anon_read_data_response(replacement_public_key, &replacement, rsp,
				     &rsp_len);
	zassert_true(rsp_len >= MGMT_HDR_SIZE,
		     "replacement session did not execute first command");

	frame_len = test_data_frame_build(&previous, 2U, 0U, 1U, smp,
				  smp_len, frame);
	test_anon_endpoint_receive_from(test_public_key, frame, frame_len);
	test_expect_session_reject_from(test_public_key, previous.session_id,
					-ESTALE);
}

ZTEST(mbs_management_contract, test_pending_session_can_be_replaced)
{
	struct test_session pending;
	struct mbs_meshcore_anon_data_send_request_event tx = {0};
	struct test_header challenge;
	uint8_t replacement_init[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t replacement_init_len;

	test_identity_prepare();
	test_session_start(&pending, TEST_SECRET);
	replacement_init_len =
		test_session_init_frame_build(pending.target_id, replacement_init);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(replacement_init, replacement_init_len);
	zassert_true(test_wait_anon_data_request(&tx),
		     "pending session replacement did not produce a challenge");
	test_frame_header_parse(tx.payload, tx.payload_len, &challenge);
	zassert_equal(challenge.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE);
	zassert_not_equal(challenge.session_id, pending.session_id);
}

ZTEST(mbs_management_contract, test_authenticated_fs_command_is_rejected)
{
	struct test_session session;
	const uint8_t empty_map[] = {0xa0};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_FS, 0U, MGMT_OP_WRITE, empty_map,
		       sizeof(empty_map), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 3U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_rotated_secret_replaces_provisioned_secret)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;
	uint8_t bad_key[MBS_MANAGEMENT_SESSION_KEY_SIZE];

	test_identity_prepare();
	test_secret_set(TEST_SECRET_ROTATED);
	test_session_start(&session, TEST_SECRET_ROTATED);

	memcpy(bad_key, session.key, sizeof(bad_key));
	test_key_derive(TEST_SECRET, &session, session.key);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 4U, 0U, 1U, smp, smp_len, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_session_reject(session.session_id, -EACCES);

	test_session_start(&session, TEST_SECRET_ROTATED);
	frame_len = test_data_frame_build(&session, 4U, 0U, 1U, smp, smp_len, frame);
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, smp, &smp_len);
	secure_wipe(bad_key, sizeof(bad_key));
}

ZTEST(mbs_management_contract, test_config_set_preserves_existing_secret_when_omitted)
{
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	mbs_management_config got = meshbus_ManagementConfig_init_zero;
	size_t rotated_len = strlen(TEST_SECRET_ROTATED);

	test_identity_prepare();
	test_secret_set(TEST_SECRET_ROTATED);

	zassert_ok(mbs_management_config_set(&cfg));
	zassert_ok(mbs_management_config_get(&got));
	zassert_equal(got.secret.size, rotated_len,
		      "secret length should be preserved");
	zassert_mem_equal(got.secret.bytes, TEST_SECRET_ROTATED,
			  rotated_len, "secret bytes should be preserved");
}

ZTEST(mbs_management_contract, test_management_password_contract_boundaries)
{
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;

	cfg.secret.size = MBS_MANAGEMENT_SECRET_MIN_LEN - 1U;
	memset(cfg.secret.bytes, '1', cfg.secret.size);
	zassert_equal(mbs_management_config_set(&cfg), -EINVAL,
		      "7-character password should fail");

	cfg.secret.size = MBS_MANAGEMENT_SECRET_MIN_LEN;
	memset(cfg.secret.bytes, '1', cfg.secret.size);
	zassert_ok(mbs_management_config_set(&cfg),
		   "all-digit 8-character password should pass");

	cfg.secret.size = MBS_MANAGEMENT_SECRET_MAX_LEN;
	memset(cfg.secret.bytes, '~', cfg.secret.size);
	zassert_ok(mbs_management_config_set(&cfg),
		   "16-character printable password should pass");

	cfg.secret.bytes[0] = ' ';
	zassert_equal(mbs_management_config_set(&cfg), -EINVAL,
		      "password containing whitespace should fail");

	cfg.secret.bytes[0] = 0x7fU;
	zassert_equal(mbs_management_config_set(&cfg), -EINVAL,
		      "password containing non-printable ASCII should fail");

	cfg.secret.size = MBS_MANAGEMENT_SECRET_MAX_LEN + 1U;
	zassert_equal(mbs_management_config_set(&cfg), -EINVAL,
		      "17-character password should fail");
}

ZTEST(mbs_management_contract, test_unprovisioned_endpoint_rejects_session_init)
{
	mbs_management_config got = meshbus_ManagementConfig_init_zero;
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE +
		      MBS_MANAGEMENT_NONCE_SIZE +
		      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE];
	size_t frame_len;

	test_identity_prepare_unprovisioned();
	zassert_ok(mbs_management_config_get(&got));
	zassert_equal(got.secret.size, 0U,
		      "reset management config must remain unprovisioned");

	test_target_id_derive(target_id);
	frame_len = test_session_init_frame_build(target_id, frame);
	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_authenticated_os_reset_is_rejected)
{
	struct test_session session;
	const uint8_t empty_map[] = {0xa0};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, OS_MGMT_ID_RESET, MGMT_OP_WRITE, empty_map,
		       sizeof(empty_map), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 7U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_authenticated_storage_erase_is_rejected)
{
	struct test_session session;
	const uint8_t empty_map[] = {0xa0};
	uint8_t smp[32];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(ZEPHYR_MGMT_GRP_BASIC, ZEPHYR_MGMT_GRP_BASIC_CMD_ERASE_STORAGE,
		       MGMT_OP_WRITE, empty_map,
		       sizeof(empty_map), smp, &smp_len);
	frame_len = test_data_frame_build(&session, 8U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_authenticated_meshcore_config_read_is_rejected)
{
	struct test_session session;
	struct test_session replacement;
	uint8_t smp[MGMT_HDR_SIZE];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ, NULL, 0U, smp, &smp_len);
	frame_len = test_data_frame_build(&session, 9U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();

	test_session_start(&replacement, TEST_SECRET);
	zassert_not_equal(replacement.session_id, session.session_id,
			  "rejected complete request retained the session");
}

ZTEST(mbs_management_contract, test_allowed_command_with_trailing_packet_is_rejected)
{
	struct test_session session;
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	uint8_t smp[2U * MGMT_HDR_SIZE + sizeof(echo_payload)];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	memset(&smp[smp_len], 0, MGMT_HDR_SIZE);
	smp_len += MGMT_HDR_SIZE;
	frame_len = test_data_frame_build(&session, 10U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_remote_secret_set_rotates_session_secret)
{
	struct test_session session;
	uint8_t payload[96];
	size_t payload_len = 0U;
	uint8_t smp[128];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t rsp_len = 0U;
	mbs_management_config got = meshbus_ManagementConfig_init_zero;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_management_secret_set_envelope_build(TEST_SECRET_ROTATED,
						  payload, &payload_len);
	test_smp_build(
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		MGMT_OP_WRITE, payload, payload_len, smp, &smp_len);
	frame_len = test_data_frame_build(&session, 5U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_read_response(&session, rsp, &rsp_len);
	zassert_true(rsp_len >= 8U, "management secret response too short");
	zassert_equal(rsp[0] & 0x07U, MGMT_OP_WRITE_RSP);
	zassert_equal(sys_get_be16(&rsp[4]),
		      meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT);
	zassert_equal(rsp[7],
		      meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET);
	zassert_ok(mbs_management_config_get(&got));
	zassert_equal(got.secret.size, strlen(TEST_SECRET_ROTATED));
	zassert_mem_equal(got.secret.bytes, TEST_SECRET_ROTATED,
			  strlen(TEST_SECRET_ROTATED));

	test_session_start(&session, TEST_SECRET_ROTATED);
}

ZTEST(mbs_management_contract, test_remote_secret_read_is_rejected)
{
	struct test_session session;
	uint8_t smp[MGMT_HDR_SIZE];
	size_t smp_len = 0U;
	uint8_t frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t frame_len;

	test_identity_prepare();
	test_session_start(&session, TEST_SECRET);
	test_smp_build(
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		MGMT_OP_READ, NULL, 0U, smp, &smp_len);
	frame_len = test_data_frame_build(&session, 6U, 0U, 1U, smp, smp_len, frame);

	test_anon_data_request_drain();
	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_zero_target_session_init_is_rejected)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE +
		      MBS_MANAGEMENT_NONCE_SIZE +
		      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE];
	uint8_t zero_target_id[MBS_MANAGEMENT_TARGET_ID_SIZE] = {0};
	size_t frame_len;

	test_identity_prepare();
	test_anon_data_request_drain();
	frame_len = test_session_init_frame_build(zero_target_id, frame);

	test_anon_endpoint_receive(frame, frame_len);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_smp_missing_contact_rejects_before_send)
{
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g'};
	uint8_t prefix[MBS_MANAGEMENT_CONTACT_PREFIX_BYTES];
	uint32_t tag = 0U;
	int rc;

	memset(prefix, 0xef, sizeof(prefix));
	test_smp_request_build(&request, prefix, MGMT_GROUP_ID_OS, 0U,
			       MGMT_OP_WRITE, echo_payload, sizeof(echo_payload));
	test_anon_data_request_drain();
	(void)mbs_contact_reset(request.contact_prefix);

	rc = mbs_management_smp_request(&request, &tag);
	zassert_equal(rc, -ENOENT, "missing contact rc=%d", rc);
	zassert_equal(tag, 0U, "missing contact tag=%u", (unsigned int)tag);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_smp_effective_capacity_is_enforced_before_lookup)
{
	mbs_management_smp_request_event request = {0};
	const uint8_t transient_secret[] = TEST_SECRET;
	uint32_t tag = UINT32_MAX;
	int rc;

	memset(request.contact_prefix, 0xef, sizeof(request.contact_prefix));
	test_anon_data_request_drain();
	(void)mbs_contact_reset(request.contact_prefix);
	zassert_equal(mbs_management_smp_effective_max_len_get(),
		      TEST_SMP_EFFECTIVE_MAX_LEN, "unexpected effective capacity");

	request.packet_len = TEST_SMP_EFFECTIVE_MAX_LEN;
	rc = mbs_management_smp_request(&request, &tag);
	zassert_equal(rc, -ENOENT, "effective capacity was rejected: %d", rc);
	zassert_equal(tag, 0U, "rejected request tag=%u", (unsigned int)tag);

	request.packet_len = TEST_SMP_EFFECTIVE_MAX_LEN + 1U;
	tag = UINT32_MAX;
	rc = mbs_management_smp_request(&request, &tag);
	zassert_equal(rc, -EMSGSIZE, "oversize request rc=%d", rc);
	zassert_equal(tag, 0U, "oversize request tag=%u", (unsigned int)tag);

	tag = UINT32_MAX;
	rc = mbs_management_smp_request_with_secret(
		&request, transient_secret, sizeof(transient_secret) - 1U, &tag);
	zassert_equal(rc, -EMSGSIZE, "oversize transient request rc=%d", rc);
	zassert_equal(tag, 0U, "oversize transient tag=%u", (unsigned int)tag);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_smp_missing_contact_secret_rejects_before_send)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g'};
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xa0U, false);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_payload,
			       sizeof(echo_payload));
	test_anon_data_request_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_equal(rc, -ENOENT, "missing contact secret rc=%d", rc);
	zassert_equal(tag, 0U, "missing contact secret tag=%u", (unsigned int)tag);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_smp_request_with_secret_does_not_persist_secret)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_contact stored = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t transient_secret[] = "phase9-transient";
	const uint8_t echo_payload[] = {0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g'};
	struct mbs_meshcore_anon_data_send_request_event anon_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header hdr = {0};
	uint8_t expected_target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t invalid_response = 0U;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xa8U, false);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_payload,
			       sizeof(echo_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request_with_secret(
		&request, transient_secret, sizeof(transient_secret) - 1U, &tag);
	zassert_ok(rc, "transient SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");
	zassert_true(test_wait_anon_data_request(&anon_req),
		     "anon-data SESSION_INIT was not published");
	zassert_mem_equal(anon_req.public_key, contact.public_key.bytes,
			  MBS_CONTACT_PUBLIC_KEY_SIZE, "anon target mismatch");

	zassert_ok(mbs_contact_find_by_key(contact.public_key.bytes, &stored));
	zassert_equal(stored.management_secret.size, 0U,
		      "transient secret must not be persisted");

	test_frame_header_parse(anon_req.payload, anon_req.payload_len, &hdr);
	zassert_equal(hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	zassert_equal(hdr.payload_len,
		      MBS_MANAGEMENT_NONCE_SIZE +
			      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE);
	test_target_id_derive_from_public_key(contact.public_key.bytes,
					      expected_target_id);
	zassert_mem_equal(hdr.target_id, expected_target_id,
			  sizeof(expected_target_id), "SESSION_INIT target id mismatch");

	test_anon_data_response_publish(contact.public_key.bytes,
					&invalid_response,
					sizeof(invalid_response));
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "invalid response should complete the accepted exchange");
	zassert_equal(smp_rsp_event.tag, tag, "invalid response tag mismatch");
	zassert_equal(smp_rsp_event.status, -EINVAL,
		      "invalid response status mismatch: %d",
		      smp_rsp_event.status);
	zassert_equal(smp_rsp_event.response_len, 0U,
		      "invalid response should publish an empty completion");
}

ZTEST(mbs_management_contract, test_smp_request_uses_anon_data_session_init)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g'};
	struct mbs_meshcore_anon_data_send_request_event anon_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header hdr = {0};
	uint8_t expected_target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t invalid_response = 0U;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xb0U, true);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_payload,
			       sizeof(echo_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_ok(rc, "SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");
	zassert_true(test_wait_anon_data_request(&anon_req),
		     "anon-data SESSION_INIT was not published");
	zassert_mem_equal(anon_req.public_key, contact.public_key.bytes,
			  MBS_CONTACT_PUBLIC_KEY_SIZE, "anon target mismatch");

	test_frame_header_parse(anon_req.payload, anon_req.payload_len, &hdr);
	zassert_equal(hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	zassert_equal(hdr.session_id, 0U, "SESSION_INIT session id should be zero");
	zassert_equal(hdr.seq, 0U, "SESSION_INIT seq should be zero");
	zassert_equal(hdr.frag_index, 0U, "SESSION_INIT frag index");
	zassert_equal(hdr.frag_count, 1U, "SESSION_INIT frag count");
	zassert_equal(hdr.payload_len,
		      MBS_MANAGEMENT_NONCE_SIZE +
			      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE,
		      "SESSION_INIT nonce length");
	test_target_id_derive_from_public_key(contact.public_key.bytes,
					      expected_target_id);
	zassert_mem_equal(hdr.target_id, expected_target_id,
			  sizeof(expected_target_id), "SESSION_INIT target id mismatch");

	test_anon_data_response_publish(contact.public_key.bytes,
					&invalid_response,
					sizeof(invalid_response));
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "invalid response should complete the accepted exchange");
	zassert_equal(smp_rsp_event.tag, tag, "invalid response tag mismatch");
	zassert_equal(smp_rsp_event.status, -EINVAL,
		      "invalid response status mismatch: %d",
		      smp_rsp_event.status);
	zassert_equal(smp_rsp_event.response_len, 0U,
		      "invalid response should publish an empty completion");
}

ZTEST(mbs_management_contract, test_SMP_response_publishes_decrypted_smp)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_req_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g',
	};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_session session;
	const uint8_t echo_rsp_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'o', 'n', 'g',
	};
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t data_plain[TEST_PLAINTEXT_MAX_LEN];
	uint8_t smp_rsp[32];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	size_t data_plain_len = 0U;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xc0U, true);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_req_payload,
			       sizeof(echo_req_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_ok(rc, "SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");

	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "anon-data SESSION_INIT was not published");
	zassert_equal(session_init_req.delay_ms, 0U,
		      "SESSION_INIT should not be delayed");
	test_frame_header_parse(session_init_req.payload, session_init_req.payload_len,
				&session_init_hdr);
	zassert_equal(session_init_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	zassert_equal(session_init_hdr.payload_len,
		      MBS_MANAGEMENT_NONCE_SIZE +
			      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE);

	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr, &session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge, session_challenge_len);

	zassert_true(test_wait_anon_data_request(&data_req),
		     "anon-data DATA_REQUEST was not published");
	zassert_equal(data_req.delay_ms,
		      CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "DATA_REQUEST should allow radio turn-around");
	zassert_mem_equal(data_req.public_key, contact.public_key.bytes,
			  MBS_CONTACT_PUBLIC_KEY_SIZE, "DATA_REQUEST target");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_equal(data_hdr.session_id, session.session_id);
	zassert_equal(data_hdr.seq, 1U);
	zassert_mem_equal(data_hdr.target_id, session.target_id,
			  sizeof(session.target_id), "DATA_REQUEST target id");
	test_aead_decrypt(session.key, data_req.payload, data_req.payload_len,
			  data_plain, &data_plain_len);
	zassert_true(data_plain_len >= MGMT_HDR_SIZE, "DATA_REQUEST too short");
	zassert_equal(data_plain[0] & 0x07U, MGMT_OP_WRITE);
	zassert_equal(sys_get_be16(&data_plain[4]), MGMT_GROUP_ID_OS);
	zassert_equal(data_plain[7], 0U);
	zassert_true(bytes_contains(&data_plain[8], data_plain_len - 8U,
				    echo_req_payload, sizeof(echo_req_payload)),
		     "DATA_REQUEST should contain echo bytes");

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP,
		       echo_rsp_payload, sizeof(echo_rsp_payload),
		       smp_rsp, &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame, response_frame_len);

	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SMP response was not published");
	zassert_equal(smp_rsp_event.tag, tag, "SMP response tag mismatch");
	zassert_ok(smp_rsp_event.status, "SMP response status=%d",
		   smp_rsp_event.status);
	zassert_equal(smp_rsp_event.response_len, smp_rsp_len,
		      "SMP response should contain decrypted SMP");
	zassert_mem_equal(smp_rsp_event.response, smp_rsp, smp_rsp_len,
			  "SMP response SMP mismatch");
}

ZTEST(mbs_management_contract, test_SMP_session_is_reused_for_next_command)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_req_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g',
	};
	const uint8_t echo_rsp_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'o', 'n', 'g',
	};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t smp_rsp[32];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	uint32_t first_tag = 0U;
	uint32_t second_tag = 0U;

	test_contact_prepare(&contact, 0xd0U, true);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_req_payload,
			       sizeof(echo_req_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	zassert_ok(mbs_management_smp_request(&request, &first_tag));
	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "first command did not open a session");
	test_frame_header_parse(session_init_req.payload,
				session_init_req.payload_len, &session_init_hdr);
	zassert_equal(session_init_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr,
		&session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge,
					session_challenge_len);
	zassert_true(test_wait_anon_data_request(&data_req),
		     "first DATA_REQUEST was not published");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.seq, 1U);

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP,
		       echo_rsp_payload, sizeof(echo_rsp_payload), smp_rsp,
		       &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes, response_frame,
					response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "first SMP response was not published");
	zassert_equal(smp_rsp_event.tag, first_tag);

	test_anon_data_request_drain();
	zassert_ok(mbs_management_smp_request(&request, &second_tag));
	zassert_not_equal(second_tag, first_tag);
	zassert_true(test_wait_anon_data_request(&data_req),
		     "reused session did not send DATA_REQUEST");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST,
		      "second command unexpectedly repeated SESSION_INIT");
	zassert_equal(data_hdr.session_id, session.session_id);
	zassert_equal(data_hdr.seq, 2U);

	response_frame_len = test_data_response_frame_build(
		&session, 2U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes, response_frame,
					response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "second SMP response was not published");
	zassert_equal(smp_rsp_event.tag, second_tag);
}

ZTEST(mbs_management_contract,
      test_SMP_session_reject_clears_reused_operator_session)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_req_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g',
	};
	const uint8_t echo_rsp_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'o', 'n', 'g',
	};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_header restarted_session_init_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t smp_rsp[32];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t reject_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t invalid_response = 0U;
	size_t session_challenge_len;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	size_t reject_frame_len;
	uint32_t first_tag = 0U;
	uint32_t second_tag = 0U;
	uint32_t third_tag = 0U;

	test_contact_prepare(&contact, 0xd2U, true);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_req_payload,
			       sizeof(echo_req_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	zassert_ok(mbs_management_smp_request(&request, &first_tag));
	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "first command did not open a session");
	test_frame_header_parse(session_init_req.payload,
				session_init_req.payload_len, &session_init_hdr);
	zassert_equal(session_init_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);
	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr,
		&session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge,
					session_challenge_len);
	zassert_true(test_wait_anon_data_request(&data_req),
		     "first DATA_REQUEST was not published");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.seq, 1U);

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP,
		       echo_rsp_payload, sizeof(echo_rsp_payload), smp_rsp,
		       &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes, response_frame,
					response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "first SMP response was not published");
	zassert_equal(smp_rsp_event.tag, first_tag);
	zassert_ok(smp_rsp_event.status);

	test_anon_data_request_drain();
	zassert_ok(mbs_management_smp_request(&request, &second_tag));
	zassert_not_equal(second_tag, first_tag);
	zassert_true(test_wait_anon_data_request(&data_req),
		     "reused session did not send DATA_REQUEST");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST,
		      "second command unexpectedly repeated SESSION_INIT");
	zassert_equal(data_hdr.session_id, session.session_id);
	zassert_equal(data_hdr.seq, 2U);

	reject_frame_len = test_session_reject_frame_build(
		&data_hdr, -ESTALE, reject_frame);
	test_anon_data_response_publish(contact.public_key.bytes, reject_frame,
					reject_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SESSION_REJECT did not complete the reused command");
	zassert_equal(smp_rsp_event.tag, second_tag);
	zassert_equal(smp_rsp_event.status, -ESTALE);
	zassert_equal(smp_rsp_event.response_len, 0U);

	test_anon_data_request_drain();
	zassert_ok(mbs_management_smp_request(&request, &third_tag));
	zassert_not_equal(third_tag, first_tag);
	zassert_not_equal(third_tag, second_tag);
	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "request after SESSION_REJECT did not restart the session");
	test_frame_header_parse(session_init_req.payload,
				session_init_req.payload_len,
				&restarted_session_init_hdr);
	zassert_equal(restarted_session_init_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT,
		      "request after SESSION_REJECT reused the stale session");
	zassert_equal(restarted_session_init_hdr.session_id, 0U);
	zassert_equal(restarted_session_init_hdr.seq, 0U);

	test_anon_data_response_publish(contact.public_key.bytes,
					&invalid_response,
					sizeof(invalid_response));
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "cleanup response was not published");
	zassert_equal(smp_rsp_event.tag, third_tag);
	zassert_equal(smp_rsp_event.status, -EINVAL);
}

ZTEST(mbs_management_contract, test_SMP_timeout_retries_same_command_once)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_req_payload[] = {
		0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g',
	};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	struct mbs_meshcore_anon_data_send_request_event retried_data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_header retried_data_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	uint32_t tag = 0U;

	test_contact_prepare(&contact, 0xd1U, true);
	test_smp_request_build(&request, contact.public_key.bytes,
			       MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE,
			       echo_req_payload, sizeof(echo_req_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	zassert_ok(mbs_management_smp_request(&request, &tag));
	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "command did not open a session");
	test_frame_header_parse(session_init_req.payload,
				session_init_req.payload_len, &session_init_hdr);
	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr,
		&session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge,
					session_challenge_len);

	zassert_true(test_wait_anon_data_request(&data_req),
		     "initial DATA_REQUEST was not published");
	test_frame_header_parse(data_req.payload, data_req.payload_len,
				&data_hdr);
	zassert_equal(data_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_equal(data_hdr.seq, 1U);

	zassert_true(test_wait_anon_data_request_timeout(
			     &retried_data_req,
			     K_MSEC(CONFIG_MBS_MANAGEMENT_SMP_TIMEOUT_MS +
				    CONFIG_MBS_MANAGEMENT_REQUEST_FRAGMENT_DELAY_MS +
				    500)),
		     "timed-out command was not retried");
	test_frame_header_parse(retried_data_req.payload,
				retried_data_req.payload_len, &retried_data_hdr);
	zassert_equal(retried_data_hdr.type,
		      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_equal(retried_data_hdr.session_id, data_hdr.session_id);
	zassert_equal(retried_data_hdr.seq, data_hdr.seq,
		      "retry must reuse the command sequence");
	zassert_equal(retried_data_hdr.frag_index, data_hdr.frag_index);
	zassert_equal(retried_data_hdr.frag_count, data_hdr.frag_count);
	zassert_mem_equal(retried_data_req.public_key, data_req.public_key,
			  sizeof(data_req.public_key));
	zassert_equal(retried_data_req.payload_len, data_req.payload_len);
	zassert_mem_equal(retried_data_req.payload, data_req.payload,
			  data_req.payload_len,
			  "retry must retransmit the identical encrypted request");

	zassert_true(test_wait_smp_response_timeout(
			     &smp_rsp_event,
			     K_MSEC(CONFIG_MBS_MANAGEMENT_SMP_TIMEOUT_MS +
				    500)),
		     "retry exhaustion did not complete the command");
	zassert_equal(smp_rsp_event.tag, tag);
	zassert_equal(smp_rsp_event.status, -ETIMEDOUT);
	zassert_equal(smp_rsp_event.response_len, 0U);
	test_expect_no_anon_data_request();
}

ZTEST(mbs_management_contract, test_SMP_request_sends_fragmented_data_frames)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req0 = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req1 = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr0 = {0};
	struct test_header data_hdr1 = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t large_payload[TEST_PLAINTEXT_MAX_LEN + 32U];
	uint8_t data_plain0[TEST_PLAINTEXT_MAX_LEN];
	uint8_t data_plain1[TEST_PLAINTEXT_MAX_LEN];
	uint8_t smp_rsp[16];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	const uint8_t rsp_payload[] = {0xa0};
	size_t session_challenge_len;
	size_t data_plain0_len = 0U;
	size_t data_plain1_len = 0U;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xc2U, true);
	memset(large_payload, 0x6c, sizeof(large_payload));
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, large_payload,
			       sizeof(large_payload));
	zassert_true(request.packet_len > TEST_PLAINTEXT_MAX_LEN);
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_ok(rc, "SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");

	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "anon-data SESSION_INIT was not published");
	test_frame_header_parse(session_init_req.payload, session_init_req.payload_len,
				&session_init_hdr);
	zassert_equal(session_init_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);

	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr, &session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge, session_challenge_len);

	zassert_true(test_wait_anon_data_request(&data_req0),
		     "first DATA_REQUEST fragment was not published");
	test_frame_header_parse(data_req0.payload, data_req0.payload_len, &data_hdr0);
	zassert_equal(data_hdr0.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_equal(data_hdr0.frag_index, 0U);
	zassert_equal(data_hdr0.frag_count, 2U);
	test_request_fragment_ack_publish(contact.public_key.bytes, &session,
					  &data_hdr0);
	zassert_true(test_wait_anon_data_request(&data_req1),
		     "second DATA_REQUEST fragment was not published");
	test_frame_header_parse(data_req1.payload, data_req1.payload_len, &data_hdr1);
	zassert_equal(data_hdr1.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	zassert_equal(data_hdr0.session_id, session.session_id);
	zassert_equal(data_hdr1.session_id, session.session_id);
	zassert_equal(data_hdr0.seq, data_hdr1.seq);
	zassert_equal(data_hdr0.seq, 1U);
	zassert_equal(data_hdr0.frag_index, 0U);
	zassert_equal(data_hdr1.frag_index, 1U);
	zassert_equal(data_hdr0.frag_count, 2U);
	zassert_equal(data_hdr1.frag_count, 2U);

	test_aead_decrypt(session.key, data_req0.payload, data_req0.payload_len,
			  data_plain0, &data_plain0_len);
	test_aead_decrypt(session.key, data_req1.payload, data_req1.payload_len,
			  data_plain1, &data_plain1_len);
	zassert_equal(data_plain0_len, TEST_PLAINTEXT_MAX_LEN);
	zassert_equal(data_plain1_len, request.packet_len - TEST_PLAINTEXT_MAX_LEN);
	zassert_mem_equal(data_plain0, request.packet, data_plain0_len);
	zassert_mem_equal(data_plain1, &request.packet[TEST_PLAINTEXT_MAX_LEN],
			  data_plain1_len);

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP,
		       rsp_payload, sizeof(rsp_payload), smp_rsp, &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame, response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SMP response was not published");
	zassert_equal(smp_rsp_event.tag, tag, "SMP response tag mismatch");
}

ZTEST(mbs_management_contract, test_SMP_608_byte_request_uses_six_fragments)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t decrypted[MBS_MANAGEMENT_SMP_PACKET_MAX_LEN];
	const uint8_t rsp_payload[] = {0xa0};
	uint8_t smp_rsp[16];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	size_t decrypted_len = 0U;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	uint32_t tag = 0U;

	zassert_equal(TEST_PLAINTEXT_MAX_LEN, 104U,
		      "test requires the full 136-byte anonymous bearer frame");
	zassert_equal(TEST_SMP_EFFECTIVE_MAX_LEN,
		      MBS_MANAGEMENT_SMP_PACKET_MAX_LEN);
	test_contact_prepare(&contact, 0xd2U, true);
	memcpy(request.contact_prefix, contact.public_key.bytes,
	       sizeof(request.contact_prefix));
	request.packet_len = MBS_MANAGEMENT_SMP_PACKET_MAX_LEN;
	request.packet[0] = MGMT_OP_WRITE;
	request.packet[1] = 0U;
	sys_put_be16(request.packet_len - MGMT_HDR_SIZE, &request.packet[2]);
	sys_put_be16(MGMT_GROUP_ID_OS, &request.packet[4]);
	request.packet[6] = 7U;
	request.packet[7] = 0U;
	for (size_t i = MGMT_HDR_SIZE; i < request.packet_len; i++) {
		request.packet[i] = (uint8_t)i;
	}
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	zassert_ok(mbs_management_smp_request(&request, &tag));
	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "608-byte request did not open a session");
	test_frame_header_parse(session_init_req.payload,
				session_init_req.payload_len, &session_init_hdr);
	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr,
		&session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge,
					session_challenge_len);

	for (uint8_t frag = 0U; frag < 6U; frag++) {
		size_t plain_len = 0U;

		zassert_true(test_wait_anon_data_request(&data_req),
			     "DATA_REQUEST fragment %u was not published", frag);
		test_frame_header_parse(data_req.payload, data_req.payload_len,
					&data_hdr);
		zassert_equal(data_hdr.type,
			      MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
		zassert_equal(data_hdr.session_id, session.session_id);
		zassert_equal(data_hdr.seq, 1U);
		zassert_equal(data_hdr.frag_index, frag);
		zassert_equal(data_hdr.frag_count, 6U);
		zassert_true(data_req.payload_len <=
			     CONFIG_MBS_MANAGEMENT_ANON_FRAME_MAX_LEN);
		test_aead_decrypt(session.key, data_req.payload,
				  data_req.payload_len, &decrypted[decrypted_len],
				  &plain_len);
		zassert_equal(plain_len,
			      frag < 5U ? TEST_PLAINTEXT_MAX_LEN : 88U);
		decrypted_len += plain_len;
		if (frag + 1U < 6U) {
			test_request_fragment_ack_publish(
				contact.public_key.bytes, &session, &data_hdr);
		}
	}
	zassert_equal(decrypted_len, request.packet_len);
	zassert_mem_equal(decrypted, request.packet, request.packet_len);

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP, rsp_payload,
		       sizeof(rsp_payload), smp_rsp, &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes, response_frame,
					response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "608-byte request response was not published");
	zassert_equal(smp_rsp_event.tag, tag);
}

ZTEST(mbs_management_contract, test_SMP_response_reassembles_fragments)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 'd', 0x64, 'p', 'i', 'n', 'g'};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t response_frame0[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t response_frame1[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t smp_rsp[TEST_PLAINTEXT_MAX_LEN + 32U];
	uint8_t rsp_payload[TEST_PLAINTEXT_MAX_LEN];
	size_t session_challenge_len;
	size_t response_frame0_len;
	size_t response_frame1_len;
	size_t smp_rsp_len = 0U;
	size_t frag0_len;
	size_t frag1_len;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xc4U, true);
	test_smp_request_build(&request, contact.public_key.bytes, MGMT_GROUP_ID_OS,
			       0U, MGMT_OP_WRITE, echo_payload,
			       sizeof(echo_payload));
	memset(rsp_payload, 0x5a, sizeof(rsp_payload));
	test_anon_data_request_drain();
	test_smp_response_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_ok(rc, "SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");

	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "anon-data SESSION_INIT was not published");
	test_frame_header_parse(session_init_req.payload, session_init_req.payload_len,
				&session_init_hdr);
	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr, &session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge, session_challenge_len);
	zassert_true(test_wait_anon_data_request(&data_req),
		     "anon-data DATA_REQUEST was not published");

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE_RSP,
		       rsp_payload, sizeof(rsp_payload), smp_rsp, &smp_rsp_len);
	zassert_true(smp_rsp_len > TEST_PLAINTEXT_MAX_LEN);
	frag0_len = TEST_PLAINTEXT_MAX_LEN;
	frag1_len = smp_rsp_len - frag0_len;
	response_frame0_len = test_data_response_fragment_build(
		&session, 1U, 0U, 2U, smp_rsp, frag0_len, response_frame0);
	response_frame1_len = test_data_response_fragment_build(
		&session, 1U, 1U, 2U, &smp_rsp[frag0_len], frag1_len,
		response_frame1);

	/* Duplicate fragments should not abort the in-flight exchange. */
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame0, response_frame0_len);
	test_expect_response_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame0, response_frame0_len);
	test_expect_response_fragment_ack(&session, 1U, 0U, 2U);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame1, response_frame1_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SMP response was not published");
	zassert_equal(smp_rsp_event.tag, tag, "SMP response tag mismatch");
	zassert_equal(smp_rsp_event.response_len, smp_rsp_len,
		      "SMP response should contain reassembled SMP");
	zassert_mem_equal(smp_rsp_event.response, smp_rsp, smp_rsp_len,
			  "SMP response SMP mismatch");
}

ZTEST(mbs_management_contract, test_smp_meshcore_config_get_builds_read_smp)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_smp_request_event request = {0};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_session session;
	const uint8_t rsp_payload[] = {0xa0};
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t data_plain[TEST_PLAINTEXT_MAX_LEN];
	uint8_t empty_envelope[16];
	uint8_t smp_rsp[16];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	size_t data_plain_len = 0U;
	size_t empty_envelope_len = 0U;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xc8U, true);
	test_empty_mbs_envelope_build(empty_envelope, &empty_envelope_len);
	test_smp_request_build(&request, contact.public_key.bytes,
			       meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
			       meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
			       MGMT_OP_READ, empty_envelope, empty_envelope_len);
	test_anon_data_request_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_smp_request(&request, &tag);
	zassert_ok(rc, "SMP request failed: %d", rc);
	zassert_not_equal(tag, 0U, "SMP exchange tag should be nonzero");

	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "anon-data SESSION_INIT was not published");
	test_frame_header_parse(session_init_req.payload, session_init_req.payload_len,
				&session_init_hdr);
	zassert_equal(session_init_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);

	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr, &session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge, session_challenge_len);

	zassert_true(test_wait_anon_data_request(&data_req),
		     "anon-data DATA_REQUEST was not published");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	test_aead_decrypt(session.key, data_req.payload, data_req.payload_len,
			  data_plain, &data_plain_len);

	zassert_equal(data_plain_len, MGMT_HDR_SIZE + empty_envelope_len,
		      "MeshCore config get request should carry an empty envelope");
	zassert_equal(data_plain[0] & 0x07U, MGMT_OP_READ);
	zassert_equal(sys_get_be16(&data_plain[4]),
		      meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE);
	zassert_equal(data_plain[7],
		      meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG);
	zassert_mem_equal(&data_plain[MGMT_HDR_SIZE], empty_envelope,
			  empty_envelope_len, "MeshCore config get envelope mismatch");

	test_smp_build(
		meshbus_MeshcoreMgmtGroupId_MESHCORE_MGMT_GROUP_ID_MESHBUS_MESHCORE,
		meshbus_MeshcoreMgmtCommandId_MESHCORE_MGMT_COMMAND_ID_CONFIG,
		MGMT_OP_READ_RSP, rsp_payload, sizeof(rsp_payload),
		smp_rsp, &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame, response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SMP response was not published");
	zassert_equal(smp_rsp_event.tag, tag, "SMP response tag mismatch");
}

ZTEST(mbs_management_contract, test_secret_set_request_builds_management_secret_smp)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	mbs_management_secret_set_request_event request = {0};
	struct mbs_meshcore_anon_data_send_request_event session_init_req = {0};
	struct mbs_meshcore_anon_data_send_request_event data_req = {0};
	mbs_management_smp_response_event smp_rsp_event = {0};
	struct test_header session_init_hdr = {0};
	struct test_header data_hdr = {0};
	struct test_session session;
	uint8_t session_challenge[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t data_plain[TEST_PLAINTEXT_MAX_LEN];
	uint8_t empty_envelope[16];
	uint8_t smp_rsp[32];
	uint8_t response_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	size_t session_challenge_len;
	size_t data_plain_len = 0U;
	size_t empty_envelope_len = 0U;
	size_t smp_rsp_len = 0U;
	size_t response_frame_len;
	size_t rotated_len = strlen(TEST_SECRET_ROTATED);
	uint32_t tag = 0U;
	int rc;

	test_contact_prepare(&contact, 0xcaU, true);
	memcpy(request.contact_prefix, contact.public_key.bytes,
	       sizeof(request.contact_prefix));
	request.secret_len = (uint8_t)rotated_len;
	memcpy(request.secret, TEST_SECRET_ROTATED, rotated_len);
	test_anon_data_request_drain();
	(void)mbs_contact_reset(contact.public_key.bytes);
	zassert_ok(mbs_contact_set(contact.public_key.bytes, &contact));

	rc = mbs_management_secret_set_request(&request, &tag);
	zassert_ok(rc, "secret set request failed: %d", rc);
	zassert_not_equal(tag, 0U, "secret set tag should be nonzero");

	zassert_true(test_wait_anon_data_request(&session_init_req),
		     "anon-data SESSION_INIT was not published");
	test_frame_header_parse(session_init_req.payload, session_init_req.payload_len,
				&session_init_hdr);
	zassert_equal(session_init_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT);

	session_challenge_len = test_smp_session_challenge_frame_build(
		&session_init_hdr, &session_init_req.payload[MBS_MANAGEMENT_HEADER_SIZE],
		TEST_SECRET, &session, session_challenge);
	test_anon_data_response_publish(contact.public_key.bytes,
					session_challenge, session_challenge_len);

	zassert_true(test_wait_anon_data_request(&data_req),
		     "anon-data DATA_REQUEST was not published");
	test_frame_header_parse(data_req.payload, data_req.payload_len, &data_hdr);
	zassert_equal(data_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST);
	test_aead_decrypt(session.key, data_req.payload, data_req.payload_len,
			  data_plain, &data_plain_len);

	zassert_true(data_plain_len >= MGMT_HDR_SIZE, "DATA_REQUEST too short");
	zassert_equal(data_plain[0] & 0x07U, MGMT_OP_WRITE);
	zassert_equal(sys_get_be16(&data_plain[4]),
		      meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT);
	zassert_equal(data_plain[7],
		      meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET);
	zassert_true(bytes_contains(&data_plain[MGMT_HDR_SIZE],
				    data_plain_len - MGMT_HDR_SIZE,
				    (const uint8_t *)TEST_SECRET_ROTATED,
				    rotated_len),
		     "DATA_REQUEST should contain rotated secret");

	test_empty_mbs_envelope_build(empty_envelope, &empty_envelope_len);
	test_smp_build(
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		MGMT_OP_WRITE_RSP, empty_envelope, empty_envelope_len,
		smp_rsp, &smp_rsp_len);
	response_frame_len = test_data_response_frame_build(
		&session, 1U, smp_rsp, smp_rsp_len, response_frame);
	test_anon_data_response_publish(contact.public_key.bytes,
					response_frame, response_frame_len);
	zassert_true(test_wait_smp_response(&smp_rsp_event),
		     "SMP response was not published");
	zassert_equal(smp_rsp_event.tag, tag, "SMP response tag mismatch");
}

ZTEST(mbs_management_contract, test_anon_endpoint_wraps_smp_response_in_data_frame)
{
	mbs_contact sender = meshbus_Contact_init_zero;
	mbs_contact got = meshbus_Contact_init_zero;
	struct mbs_meshcore_anon_data_send_request_event session_challenge = {0};
	struct test_session session = {0};
	struct test_header session_challenge_hdr = {0};
	const uint8_t echo_payload[] = {0xa1, 0x61, 0x64, 0x62, 0x68, 0x69};
	const uint8_t expected_echo_pair[] = {0x61, 0x72, 0x62, 0x68, 0x69};
	uint8_t session_init[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t data_frame[MBS_MANAGEMENT_FRAME_MAX_LEN];
	uint8_t smp[32];
	uint8_t rsp[CONFIG_MCUMGR_TRANSPORT_NETBUF_SIZE];
	size_t session_init_len;
	size_t data_frame_len;
	size_t smp_len = 0U;
	size_t rsp_len = 0U;

	test_identity_prepare();
	test_contact_prepare(&sender, 0xd0U, false);
	(void)mbs_contact_reset(sender.public_key.bytes);
	test_target_id_derive(session.target_id);
	session_init_len = test_session_init_frame_build(session.target_id, session_init);
	memcpy(session.client_nonce, &session_init[MBS_MANAGEMENT_HEADER_SIZE],
	       sizeof(session.client_nonce));

	test_anon_data_request_drain();
	test_anon_data_response_publish_route(sender.public_key.bytes, session_init,
					      session_init_len,
					      MBS_MESHCORE_ROUTE_DIRECT, 0U);
	zassert_true(test_wait_anon_data_request(&session_challenge),
		     "anon SESSION_CHALLENGE was not published");
	zassert_mem_equal(session_challenge.public_key, sender.public_key.bytes,
			  MBS_MESHCORE_PUBLIC_KEY_SIZE,
			  "anon SESSION_CHALLENGE target mismatch");
	zassert_equal(session_challenge.delay_ms,
		      CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS,
		      "SESSION_CHALLENGE turn-around delay mismatch");
	test_frame_header_parse(session_challenge.payload, session_challenge.payload_len,
				&session_challenge_hdr);
	zassert_equal(session_challenge_hdr.type, MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE);
	zassert_mem_equal(session_challenge_hdr.target_id, session.target_id,
			  sizeof(session.target_id), "SESSION_CHALLENGE target id mismatch");
	zassert_equal(mbs_contact_find_by_key(sender.public_key.bytes, &got), -ENOENT,
		      "SESSION_INIT must not create operator contact");
	session.session_id = session_challenge_hdr.session_id;
	memcpy(session.server_nonce,
	       &session_challenge.payload[MBS_MANAGEMENT_HEADER_SIZE +
				  MBS_MANAGEMENT_NONCE_SIZE],
	       sizeof(session.server_nonce));
	test_key_derive(TEST_SECRET, &session, session.key);

	test_smp_build(MGMT_GROUP_ID_OS, 0U, MGMT_OP_WRITE, echo_payload,
		       sizeof(echo_payload), smp, &smp_len);
	data_frame_len = test_data_frame_build(&session, 1U, 0U, 1U, smp,
					       smp_len, data_frame);

	test_anon_data_request_drain();
	test_anon_data_response_publish_route(sender.public_key.bytes, data_frame,
					      data_frame_len,
					      MBS_MESHCORE_ROUTE_DIRECT, 0U);
	test_anon_read_data_response(sender.public_key.bytes, &session, rsp,
				     &rsp_len);
	zassert_equal(mbs_contact_find_by_key(sender.public_key.bytes, &got),
		      -ENOENT,
		      "Management must not infer or persist a route from consumed path metadata");

	zassert_true(rsp_len >= 8U + sizeof(echo_payload));
	zassert_equal(rsp[0] & 0x07U, MGMT_OP_WRITE_RSP);
	zassert_equal(sys_get_be16(&rsp[4]), MGMT_GROUP_ID_OS);
	zassert_equal(rsp[7], 0U);
	zassert_true(bytes_contains(&rsp[8], rsp_len - 8U, expected_echo_pair,
				    sizeof(expected_echo_pair)));
}

ZTEST_SUITE(mbs_management_contract, NULL, NULL, NULL, NULL, NULL);
