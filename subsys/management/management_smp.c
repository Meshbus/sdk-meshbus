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

#include <pb_encode.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <contact/contact.h>
#include <meshcore/meshcore.h>
#if defined(CONFIG_MBS_NOTIFY)
#include <notify/notify.h>
#endif
#include <management/management.h>

#include "mbs_mgmt_internal.h"
#include "management_priv.h"
#include "meshbus/firmware.pb.h"

LOG_MODULE_DECLARE(mbs_management, CONFIG_MBS_MANAGEMENT_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */

enum management_smp_phase {
	MANAGEMENT_SMP_PHASE_IDLE = 0,
	MANAGEMENT_SMP_PHASE_WAIT_SESSION_CHALLENGE,
	MANAGEMENT_SMP_PHASE_WAIT_RESPONSE,
};

struct management_smp_state {
	bool active;
	enum management_smp_phase phase;
	uint32_t tag;
	uint8_t target_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t client_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t server_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
	uint32_t session_id;
	uint32_t next_seq;
	size_t smp_len;
	uint8_t *smp;
	uint32_t data_seq;
	uint8_t data_frag_count;
	uint8_t next_data_frag;
	bool waiting_request_ack;
	bool direct_only;
	struct management_return_route return_route;
	uint8_t retry_count;
	struct management_reassembly *response_reassembly;
};

struct management_operator_session {
	bool active;
	uint8_t target_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	size_t secret_len;
	uint32_t session_id;
	uint32_t next_seq;
	int64_t last_active_ms;
	struct management_return_route return_route;
};

struct management_smp_pending_ack {
	bool ready;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + 1U +
		      MBS_MANAGEMENT_AEAD_TAG_SIZE];
	size_t frame_len;
	uint32_t seq;
	uint8_t frag_index;
	uint8_t frag_count;
	uint8_t retry_count;
	bool direct_only;
	struct management_return_route return_route;
};

static K_MUTEX_DEFINE(management_smp_mutex);
static atomic_t request_tag_counter = ATOMIC_INIT(1);
static struct management_smp_state smp_state;
static struct management_operator_session operator_session;
static struct k_work_delayable smp_timeout_work;
static struct k_work_delayable smp_send_work;

static void smp_data_send_work_handler(struct k_work *work);
static void smp_state_clear_locked(void);
static void operator_session_clear_locked(void);

/* -------------------------------------------------------------------------- */
/* Encoding Helpers                                                           */
/* -------------------------------------------------------------------------- */

static int cbor_data_envelope_build(const uint8_t *proto, size_t proto_len,
				    uint8_t *out, size_t cap, size_t *out_len)
{
	size_t pos = 0U;

	if ((proto == NULL && proto_len > 0U) || out == NULL ||
	    out_len == NULL || proto_len > UINT8_MAX) {
		return -EINVAL;
	}
	if (cap < 7U + proto_len) {
		return -EMSGSIZE;
	}

	out[pos++] = 0xa1U;
	out[pos++] = 0x64U;
	memcpy(&out[pos], MBS_MGMT_DATA_KEY, strlen(MBS_MGMT_DATA_KEY));
	pos += strlen(MBS_MGMT_DATA_KEY);
	if (proto_len <= 23U) {
		out[pos++] = 0x40U | (uint8_t)proto_len;
	} else {
		if (cap < 8U + proto_len) {
			return -EMSGSIZE;
		}
		out[pos++] = 0x58U;
		out[pos++] = (uint8_t)proto_len;
	}
	if (proto_len > 0U) {
		memcpy(&out[pos], proto, proto_len);
		pos += proto_len;
	}

	*out_len = pos;
	return 0;
}

static int proto_encode_to_buffer(const void *src, const pb_msgdesc_t *fields,
				  uint8_t *out, size_t cap, size_t *out_len)
{
	pb_ostream_t stream;

	if (src == NULL || fields == NULL || out == NULL || out_len == NULL) {
		return -EINVAL;
	}

	stream = pb_ostream_from_buffer(out, cap);
	if (!pb_encode(&stream, fields, src)) {
		return -EINVAL;
	}

	*out_len = stream.bytes_written;
	return 0;
}

static int smp_packet_build(uint16_t group, uint8_t id, uint8_t op,
			    const uint8_t *payload, size_t payload_len,
			    uint8_t *out, size_t cap, size_t *out_len)
{
	if (out == NULL || out_len == NULL || (payload == NULL && payload_len > 0U) ||
	    payload_len > UINT16_MAX || MGMT_HDR_SIZE + payload_len > cap) {
		return -EINVAL;
	}

	out[0] = op;
	out[1] = 0U;
	sys_put_be16((uint16_t)payload_len, &out[2]);
	sys_put_be16(group, &out[4]);
	out[6] = MANAGEMENT_SMP_SEQ;
	out[7] = id;
	if (payload_len > 0U) {
		memcpy(&out[MGMT_HDR_SIZE], payload, payload_len);
	}
	*out_len = MGMT_HDR_SIZE + payload_len;
	return 0;
}

static int secret_set_smp_build(
	const mbs_management_secret_set_request_event *request,
	uint8_t *out, size_t cap, size_t *out_len)
{
	meshbus_ManagementSecretSetRequest req =
		meshbus_ManagementSecretSetRequest_init_zero;
	uint8_t proto[meshbus_ManagementSecretSetRequest_size];
	uint8_t payload[MANAGEMENT_SMP_PLAINTEXT_MAX_LEN];
	size_t proto_len = 0U;
	size_t payload_len = 0U;
	int rc;

	if (request == NULL || out == NULL || out_len == NULL ||
	    !management_password_is_valid(request->secret, request->secret_len)) {
		return -EINVAL;
	}

	req.secret.size = request->secret_len;
	memcpy(req.secret.bytes, request->secret, request->secret_len);
	rc = proto_encode_to_buffer(&req, meshbus_ManagementSecretSetRequest_fields,
				    proto, sizeof(proto), &proto_len);
	management_secure_wipe(&req, sizeof(req));
	if (rc != 0) {
		management_secure_wipe(proto, sizeof(proto));
		return rc;
	}

	rc = cbor_data_envelope_build(proto, proto_len, payload,
				      sizeof(payload), &payload_len);
	management_secure_wipe(proto, sizeof(proto));
	if (rc != 0) {
		management_secure_wipe(payload, sizeof(payload));
		return rc;
	}

	rc = smp_packet_build(
		meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT,
		meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET,
		MGMT_OP_WRITE, payload, payload_len, out, cap, out_len);
	management_secure_wipe(payload, sizeof(payload));
	return rc;
}

static uint32_t request_tag_next(void)
{
	uint32_t tag = (uint32_t)atomic_inc(&request_tag_counter);

	return tag == 0U ? (uint32_t)atomic_inc(&request_tag_counter) : tag;
}

static size_t smp_session_init_frame_build(
	const struct management_smp_state *state, uint8_t *frame,
	size_t cap)
{
	struct management_frame_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT,
		.session_id = 0U,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = MBS_MANAGEMENT_NONCE_SIZE +
			       MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE,
	};
	size_t route_off;

	if (state == NULL || frame == NULL ||
	    cap < MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len +
			state->return_route.path_byte_len) {
		return 0U;
	}

	memcpy(hdr.target_id, state->target_id, sizeof(hdr.target_id));
	management_frame_header_write(frame, &hdr);
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE], state->client_nonce,
	       sizeof(state->client_nonce));
	route_off = MBS_MANAGEMENT_HEADER_SIZE +
		    MBS_MANAGEMENT_NONCE_SIZE;
	frame[route_off] = state->return_route.present
		? MBS_MANAGEMENT_SESSION_ROUTE_PRESENT : 0U;
	frame[route_off + 1U] = state->return_route.path_hash_size;
	frame[route_off + 2U] = state->return_route.path_byte_len;
	if (state->return_route.path_byte_len > 0U) {
		memcpy(&frame[route_off +
			      MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE],
		       state->return_route.path,
		       state->return_route.path_byte_len);
		hdr.payload_len += state->return_route.path_byte_len;
		sys_put_le16(hdr.payload_len,
			     &frame[MANAGEMENT_OFF_PAYLOAD_LEN]);
	}
	return MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
}

static int smp_data_frame_build(
	const struct management_smp_state *state, uint8_t frag_index,
	uint8_t frag_count, uint8_t *frame, size_t cap, size_t *frame_len)
{
	struct management_frame_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST,
	};
	size_t offset;
	size_t frag_len;
	int rc;

	if (state == NULL || state->smp == NULL || frame == NULL ||
	    frame_len == NULL ||
	    state->smp_len == 0U ||
	    state->smp_len > MANAGEMENT_SMP_EFFECTIVE_MAX_LEN ||
	    frag_count == 0U || frag_count > CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS ||
	    frag_index >= frag_count) {
		return -EINVAL;
	}

	offset = frag_index * MANAGEMENT_SMP_PLAINTEXT_MAX_LEN;
	if (offset >= state->smp_len) {
		return -EINVAL;
	}
	frag_len = MIN(state->smp_len - offset, MANAGEMENT_SMP_PLAINTEXT_MAX_LEN);
	if (cap < MBS_MANAGEMENT_HEADER_SIZE + frag_len +
		  MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		return -EINVAL;
	}

	hdr.session_id = state->session_id;
	hdr.seq = state->data_seq;
	hdr.frag_index = frag_index;
	hdr.frag_count = frag_count;
	hdr.payload_len = frag_len + MBS_MANAGEMENT_AEAD_TAG_SIZE;
	memcpy(hdr.target_id, state->target_id, sizeof(hdr.target_id));
	management_frame_header_write(frame, &hdr);
	rc = management_aead_encrypt(state->key, frame, &state->smp[offset], frag_len);
	if (rc != 0) {
		return rc;
	}

	*frame_len = MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
	return 0;
}

static int smp_ack_frame_build(
	const struct management_smp_state *state, uint8_t type,
	uint8_t frag_index, uint8_t frag_count, uint8_t *frame, size_t cap,
	size_t *frame_len)
{
	struct management_frame_header hdr = {
		.type = type,
	};
	uint8_t plain = frag_index;
	int rc;

	if (state == NULL || frame == NULL || frame_len == NULL ||
	    type != MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK ||
	    state->session_id == 0U || state->data_seq == 0U ||
	    frag_count == 0U ||
	    frag_count > CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS ||
	    frag_index >= frag_count ||
	    cap < MBS_MANAGEMENT_HEADER_SIZE + sizeof(plain) +
		  MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		return -EINVAL;
	}

	hdr.session_id = state->session_id;
	hdr.seq = state->data_seq;
	hdr.frag_index = frag_index;
	hdr.frag_count = frag_count;
	hdr.payload_len = sizeof(plain) + MBS_MANAGEMENT_AEAD_TAG_SIZE;
	memcpy(hdr.target_id, state->target_id, sizeof(hdr.target_id));
	management_frame_header_write(frame, &hdr);
	rc = management_aead_encrypt(state->key, frame, &plain, sizeof(plain));
	if (rc != 0) {
		return rc;
	}

	*frame_len = MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len;
	return 0;
}

static void smp_state_clear_locked(void)
{
	if (smp_state.smp != NULL) {
		management_secure_wipe(smp_state.smp, smp_state.smp_len);
		k_free(smp_state.smp);
	}
	if (smp_state.response_reassembly != NULL) {
		management_secure_wipe(smp_state.response_reassembly,
				       sizeof(*smp_state.response_reassembly));
		k_free(smp_state.response_reassembly);
	}
	management_secure_wipe(&smp_state, sizeof(smp_state));
}

static void smp_response_reassembly_clear_locked(void)
{
	if (smp_state.response_reassembly == NULL) {
		return;
	}
	management_secure_wipe(smp_state.response_reassembly,
			       sizeof(*smp_state.response_reassembly));
	k_free(smp_state.response_reassembly);
	smp_state.response_reassembly = NULL;
}

static void operator_session_clear_locked(void)
{
	management_secure_wipe(&operator_session, sizeof(operator_session));
}

static bool operator_session_matches_locked(
	const uint8_t target_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE],
	const uint8_t *secret, size_t secret_len,
	const struct management_return_route *return_route)
{
	if (!operator_session.active ||
	    k_uptime_get() - operator_session.last_active_ms >
		    CONFIG_MBS_MANAGEMENT_SESSION_TIMEOUT_MS) {
		operator_session_clear_locked();
		return false;
	}

	return return_route != NULL && operator_session.secret_len == secret_len &&
	       memcmp(operator_session.target_public_key, target_public_key,
		      MBS_MESHCORE_PUBLIC_KEY_SIZE) == 0 &&
	       memcmp(operator_session.target_id, target_id,
		      MBS_MANAGEMENT_TARGET_ID_SIZE) == 0 &&
	       memcmp(operator_session.secret, secret, secret_len) == 0 &&
	       memcmp(&operator_session.return_route, return_route,
		      sizeof(*return_route)) == 0;
}

static int management_return_route_from_contact(
	const mbs_contact *contact, struct management_return_route *out)
{
	uint8_t hash_size;

	if (contact == NULL || out == NULL) {
		return -EINVAL;
	}
	memset(out, 0, sizeof(*out));
	hash_size = contact->path_hash_size >= 1U &&
			    contact->path_hash_size <= 3U
		? (uint8_t)contact->path_hash_size : 1U;
	if (contact->is_neighbor) {
		out->present = true;
		out->path_hash_size = hash_size;
		return 0;
	}
	if (contact->out_path.size == 0U ||
	    contact->out_path.size > sizeof(out->path) ||
	    contact->out_path.size % hash_size != 0U) {
		return -ENOENT;
	}

	out->present = true;
	out->path_hash_size = hash_size;
	out->path_byte_len = (uint8_t)contact->out_path.size;
	for (size_t src = 0U; src < contact->out_path.size; src += hash_size) {
		size_t dst = contact->out_path.size - src - hash_size;

		memcpy(&out->path[dst], &contact->out_path.bytes[src], hash_size);
	}
	return 0;
}

static int smp_exchange_data_prepare_locked(void)
{
	uint8_t frag_count;

	frag_count = DIV_ROUND_UP(smp_state.smp_len,
				  MANAGEMENT_SMP_PLAINTEXT_MAX_LEN);
	if (frag_count == 0U ||
	    frag_count > CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS) {
		return -EMSGSIZE;
	}

	do {
		operator_session.next_seq++;
	} while (operator_session.next_seq == 0U);
	smp_state.session_id = operator_session.session_id;
	memcpy(smp_state.key, operator_session.key, sizeof(smp_state.key));
	smp_state.next_seq = operator_session.next_seq;
	smp_state.data_seq = operator_session.next_seq;
	smp_state.data_frag_count = frag_count;
	smp_state.next_data_frag = 0U;
	smp_state.waiting_request_ack = false;
	smp_state.retry_count = 0U;
	smp_state.return_route = operator_session.return_route;
	smp_state.phase = MANAGEMENT_SMP_PHASE_WAIT_RESPONSE;
	operator_session.last_active_ms = k_uptime_get();
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Response Publishing                                                        */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_MBS_NOTIFY)
static __noinline void smp_response_notify_publish(
	uint32_t tag, const uint8_t *response, size_t response_len)
{
	mbs_notify *payload;
	int rc;

	if (response == NULL || response_len == 0U ||
	    response_len > mbs_management_smp_effective_max_len_get()) {
		return;
	}

	payload = k_calloc(1U, sizeof(*payload));
	if (payload == NULL) {
		LOG_DBG("Management SMP notify allocation failed: tag=%u",
			(unsigned int)tag);
		return;
	}

	payload->which_payload_variant = MBS_NOTIFY_TAG_MANAGEMENT_SMP;
	payload->payload_variant.management_smp.tag = tag;
	payload->payload_variant.management_smp.response.size =
		(pb_size_t)response_len;
	memcpy(payload->payload_variant.management_smp.response.bytes,
	       response, response_len);

	rc = mbs_notify_publish(MBS_NOTIFY_TYPE_MANAGEMENT_SMP,
				    payload);
	if (rc != 0) {
		LOG_DBG("Management SMP notify publish failed: tag=%u rc=%d",
			(unsigned int)tag, rc);
	}
	management_secure_wipe(payload, sizeof(*payload));
	k_free(payload);
}
#endif

static void smp_exchange_response_publish(
	uint32_t tag, int status, const uint8_t *response, size_t response_len)
{
	mbs_management_smp_response_event *event;
	int rc;

	if ((response == NULL && response_len > 0U) ||
	    (status == 0 && response_len < MGMT_HDR_SIZE) ||
	    (status != 0 && response_len != 0U) ||
	    response_len > mbs_management_smp_effective_max_len_get()) {
		return;
	}

	event = k_calloc(1U, sizeof(*event));
	if (event == NULL) {
		LOG_WRN("Management SMP response allocation failed: tag=%u",
			(unsigned int)tag);
		return;
	}
	event->tag = tag;
	event->status = status;
	event->response_len = (uint16_t)response_len;
	if (response_len > 0U) {
		memcpy(event->response, response, response_len);
	}

	rc = zbus_chan_pub(&mbs_management_smp_response_chan, event,
			   K_NO_WAIT);

	LOG_DBG("Management SMP response event publish: tag=%u status=%d len=%u rc=%d",
		(unsigned int)tag, status, (unsigned int)response_len, rc);
	if (rc != 0) {
		LOG_DBG("Management SMP response publish failed: tag=%u rc=%d",
			(unsigned int)tag, rc);
	}

#if defined(CONFIG_MBS_NOTIFY)
	if (response_len > 0U) {
		smp_response_notify_publish(tag, response, response_len);
	}
#endif
	management_secure_wipe(event, sizeof(*event));
	k_free(event);
}

static void smp_timeout_work_handler(struct k_work *work)
{
	uint32_t tag = 0U;
	bool retry = false;

	ARG_UNUSED(work);

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (smp_state.active) {
		if (smp_state.phase == MANAGEMENT_SMP_PHASE_WAIT_RESPONSE &&
		    smp_state.retry_count <
			    CONFIG_MBS_MANAGEMENT_COMMAND_RETRY_COUNT) {
			smp_state.retry_count++;
			smp_state.next_data_frag = 0U;
			smp_state.waiting_request_ack = false;
			smp_response_reassembly_clear_locked();
			retry = true;
		} else {
			tag = smp_state.tag;
			smp_state_clear_locked();
			operator_session_clear_locked();
		}
	}
	k_mutex_unlock(&management_smp_mutex);

	if (retry) {
		(void)k_work_reschedule(
			&smp_send_work,
			K_MSEC(CONFIG_MBS_MANAGEMENT_REQUEST_FRAGMENT_DELAY_MS));
		return;
	}

	if (tag != 0U) {
		smp_exchange_response_publish(tag, -ETIMEDOUT, NULL, 0U);
		LOG_DBG("Management SMP exchange timed out: tag=%u",
			(unsigned int)tag);
	}
}

/* -------------------------------------------------------------------------- */
/* Bearer Send                                                                */
/* -------------------------------------------------------------------------- */

static int smp_anon_frame_send(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t *frame, size_t frame_len, bool direct_only,
	const struct management_return_route *return_route)
{
	uint8_t forward_path[MBS_MESHCORE_PATH_MAX_LEN];
	uint32_t delay_ms;

	if (public_key == NULL || frame == NULL || frame_len == 0U ||
	    frame_len > MANAGEMENT_SMP_FRAME_MAX_LEN) {
		return -EINVAL;
	}
	delay_ms = frame[MANAGEMENT_OFF_TYPE] ==
			   MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT
			   ? 0U
			   : CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS;

	LOG_DBG("Management SMP anon send: target=%02x%02x%02x%02x type=%u len=%u",
		public_key[0], public_key[1], public_key[2], public_key[3],
		(unsigned int)frame[MANAGEMENT_OFF_TYPE], (unsigned int)frame_len);

	if (!direct_only) {
		return mbs_meshcore_anon_data_send_delayed(
			public_key, frame, frame_len, delay_ms);
	}
	if (return_route == NULL || !return_route->present ||
	    return_route->path_hash_size == 0U ||
	    return_route->path_hash_size > 3U ||
	    return_route->path_byte_len > sizeof(forward_path) ||
	    return_route->path_byte_len % return_route->path_hash_size != 0U) {
		return -EHOSTUNREACH;
	}
	for (size_t src = 0U; src < return_route->path_byte_len;
	     src += return_route->path_hash_size) {
		size_t dst = return_route->path_byte_len - src -
			     return_route->path_hash_size;

		memcpy(&forward_path[dst], &return_route->path[src],
		       return_route->path_hash_size);
	}
	return mbs_meshcore_anon_data_send_via_path_delayed(
		public_key, frame, frame_len, forward_path,
		return_route->path_byte_len, return_route->path_hash_size,
		delay_ms);
}

static void smp_data_send_work_handler(struct k_work *work)
{
	uint8_t target_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t frame[MANAGEMENT_SMP_FRAME_MAX_LEN];
	size_t frame_len = 0U;
	uint32_t smp_tag = 0U;
	uint8_t frag_index = 0U;
	uint8_t frag_count = 0U;
	bool last_frag = false;
	bool direct_only = false;
	struct management_return_route return_route = {0};
	bool publish_failure = false;
	int failure_status = 0;
	int rc;

	ARG_UNUSED(work);

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (!smp_state.active ||
	    smp_state.phase != MANAGEMENT_SMP_PHASE_WAIT_RESPONSE ||
	    smp_state.waiting_request_ack ||
	    smp_state.next_data_frag >= smp_state.data_frag_count) {
		k_mutex_unlock(&management_smp_mutex);
		return;
	}

	smp_tag = smp_state.tag;
	frag_index = smp_state.next_data_frag;
	frag_count = smp_state.data_frag_count;
	memcpy(target_public_key, smp_state.target_public_key,
	       sizeof(target_public_key));
	direct_only = smp_state.direct_only;
	return_route = smp_state.return_route;
	rc = smp_data_frame_build(&smp_state, frag_index, frag_count, frame,
				  sizeof(frame), &frame_len);
	if (rc == 0) {
		smp_state.next_data_frag++;
		last_frag = smp_state.next_data_frag >= smp_state.data_frag_count;
		smp_state.waiting_request_ack = !last_frag;
	}
	k_mutex_unlock(&management_smp_mutex);

	if (rc == 0) {
		rc = smp_anon_frame_send(
			target_public_key, frame, frame_len, direct_only,
			direct_only ? &return_route : NULL);
	}
	management_secure_wipe(frame, sizeof(frame));

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (smp_state.active && smp_state.tag == smp_tag &&
	    smp_state.phase == MANAGEMENT_SMP_PHASE_WAIT_RESPONSE) {
		if (rc == 0) {
			(void)k_work_reschedule(
				&smp_timeout_work,
				K_MSEC(MANAGEMENT_SMP_TIMEOUT_MS));
		} else {
			failure_status = rc;
			smp_state_clear_locked();
			(void)k_work_cancel_delayable(&smp_timeout_work);
			publish_failure = true;
		}
	}
	k_mutex_unlock(&management_smp_mutex);

	if (publish_failure) {
		smp_exchange_response_publish(
			smp_tag, failure_status, NULL, 0U);
	}
}

/* -------------------------------------------------------------------------- */
/* Response Handling                                                          */
/* -------------------------------------------------------------------------- */

static int smp_request_fragment_ack_handle_locked(
	const struct mbs_meshcore_anon_data_response_event *event)
{
	struct management_frame_header hdr;
	const uint8_t *payload = NULL;
	uint8_t plain[1];
	size_t plain_len = 0U;
	int rc;

	if (event == NULL || !smp_state.active ||
	    smp_state.phase != MANAGEMENT_SMP_PHASE_WAIT_RESPONSE ||
	    !smp_state.waiting_request_ack) {
		return -EINVAL;
	}

	rc = management_frame_header_parse(event->payload, event->payload_len,
					   &hdr, &payload);
	if (rc != 0) {
		return rc;
	}
	if (hdr.type != MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK ||
	    hdr.session_id != smp_state.session_id ||
	    hdr.seq != smp_state.data_seq ||
	    hdr.frag_count != smp_state.data_frag_count ||
	    hdr.frag_index + 1U != smp_state.next_data_frag ||
	    hdr.frag_index + 1U >= hdr.frag_count ||
	    hdr.payload_len != sizeof(plain) +
			       MBS_MANAGEMENT_AEAD_TAG_SIZE ||
	    memcmp(hdr.target_id, smp_state.target_id,
		   sizeof(smp_state.target_id)) != 0) {
		return -EINVAL;
	}

	rc = management_aead_decrypt(smp_state.key, event->payload, &hdr,
				     payload, plain, &plain_len);
	if (rc != 0) {
		return rc;
	}
	if (plain_len != sizeof(plain) || plain[0] != hdr.frag_index) {
		management_secure_wipe(plain, sizeof(plain));
		return -EINVAL;
	}
	management_secure_wipe(plain, sizeof(plain));
	smp_state.waiting_request_ack = false;
	if (operator_session.active &&
	    operator_session.session_id == smp_state.session_id) {
		operator_session.last_active_ms = k_uptime_get();
	}
	LOG_DBG("Management DATA_REQUEST_ACK receive: seq=%u frag=%u/%u",
		(unsigned int)hdr.seq,
		(unsigned int)(hdr.frag_index + 1U),
		(unsigned int)hdr.frag_count);
	return 0;
}

static int smp_response_fragment_ack_prepare_locked(
	const struct mbs_meshcore_anon_data_response_event *event,
	const struct management_frame_header *hdr,
	struct management_smp_pending_ack *pending_ack)
{
	int rc;

	if (event == NULL || hdr == NULL || pending_ack == NULL ||
	    hdr->frag_index >= hdr->frag_count) {
		return -EINVAL;
	}

	rc = smp_ack_frame_build(
		&smp_state, MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK,
		hdr->frag_index, hdr->frag_count, pending_ack->frame,
		sizeof(pending_ack->frame), &pending_ack->frame_len);
	if (rc == 0) {
		memcpy(pending_ack->public_key, event->public_key,
		       sizeof(pending_ack->public_key));
		pending_ack->seq = hdr->seq;
		pending_ack->frag_index = hdr->frag_index;
		pending_ack->frag_count = hdr->frag_count;
		pending_ack->retry_count = smp_state.retry_count;
		pending_ack->direct_only = smp_state.direct_only;
		pending_ack->return_route = smp_state.return_route;
		pending_ack->ready = true;
	}
	return rc;
}

static int smp_response_fragment_ack_send(
	const struct management_smp_pending_ack *pending_ack)
{
	int rc;

	if (pending_ack == NULL || !pending_ack->ready ||
	    pending_ack->frame_len == 0U) {
		return -EINVAL;
	}

	rc = smp_anon_frame_send(pending_ack->public_key, pending_ack->frame,
				 pending_ack->frame_len, pending_ack->direct_only,
				 pending_ack->direct_only
					 ? &pending_ack->return_route : NULL);
	LOG_DBG("Management DATA_RESPONSE_ACK send: seq=%u frag=%u/%u rc=%d",
		(unsigned int)pending_ack->seq,
		(unsigned int)(pending_ack->frag_index + 1U),
		(unsigned int)pending_ack->frag_count, rc);
	return rc;
}

static int smp_session_challenge_handle_locked(
	const struct mbs_meshcore_anon_data_response_event *event)
{
	struct management_frame_header hdr;
	const uint8_t *payload = NULL;
	int rc;

	if (event == NULL || !smp_state.active ||
	    smp_state.phase != MANAGEMENT_SMP_PHASE_WAIT_SESSION_CHALLENGE) {
		return -EINVAL;
	}

	rc = management_frame_header_parse(event->payload, event->payload_len, &hdr, &payload);
	if (rc != 0) {
		return rc;
	}
	if (hdr.type != MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE ||
	    hdr.session_id == 0U || hdr.seq != 0U || hdr.frag_index != 0U ||
	    hdr.frag_count != 1U ||
	    hdr.payload_len != 2U * MBS_MANAGEMENT_NONCE_SIZE ||
	    memcmp(hdr.target_id, smp_state.target_id,
		   sizeof(smp_state.target_id)) != 0 ||
	    memcmp(payload, smp_state.client_nonce,
		   sizeof(smp_state.client_nonce)) != 0) {
		return -EINVAL;
	}

	smp_state.session_id = hdr.session_id;
	memcpy(smp_state.server_nonce,
	       &payload[MBS_MANAGEMENT_NONCE_SIZE],
	       sizeof(smp_state.server_nonce));
	rc = management_session_key_derive(smp_state.secret, smp_state.secret_len,
				smp_state.target_id,
				smp_state.client_nonce,
				smp_state.server_nonce,
				smp_state.session_id,
				&smp_state.return_route,
				smp_state.key);
	if (rc != 0) {
		return rc;
	}

	operator_session_clear_locked();
	operator_session.active = true;
	memcpy(operator_session.target_public_key,
	       smp_state.target_public_key,
	       sizeof(operator_session.target_public_key));
	memcpy(operator_session.target_id, smp_state.target_id,
	       sizeof(operator_session.target_id));
	memcpy(operator_session.key, smp_state.key,
	       sizeof(operator_session.key));
	memcpy(operator_session.secret, smp_state.secret,
	       smp_state.secret_len);
	operator_session.secret_len = smp_state.secret_len;
	operator_session.session_id = smp_state.session_id;
	operator_session.last_active_ms = k_uptime_get();
	operator_session.return_route = smp_state.return_route;

	return smp_exchange_data_prepare_locked();
}

static int smp_data_response_handle_locked(
	const struct mbs_meshcore_anon_data_response_event *event,
	uint8_t *response, size_t response_cap, size_t *response_len,
	bool *complete, struct management_smp_pending_ack *pending_ack)
{
	struct management_frame_header hdr;
	struct management_reassembly *reassembly;
	const uint8_t *payload = NULL;
	uint8_t plain[MANAGEMENT_SMP_PLAINTEXT_MAX_LEN];
	size_t plain_len = 0U;
	size_t offset;
	int rc;

	if (event == NULL || response == NULL || response_len == NULL ||
	    complete == NULL || pending_ack == NULL || !smp_state.active ||
	    smp_state.phase != MANAGEMENT_SMP_PHASE_WAIT_RESPONSE ||
	    smp_state.waiting_request_ack ||
	    smp_state.next_data_frag != smp_state.data_frag_count) {
		return -EINVAL;
	}
	*complete = false;
	*response_len = 0U;

	rc = management_frame_header_parse(event->payload, event->payload_len, &hdr, &payload);
	if (rc != 0) {
		return rc;
	}
	if (hdr.type != MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE ||
	    hdr.session_id != smp_state.session_id ||
	    hdr.seq != smp_state.data_seq ||
	    memcmp(hdr.target_id, smp_state.target_id,
		   sizeof(smp_state.target_id)) != 0 ||
	    hdr.payload_len < MBS_MANAGEMENT_AEAD_TAG_SIZE ||
	    hdr.payload_len - MBS_MANAGEMENT_AEAD_TAG_SIZE > sizeof(plain)) {
		return -EINVAL;
	}

	rc = management_aead_decrypt(smp_state.key, event->payload, &hdr, payload,
			  plain, &plain_len);
	if (rc != 0) {
		return rc;
	}
	if (operator_session.active &&
	    operator_session.session_id == smp_state.session_id) {
		operator_session.last_active_ms = k_uptime_get();
	}

	if (smp_state.response_reassembly == NULL) {
		smp_state.response_reassembly =
			k_calloc(1U, sizeof(*smp_state.response_reassembly));
		if (smp_state.response_reassembly == NULL) {
			management_secure_wipe(plain, sizeof(plain));
			return -ENOMEM;
		}
	}
	reassembly = smp_state.response_reassembly;
	if (!reassembly->active || reassembly->seq != hdr.seq) {
		management_secure_wipe(reassembly, sizeof(*reassembly));
		reassembly->active = true;
		reassembly->seq = hdr.seq;
		reassembly->frag_count = hdr.frag_count;
	} else if (reassembly->frag_count != hdr.frag_count) {
		smp_response_reassembly_clear_locked();
		management_secure_wipe(plain, sizeof(plain));
		return -EINVAL;
	}
	if ((reassembly->received_mask & BIT(hdr.frag_index)) != 0U) {
		rc = smp_response_fragment_ack_prepare_locked(
			event, &hdr, pending_ack);
		if (rc != 0) {
			management_secure_wipe(plain, sizeof(plain));
			return rc;
		}
		management_secure_wipe(plain, sizeof(plain));
		return -EALREADY;
	}

	offset = hdr.frag_index * MANAGEMENT_SMP_PLAINTEXT_MAX_LEN;
	if (offset + plain_len > sizeof(reassembly->data)) {
		smp_response_reassembly_clear_locked();
		management_secure_wipe(plain, sizeof(plain));
		return -EMSGSIZE;
	}

	memcpy(&reassembly->data[offset], plain, plain_len);
	reassembly->len[hdr.frag_index] = plain_len;
	reassembly->received_mask |= BIT(hdr.frag_index);
	reassembly->received_count++;
	management_secure_wipe(plain, sizeof(plain));
	rc = smp_response_fragment_ack_prepare_locked(
		event, &hdr, pending_ack);
	if (rc != 0) {
		return rc;
	}

	if (reassembly->received_count != reassembly->frag_count) {
		return 0;
	}

	*response_len = 0U;
	for (uint8_t i = 0; i < reassembly->frag_count; i++) {
		offset = i * MANAGEMENT_SMP_PLAINTEXT_MAX_LEN;
		if (*response_len + reassembly->len[i] >
		    response_cap) {
			smp_response_reassembly_clear_locked();
			return -EMSGSIZE;
		}
		memcpy(&response[*response_len],
		       &reassembly->data[offset], reassembly->len[i]);
		*response_len += reassembly->len[i];
	}

	smp_response_reassembly_clear_locked();
	if (*response_len < MGMT_HDR_SIZE) {
		return -EINVAL;
	}

	*complete = true;
	return 0;
}

static int smp_session_reject_handle_locked(
	const struct mbs_meshcore_anon_data_response_event *event,
	int *status)
{
	struct management_frame_header hdr;
	const uint8_t *payload = NULL;
	int32_t decoded_status;
	int rc;

	if (event == NULL || status == NULL) {
		return -EINVAL;
	}

	rc = management_frame_header_parse(event->payload, event->payload_len,
					   &hdr, &payload);
	if (rc != 0) {
		return rc;
	}
	if (hdr.type != MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT ||
	    hdr.session_id == 0U || hdr.session_id != smp_state.session_id ||
	    hdr.seq != 0U || hdr.frag_index != 0U || hdr.frag_count != 1U ||
	    hdr.payload_len != sizeof(uint32_t) ||
	    memcmp(hdr.target_id, smp_state.target_id,
		   sizeof(smp_state.target_id)) != 0) {
		return -EINVAL;
	}

	decoded_status = (int32_t)sys_get_le32(payload);
	if (decoded_status >= 0) {
		return -EINVAL;
	}

	*status = decoded_status;
	operator_session_clear_locked();
	return 0;
}

void management_smp_anon_data_response_handle(
	const struct mbs_meshcore_anon_data_response_event *event)
{
	struct management_smp_pending_ack pending_ack = {0};
	uint8_t response[MANAGEMENT_SMP_EFFECTIVE_MAX_LEN];
	size_t response_len = 0U;
	uint32_t smp_tag = 0U;
	bool response_complete = false;
	bool publish_response = false;
	bool send_data = false;
	bool frame_handled = false;
	int completion_status = 0;
	int rc = 0;

	if (event == NULL) {
		return;
	}

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (!smp_state.active ||
	    memcmp(smp_state.target_public_key, event->public_key,
		   sizeof(smp_state.target_public_key)) != 0) {
		k_mutex_unlock(&management_smp_mutex);
		return;
	}
	if (smp_state.direct_only &&
	    smp_state.phase == MANAGEMENT_SMP_PHASE_WAIT_RESPONSE &&
	    event->route != MBS_MESHCORE_ROUTE_DIRECT) {
		k_mutex_unlock(&management_smp_mutex);
		return;
	}

	smp_tag = smp_state.tag;
	LOG_DBG("Management SMP anon recv: tag=%u phase=%u sender=%02x%02x%02x%02x len=%u",
		(unsigned int)smp_tag, (unsigned int)smp_state.phase,
		event->public_key[0], event->public_key[1], event->public_key[2],
		event->public_key[3], (unsigned int)event->payload_len);

	if (event->payload_len > MANAGEMENT_OFF_TYPE &&
	    event->payload[MANAGEMENT_OFF_TYPE] ==
		    MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT) {
		rc = smp_session_reject_handle_locked(event, &completion_status);
		if (rc == 0) {
			publish_response = true;
			frame_handled = true;
			smp_state_clear_locked();
			(void)k_work_cancel_delayable(&smp_timeout_work);
		}
	}

	if (!frame_handled) {
		switch (smp_state.phase) {
		case MANAGEMENT_SMP_PHASE_WAIT_SESSION_CHALLENGE:
			rc = smp_session_challenge_handle_locked(event);
			if (rc == 0) {
				send_data = true;
			}
			break;
		case MANAGEMENT_SMP_PHASE_WAIT_RESPONSE:
			if (event->payload_len > MANAGEMENT_OFF_TYPE &&
			    event->payload[MANAGEMENT_OFF_TYPE] ==
				    MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK) {
				rc = smp_request_fragment_ack_handle_locked(event);
				if (rc == 0) {
					(void)k_work_cancel_delayable(&smp_timeout_work);
					send_data = true;
				}
				break;
			}
			rc = smp_data_response_handle_locked(
				event, response, sizeof(response), &response_len,
				&response_complete, &pending_ack);
			if (rc == -EALREADY) {
				rc = 0;
				break;
			}
			if (rc != 0) {
				break;
			}
			break;
		default:
			rc = -EINVAL;
			break;
		}
	}
	if (rc == 0 && pending_ack.ready) {
		k_mutex_unlock(&management_smp_mutex);
		rc = smp_response_fragment_ack_send(&pending_ack);
		k_mutex_lock(&management_smp_mutex, K_FOREVER);
		if (!smp_state.active || smp_state.tag != smp_tag ||
		    smp_state.retry_count != pending_ack.retry_count) {
			k_mutex_unlock(&management_smp_mutex);
			management_secure_wipe(&pending_ack, sizeof(pending_ack));
			management_secure_wipe(response, sizeof(response));
			return;
		}
	}
	if (rc == 0 && response_complete) {
		publish_response = true;
		smp_state_clear_locked();
		(void)k_work_cancel_delayable(&smp_timeout_work);
	}
	if (rc != 0) {
		completion_status = rc;
		response_len = 0U;
		publish_response = true;
		smp_state_clear_locked();
		operator_session_clear_locked();
		(void)k_work_cancel_delayable(&smp_timeout_work);
	}
	k_mutex_unlock(&management_smp_mutex);
	management_secure_wipe(&pending_ack, sizeof(pending_ack));

	if (send_data) {
		(void)k_work_reschedule(
			&smp_send_work,
			K_MSEC(CONFIG_MBS_MANAGEMENT_REQUEST_FRAGMENT_DELAY_MS));
		return;
	}

	if (publish_response) {
		smp_exchange_response_publish(
			smp_tag, completion_status,
			response_len > 0U ? response : NULL, response_len);
		management_secure_wipe(response, sizeof(response));
	}
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

static int management_smp_start(
	const uint8_t contact_prefix[MBS_MANAGEMENT_CONTACT_PREFIX_BYTES],
	const uint8_t *smp, size_t smp_len, const uint8_t *secret,
	size_t secret_len, bool use_contact_secret, uint32_t *out_tag)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	const uint8_t *effective_secret = secret;
	size_t effective_secret_len = secret_len;
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t target_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t client_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t session_init[MANAGEMENT_SMP_FRAME_MAX_LEN];
	size_t session_init_len = 0U;
	uint32_t tag = 0U;
	bool state_stored = false;
	bool reuse_session = false;
	int rc = 0;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (contact_prefix == NULL || smp == NULL || smp_len == 0U) {
		return -EINVAL;
	}
	if (smp_len > mbs_management_smp_effective_max_len_get()) {
		return -EMSGSIZE;
	}
	if (!use_contact_secret &&
	    !management_password_is_valid(secret, secret_len)) {
		return -EINVAL;
	}

	rc = mbs_contact_find_by_prefix(contact_prefix, &contact);
	if (rc != 0) {
		return rc;
	}
	if (contact.public_key.size != MBS_MESHCORE_PUBLIC_KEY_SIZE) {
		rc = -ENOENT;
		goto out;
	}
	if (use_contact_secret) {
		if (!management_password_is_valid(contact.management_secret.bytes,
					  contact.management_secret.size)) {
			rc = -ENOENT;
			goto out;
		}
		effective_secret = contact.management_secret.bytes;
		effective_secret_len = contact.management_secret.size;
	}

	memcpy(target_public_key, contact.public_key.bytes, sizeof(target_public_key));
	rc = management_target_id_from_public_key(target_public_key, target_id);
	if (rc != 0) {
		goto out;
	}
	rc = management_random_get(client_nonce, sizeof(client_nonce));
	if (rc != 0) {
		goto out;
	}
	tag = request_tag_next();

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (smp_state.active) {
		k_mutex_unlock(&management_smp_mutex);
		rc = -EBUSY;
		goto out;
	}

	smp_state_clear_locked();
	smp_state.active = true;
	smp_state.tag = tag;
	memcpy(smp_state.target_public_key, target_public_key,
	       sizeof(smp_state.target_public_key));
	memcpy(smp_state.target_id, target_id, sizeof(smp_state.target_id));
	smp_state.smp = k_malloc(smp_len);
	if (smp_state.smp == NULL) {
		smp_state_clear_locked();
		k_mutex_unlock(&management_smp_mutex);
		rc = -ENOMEM;
		goto out;
	}
	memcpy(smp_state.smp, smp, smp_len);
	smp_state.smp_len = smp_len;
	smp_state.direct_only = smp_len >= MGMT_HDR_SIZE &&
		sys_get_be16(&smp[4]) ==
			meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE;
	if (smp_state.direct_only) {
		rc = management_return_route_from_contact(
			&contact, &smp_state.return_route);
		if (rc != 0) {
			smp_state_clear_locked();
			k_mutex_unlock(&management_smp_mutex);
			goto out;
		}
	}
	reuse_session = operator_session_matches_locked(
		target_public_key, target_id, effective_secret,
		effective_secret_len, &smp_state.return_route);
	if (reuse_session) {
		rc = smp_exchange_data_prepare_locked();
		if (rc != 0) {
			smp_state_clear_locked();
			k_mutex_unlock(&management_smp_mutex);
			goto out;
		}
	} else {
		operator_session_clear_locked();
		smp_state.phase = MANAGEMENT_SMP_PHASE_WAIT_SESSION_CHALLENGE;
		memcpy(smp_state.client_nonce, client_nonce,
		       sizeof(smp_state.client_nonce));
		memcpy(smp_state.secret, effective_secret,
		       effective_secret_len);
		smp_state.secret_len = effective_secret_len;
	}

	LOG_DBG("Management SMP exchange start: tag=%u target=%02x%02x%02x%02x smp_len=%u secret=%s",
		(unsigned int)tag, target_public_key[0], target_public_key[1],
		target_public_key[2], target_public_key[3], (unsigned int)smp_len,
		use_contact_secret ? "contact" : "transient");

	if (!reuse_session) {
		session_init_len = smp_session_init_frame_build(
			&smp_state, session_init, sizeof(session_init));
		if (session_init_len == 0U) {
			smp_state_clear_locked();
			k_mutex_unlock(&management_smp_mutex);
			rc = -EINVAL;
			goto out;
		}
	}
	state_stored = true;
	k_mutex_unlock(&management_smp_mutex);

	if (reuse_session) {
		rc = k_work_reschedule(
			&smp_send_work,
			K_MSEC(CONFIG_MBS_MANAGEMENT_REQUEST_FRAGMENT_DELAY_MS));
		if (rc >= 0) {
			rc = 0;
		}
	} else {
		rc = smp_anon_frame_send(target_public_key, session_init,
					 session_init_len, false, NULL);
		management_secure_wipe(session_init, sizeof(session_init));
	}

	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	if (smp_state.active && smp_state.tag == tag) {
		if (rc == 0 && !reuse_session) {
			(void)k_work_reschedule(
				&smp_timeout_work,
				K_MSEC(MANAGEMENT_SMP_TIMEOUT_MS));
		} else if (rc != 0) {
			smp_state_clear_locked();
			operator_session_clear_locked();
		}
	}
	k_mutex_unlock(&management_smp_mutex);

	if (rc == 0 && out_tag != NULL) {
		*out_tag = tag;
	}
out:
	management_secure_wipe(&contact, sizeof(contact));
	management_secure_wipe(target_id, sizeof(target_id));
	management_secure_wipe(target_public_key, sizeof(target_public_key));
	management_secure_wipe(client_nonce, sizeof(client_nonce));
	if (!state_stored) {
		management_secure_wipe(session_init, sizeof(session_init));
	}
	return rc;
}

static int management_smp_request_validate(
	const mbs_management_smp_request_event *request, uint32_t *out_tag)
{
	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	if (request == NULL || request->packet_len == 0U) {
		return -EINVAL;
	}
	if (request->packet_len >
	    mbs_management_smp_effective_max_len_get()) {
		return -EMSGSIZE;
	}

	return 0;
}

int mbs_management_smp_request(
	const mbs_management_smp_request_event *request, uint32_t *out_tag)
{
	int rc;

	rc = management_smp_request_validate(request, out_tag);
	if (rc != 0) {
		return rc;
	}

	return management_smp_start(request->contact_prefix, request->packet,
				    request->packet_len, NULL, 0U, true,
				    out_tag);
}

int mbs_management_smp_request_with_secret(
	const mbs_management_smp_request_event *request,
	const uint8_t *secret, size_t secret_len, uint32_t *out_tag)
{
	int rc;

	rc = management_smp_request_validate(request, out_tag);
	if (rc != 0) {
		return rc;
	}

	return management_smp_start(request->contact_prefix, request->packet,
				    request->packet_len, secret, secret_len,
				    false, out_tag);
}

int mbs_management_secret_set_request(
	const mbs_management_secret_set_request_event *request, uint32_t *out_tag)
{
	uint8_t smp[MANAGEMENT_SMP_PLAINTEXT_MAX_LEN];
	size_t smp_len = 0U;
	int rc;

	if (out_tag != NULL) {
		*out_tag = 0U;
	}
	rc = secret_set_smp_build(request, smp, sizeof(smp), &smp_len);
	if (rc != 0) {
		management_secure_wipe(smp, sizeof(smp));
		return rc;
	}

	rc = management_smp_start(request->contact_prefix, smp, smp_len, NULL,
				  0U, true, out_tag);
	management_secure_wipe(smp, sizeof(smp));
	return rc;
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

void management_smp_session_clear(void)
{
	k_mutex_lock(&management_smp_mutex, K_FOREVER);
	operator_session_clear_locked();
	if (smp_state.active) {
		smp_state_clear_locked();
		(void)k_work_cancel_delayable(&smp_timeout_work);
		(void)k_work_cancel_delayable(&smp_send_work);
	}
	k_mutex_unlock(&management_smp_mutex);
}

void management_smp_init(void)
{
	k_work_init_delayable(&smp_timeout_work, smp_timeout_work_handler);
	k_work_init_delayable(&smp_send_work, smp_data_send_work_handler);
}
