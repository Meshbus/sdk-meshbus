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

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <meshcore/meshcore.h>
#include <management/management.h>
#if defined(CONFIG_MBS_FIRMWARE)
#include <firmware/firmware.h>
#endif

#include "management_priv.h"
#include "meshbus/firmware.pb.h"
#include "meshbus/power.pb.h"
#include "meshbus/radio.pb.h"

LOG_MODULE_DECLARE(mbs_management, CONFIG_MBS_MANAGEMENT_LOG_LEVEL);

struct management_session {
	bool active;
	bool established;
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t peer_public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint32_t session_id;
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	uint32_t last_rx_seq;
	uint8_t last_rx_frag_count;
	int64_t last_active_ms;
	bool direct_only;
	struct management_return_route return_route;
};

struct management_response_cache {
	bool valid;
	uint32_t session_id;
	uint32_t request_seq;
	size_t len;
	uint8_t response[MANAGEMENT_SMP_EFFECTIVE_MAX_LEN];
};

enum management_packet_state_kind {
	MANAGEMENT_PACKET_STATE_EMPTY = 0,
	MANAGEMENT_PACKET_STATE_REASSEMBLY,
	MANAGEMENT_PACKET_STATE_RESPONSE_CACHE,
};

union management_packet_state {
	struct management_reassembly reassembly;
	struct management_response_cache response_cache;
};

struct management_response_send_state {
	bool active;
	struct management_session_snapshot snapshot;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	size_t len;
	uint32_t seq;
	uint8_t frag_count;
	uint8_t next_frag;
	bool waiting_ack;
	bool restart_requested;
	bool firmware_activate;
};

struct management_anon_response_context {
	bool active;
	uint8_t route;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
};

static K_MUTEX_DEFINE(management_mutex);
static K_MUTEX_DEFINE(management_response_mutex);
static K_MUTEX_DEFINE(management_response_send_mutex);
static atomic_t smp_processing = ATOMIC_INIT(0);
static atomic_t response_send_active = ATOMIC_INIT(0);
static struct management_session session;
static struct management_session candidate;
static enum management_packet_state_kind packet_state_kind;
static union management_packet_state *packet_state;
static struct management_anon_response_context anon_response_context;
static struct management_response_send_state response_send_state;
static int64_t last_session_init_ms;
static bool session_clear_pending;
static struct k_work_delayable response_send_work;
static struct k_work_delayable response_ack_timeout_work;
static struct smp_transport management_smp_transport;

static int management_smp_output(struct net_buf *nb);
static uint16_t management_smp_get_mtu(const struct net_buf *nb);
static int session_reject_send(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE],
	uint32_t session_id, int status,
	const struct management_return_route *return_route);
static int response_frame_send(const struct management_session_snapshot *snapshot,
			       const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
			       uint32_t seq, uint8_t frag_index, uint8_t frag_count,
			       const uint8_t *plain, size_t plain_len);
static int response_packet_send_paced(const uint8_t *response, size_t len);
static int response_cache_resend(uint32_t request_seq);
static void response_send_work_handler(struct k_work *work);
static void response_ack_timeout_work_handler(struct k_work *work);
static void response_send_finish(int rc);
static bool remote_smp_packet_allowed(const uint8_t *packet, size_t packet_len);
static int local_target_id_get(uint8_t out[MBS_MANAGEMENT_TARGET_ID_SIZE]);
static void session_clear_after_response_or_now_locked(void);
static void management_session_request_frame_receive(const uint8_t *frame, size_t len);

static void anon_response_context_set(
	const struct mbs_meshcore_anon_data_response_event *request)
{
	if (request == NULL) {
		return;
	}

	k_mutex_lock(&management_response_mutex, K_FOREVER);
	memset(&anon_response_context, 0, sizeof(anon_response_context));
	anon_response_context.active = true;
	anon_response_context.route = request->route;
	memcpy(anon_response_context.public_key, request->public_key,
	       sizeof(anon_response_context.public_key));
	k_mutex_unlock(&management_response_mutex);
}

static void anon_response_context_clear(void)
{
	k_mutex_lock(&management_response_mutex, K_FOREVER);
	memset(&anon_response_context, 0, sizeof(anon_response_context));
	k_mutex_unlock(&management_response_mutex);
}

static bool anon_response_context_snapshot(
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE], uint8_t *route)
{
	bool active;

	if (public_key == NULL) {
		return false;
	}

	k_mutex_lock(&management_response_mutex, K_FOREVER);
	active = anon_response_context.active;
	if (active) {
		memcpy(public_key, anon_response_context.public_key,
		       sizeof(anon_response_context.public_key));
		if (route != NULL) {
			*route = anon_response_context.route;
		}
	}
	k_mutex_unlock(&management_response_mutex);
	return active;
}

static int secret_effective_get(uint8_t *secret, size_t *len)
{
	mbs_management_config cfg = meshbus_ManagementConfig_init_zero;
	size_t out_len;
	int rc;

	if (secret == NULL || len == NULL) {
		return -EINVAL;
	}

	rc = mbs_management_config_get(&cfg);
	if (rc != 0) {
		return rc;
	}

	if (cfg.secret.size > 0U) {
		out_len = cfg.secret.size;
		if (!management_password_is_valid(cfg.secret.bytes, out_len)) {
			management_secure_wipe(&cfg, sizeof(cfg));
			return -EINVAL;
		}
		memcpy(secret, cfg.secret.bytes, out_len);
		*len = out_len;
		management_secure_wipe(&cfg, sizeof(cfg));
		return 0;
	}
	management_secure_wipe(&cfg, sizeof(cfg));

	return -ENOENT;
}

/* -------------------------------------------------------------------------- */
/* Session Helpers                                                            */
/* -------------------------------------------------------------------------- */

static void packet_state_clear_locked(void)
{
	if (packet_state != NULL) {
		management_secure_wipe(packet_state, sizeof(*packet_state));
		k_free(packet_state);
		packet_state = NULL;
	}
	packet_state_kind = MANAGEMENT_PACKET_STATE_EMPTY;
}

static void active_session_clear_locked(void)
{
	management_secure_wipe(&session, sizeof(session));
	packet_state_clear_locked();
	session_clear_pending = false;
}

static void session_clear_locked(void)
{
	active_session_clear_locked();
	management_secure_wipe(&candidate, sizeof(candidate));
}

static void candidate_clear_locked(void)
{
	management_secure_wipe(&candidate, sizeof(candidate));
}

static void session_clear_after_response_or_now_locked(void)
{
	if ((atomic_get(&smp_processing) != 0 ||
	     atomic_get(&response_send_active) != 0) &&
	    session.active) {
		session_clear_pending = true;
		return;
	}

	session_clear_locked();
}

/* -------------------------------------------------------------------------- */
/* Session And SMP Processing                                                 */
/* -------------------------------------------------------------------------- */

static bool session_expired_locked(int64_t now_ms)
{
	return session.active &&
	       now_ms - session.last_active_ms >
		       CONFIG_MBS_MANAGEMENT_SESSION_TIMEOUT_MS;
}

static bool candidate_expired_locked(int64_t now_ms)
{
	return candidate.active &&
	       now_ms - candidate.last_active_ms > MANAGEMENT_SMP_TIMEOUT_MS;
}

static bool session_peer_matches(
	const struct management_session *state,
	const struct management_frame_header *hdr,
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE])
{
	return state->active && hdr->session_id == state->session_id &&
	       memcmp(hdr->target_id, state->target_id,
		      MBS_MANAGEMENT_TARGET_ID_SIZE) == 0 &&
	       memcmp(public_key, state->peer_public_key,
		      MBS_MESHCORE_PUBLIC_KEY_SIZE) == 0;
}

static void session_snapshot_get_locked(
	struct management_session_snapshot *snapshot)
{
	memcpy(snapshot->target_id, session.target_id,
	       sizeof(snapshot->target_id));
	memcpy(snapshot->peer_public_key, session.peer_public_key,
	       sizeof(snapshot->peer_public_key));
	memcpy(snapshot->key, session.key, sizeof(snapshot->key));
	snapshot->session_id = session.session_id;
	snapshot->direct_only = session.direct_only;
	snapshot->return_route = session.return_route;
}

static void candidate_promote_locked(void)
{
	management_secure_wipe(&session, sizeof(session));
	memcpy(&session, &candidate, sizeof(session));
	session.established = true;
	session.last_active_ms = k_uptime_get();
	candidate_clear_locked();
	packet_state_clear_locked();
	session_clear_pending = false;
}

static void expired_state_clear_locked(int64_t now_ms)
{
	if (session_expired_locked(now_ms)) {
		active_session_clear_locked();
	}

	if (candidate_expired_locked(now_ms)) {
		candidate_clear_locked();
	}
}

static int session_challenge_send(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE],
	uint32_t session_id,
	const uint8_t client_nonce[MBS_MANAGEMENT_NONCE_SIZE],
	const uint8_t server_nonce[MBS_MANAGEMENT_NONCE_SIZE])
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + 2U * MBS_MANAGEMENT_NONCE_SIZE];
	struct management_frame_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_CHALLENGE,
		.session_id = session_id,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = 2U * MBS_MANAGEMENT_NONCE_SIZE,
	};

	memcpy(hdr.target_id, target_id, MBS_MANAGEMENT_TARGET_ID_SIZE);
	management_frame_header_write(frame, &hdr);
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE], client_nonce,
	       MBS_MANAGEMENT_NONCE_SIZE);
	memcpy(&frame[MBS_MANAGEMENT_HEADER_SIZE + MBS_MANAGEMENT_NONCE_SIZE],
	       server_nonce, MBS_MANAGEMENT_NONCE_SIZE);

	LOG_DBG("Management SESSION_CHALLENGE send: session=%u device=%02x%02x%02x%02x",
		(unsigned int)session_id, target_id[0], target_id[1], target_id[2],
		target_id[3]);

	return mbs_meshcore_anon_data_send_delayed(
		public_key, frame, sizeof(frame),
		CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
}

static int request_fragment_ack_send(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const struct management_frame_header *request_hdr)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + 1U +
		      MBS_MANAGEMENT_AEAD_TAG_SIZE];
	struct management_session_snapshot snapshot;
	struct management_frame_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST_ACK,
	};
	uint8_t plain;
	int rc;

	if (public_key == NULL || request_hdr == NULL ||
	    request_hdr->frag_index + 1U >= request_hdr->frag_count) {
		return -EINVAL;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (!session_peer_matches(&session, request_hdr, public_key)) {
		k_mutex_unlock(&management_mutex);
		return -ESTALE;
	}
	session_snapshot_get_locked(&snapshot);
	session.last_active_ms = k_uptime_get();
	k_mutex_unlock(&management_mutex);

	hdr.session_id = request_hdr->session_id;
	hdr.seq = request_hdr->seq;
	hdr.frag_index = request_hdr->frag_index;
	hdr.frag_count = request_hdr->frag_count;
	hdr.payload_len = sizeof(plain) + MBS_MANAGEMENT_AEAD_TAG_SIZE;
	memcpy(hdr.target_id, request_hdr->target_id, sizeof(hdr.target_id));
	plain = request_hdr->frag_index;
	management_frame_header_write(frame, &hdr);
	rc = management_aead_encrypt(snapshot.key, frame, &plain, sizeof(plain));
	if (rc == 0) {
		if (snapshot.direct_only && snapshot.return_route.present) {
			rc = mbs_meshcore_anon_data_send_via_path_delayed(
				public_key, frame, sizeof(frame),
				snapshot.return_route.path,
				snapshot.return_route.path_byte_len,
				snapshot.return_route.path_hash_size,
				CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
		} else {
			rc = mbs_meshcore_anon_data_send_delayed(
				public_key, frame, sizeof(frame),
				CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
		}
	}
	LOG_DBG("Management DATA_REQUEST_ACK send: seq=%u frag=%u/%u rc=%d",
		(unsigned int)hdr.seq, (unsigned int)(hdr.frag_index + 1U),
		(unsigned int)hdr.frag_count, rc);
	management_secure_wipe(&snapshot, sizeof(snapshot));
	management_secure_wipe(frame, sizeof(frame));
	return rc;
}

static int session_reject_send(
	const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
	const uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE],
	uint32_t session_id, int status,
	const struct management_return_route *return_route)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + sizeof(uint32_t)];
	struct management_frame_header hdr = {
		.type = MBS_MANAGEMENT_FRAME_TYPE_SESSION_REJECT,
		.session_id = session_id,
		.seq = 0U,
		.frag_index = 0U,
		.frag_count = 1U,
		.payload_len = sizeof(uint32_t),
	};

	if (public_key == NULL || target_id == NULL || session_id == 0U ||
	    status >= 0) {
		return -EINVAL;
	}

	memcpy(hdr.target_id, target_id, sizeof(hdr.target_id));
	management_frame_header_write(frame, &hdr);
	sys_put_le32((uint32_t)status,
		     &frame[MBS_MANAGEMENT_HEADER_SIZE]);

	if (return_route != NULL) {
		if (!return_route->present) {
			return -EHOSTUNREACH;
		}
		return mbs_meshcore_anon_data_send_via_path_delayed(
			public_key, frame, sizeof(frame), return_route->path,
			return_route->path_byte_len, return_route->path_hash_size,
			CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
	}
	return mbs_meshcore_anon_data_send_delayed(
		public_key, frame, sizeof(frame),
		CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
}

static int session_init_handle(const struct management_frame_header *hdr, const uint8_t *payload)
{
	struct management_return_route return_route = {0};
	uint8_t target_id[MBS_MANAGEMENT_TARGET_ID_SIZE];
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t server_nonce[MBS_MANAGEMENT_NONCE_SIZE];
	uint8_t secret[MBS_MANAGEMENT_SECRET_MAX_LEN];
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	size_t secret_len = 0U;
	uint32_t session_id;
	int64_t now_ms = k_uptime_get();
	int rc;

	if (hdr == NULL || payload == NULL ||
	    hdr->payload_len < MBS_MANAGEMENT_NONCE_SIZE +
				 MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE ||
	    hdr->session_id != 0U || hdr->seq != 0U ||
	    hdr->frag_index != 0U || hdr->frag_count != 1U) {
		LOG_ERR("Management SESSION_INIT rejected: invalid header");
		return -EINVAL;
	}
	{
		const size_t route_off = MBS_MANAGEMENT_NONCE_SIZE;
		const uint8_t route_flags = payload[route_off];

		return_route.present =
			(route_flags & MBS_MANAGEMENT_SESSION_ROUTE_PRESENT) != 0U;
		return_route.path_hash_size = payload[route_off + 1U];
		return_route.path_byte_len = payload[route_off + 2U];
		if ((route_flags & ~MBS_MANAGEMENT_SESSION_ROUTE_PRESENT) != 0U ||
		    hdr->payload_len != route_off +
					MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE +
					return_route.path_byte_len ||
		    (!return_route.present &&
		     (return_route.path_hash_size != 0U ||
		      return_route.path_byte_len != 0U)) ||
		    (return_route.present &&
		     (return_route.path_hash_size == 0U ||
		      return_route.path_hash_size > 3U ||
		      return_route.path_byte_len > sizeof(return_route.path) ||
		      return_route.path_byte_len %
			      return_route.path_hash_size != 0U))) {
			return -EINVAL;
		}
		if (return_route.path_byte_len > 0U) {
			memcpy(return_route.path,
			       &payload[route_off +
					MBS_MANAGEMENT_SESSION_ROUTE_HEADER_SIZE],
			       return_route.path_byte_len);
		}
	}
	if (!anon_response_context_snapshot(public_key, NULL)) {
		return -EHOSTUNREACH;
	}

	rc = local_target_id_get(target_id);
	if (rc != 0) {
		LOG_ERR("Management SESSION_INIT rejected: local target id rc=%d", rc);
		return rc;
	}
	if (memcmp(hdr->target_id, target_id, MBS_MANAGEMENT_TARGET_ID_SIZE) != 0) {
		LOG_ERR("Management SESSION_INIT rejected: target mismatch local=%02x%02x%02x%02x remote=%02x%02x%02x%02x",
			target_id[0], target_id[1], target_id[2], target_id[3],
			hdr->target_id[0], hdr->target_id[1], hdr->target_id[2],
			hdr->target_id[3]);
		return -EHOSTUNREACH;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	expired_state_clear_locked(now_ms);
	if (CONFIG_MBS_MANAGEMENT_SESSION_INIT_COOLDOWN_MS > 0 &&
	    last_session_init_ms != 0 &&
	    now_ms - last_session_init_ms < CONFIG_MBS_MANAGEMENT_SESSION_INIT_COOLDOWN_MS) {
		k_mutex_unlock(&management_mutex);
		LOG_ERR("Management SESSION_INIT rejected: cooldown");
		return -EAGAIN;
	}

	rc = secret_effective_get(secret, &secret_len);
	if (rc != 0) {
		k_mutex_unlock(&management_mutex);
		LOG_ERR("Management SESSION_INIT rejected: secret rc=%d", rc);
		return rc;
	}
	last_session_init_ms = now_ms;
	k_mutex_unlock(&management_mutex);

	rc = management_random_get(server_nonce, sizeof(server_nonce));
	if (rc != 0) {
		management_secure_wipe(secret, sizeof(secret));
		LOG_ERR("Management SESSION_INIT rejected: server nonce rc=%d", rc);
		return rc;
	}
	do {
		rc = management_random_get((uint8_t *)&session_id, sizeof(session_id));
		if (rc != 0) {
			management_secure_wipe(secret, sizeof(secret));
			management_secure_wipe(server_nonce, sizeof(server_nonce));
			LOG_ERR("Management SESSION_INIT rejected: session id rc=%d", rc);
			return rc;
		}
	} while (session_id == 0U);

	rc = management_session_key_derive(
		secret, secret_len, target_id, payload, server_nonce, session_id,
		&return_route, key);
	management_secure_wipe(secret, sizeof(secret));
	if (rc != 0) {
		management_secure_wipe(server_nonce, sizeof(server_nonce));
		management_secure_wipe(key, sizeof(key));
		LOG_DBG("Management SESSION_INIT rejected: session key rc=%d", rc);
		return rc;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	expired_state_clear_locked(k_uptime_get());
	candidate_clear_locked();
	candidate.active = true;
	memcpy(candidate.target_id, target_id, sizeof(candidate.target_id));
	memcpy(candidate.peer_public_key, public_key,
	       sizeof(candidate.peer_public_key));
	candidate.session_id = session_id;
	candidate.last_active_ms = now_ms;
	memcpy(candidate.key, key, sizeof(candidate.key));
	candidate.return_route = return_route;
	k_mutex_unlock(&management_mutex);

	management_secure_wipe(key, sizeof(key));
	rc = session_challenge_send(public_key, target_id, session_id, payload,
				    server_nonce);
	if (rc != 0) {
		k_mutex_lock(&management_mutex, K_FOREVER);
		if (candidate.active && candidate.session_id == session_id) {
			candidate_clear_locked();
		}
		k_mutex_unlock(&management_mutex);
	}
	management_secure_wipe(server_nonce, sizeof(server_nonce));
	management_secure_wipe(public_key, sizeof(public_key));
	LOG_DBG("Management SESSION_INIT handled: session=%u rc=%d",
		(unsigned int)session_id, rc);
	return rc;
}

static int data_fragment_store_locked(const struct management_frame_header *hdr,
				      const uint8_t *plain, size_t plain_len,
				      bool *complete, bool *cached_response,
				      uint8_t *smp, size_t *smp_len)
{
	size_t offset;

	if (hdr == NULL || plain == NULL || complete == NULL ||
	    cached_response == NULL || smp == NULL || smp_len == NULL ||
	    plain_len > MANAGEMENT_PLAINTEXT_MAX_LEN) {
		return -EINVAL;
	}
	*complete = false;
	*cached_response = false;
	if (session_expired_locked(k_uptime_get())) {
		active_session_clear_locked();
		return -ETIMEDOUT;
	}
	if (!session.active || hdr->session_id != session.session_id ||
	    memcmp(hdr->target_id, session.target_id,
		   MBS_MANAGEMENT_TARGET_ID_SIZE) != 0) {
		return -EACCES;
	}
	if (hdr->seq < session.last_rx_seq) {
		return -EALREADY;
	}
	if (hdr->seq == session.last_rx_seq) {
		if (packet_state_kind == MANAGEMENT_PACKET_STATE_RESPONSE_CACHE &&
		    packet_state != NULL && packet_state->response_cache.valid &&
		    packet_state->response_cache.session_id == session.session_id &&
		    packet_state->response_cache.request_seq == hdr->seq &&
		    hdr->frag_count == session.last_rx_frag_count) {
			session.last_active_ms = k_uptime_get();
			*cached_response =
				hdr->frag_index + 1U == hdr->frag_count;
			return 0;
		}
		return -EALREADY;
	}
	session.established = true;
	if (packet_state_kind != MANAGEMENT_PACKET_STATE_REASSEMBLY ||
	    packet_state == NULL || !packet_state->reassembly.active ||
	    packet_state->reassembly.seq != hdr->seq) {
		packet_state_clear_locked();
		packet_state = k_calloc(1, sizeof(*packet_state));
		if (packet_state == NULL) {
			return -ENOMEM;
		}
		packet_state_kind = MANAGEMENT_PACKET_STATE_REASSEMBLY;
		packet_state->reassembly.active = true;
		packet_state->reassembly.seq = hdr->seq;
		packet_state->reassembly.frag_count = hdr->frag_count;
	} else if (packet_state->reassembly.frag_count != hdr->frag_count) {
		packet_state_clear_locked();
		return -EINVAL;
	}
	if ((packet_state->reassembly.received_mask & BIT(hdr->frag_index)) != 0U) {
		session.last_active_ms = k_uptime_get();
		return 0;
	}

	offset = hdr->frag_index * MANAGEMENT_PLAINTEXT_MAX_LEN;
	if (offset + plain_len > sizeof(packet_state->reassembly.data)) {
		packet_state_clear_locked();
		return -EMSGSIZE;
	}

	memcpy(&packet_state->reassembly.data[offset], plain, plain_len);
	packet_state->reassembly.len[hdr->frag_index] = plain_len;
	packet_state->reassembly.received_mask |= BIT(hdr->frag_index);
	packet_state->reassembly.received_count++;
	session.last_active_ms = k_uptime_get();

	if (packet_state->reassembly.received_count !=
	    packet_state->reassembly.frag_count) {
		return 0;
	}

	*smp_len = 0U;
	for (uint8_t i = 0; i < packet_state->reassembly.frag_count; i++) {
		offset = i * MANAGEMENT_PLAINTEXT_MAX_LEN;
		if (*smp_len + packet_state->reassembly.len[i] >
		    MANAGEMENT_SMP_EFFECTIVE_MAX_LEN) {
			packet_state_clear_locked();
			return -EMSGSIZE;
		}
		memcpy(&smp[*smp_len], &packet_state->reassembly.data[offset],
		       packet_state->reassembly.len[i]);
		*smp_len += packet_state->reassembly.len[i];
	}

	session.last_rx_seq = hdr->seq;
	session.last_rx_frag_count = hdr->frag_count;
	packet_state_clear_locked();
	*complete = true;
	return 0;
}

static int smp_packet_process(const uint8_t *packet, size_t packet_len)
{
	struct cbor_nb_reader reader;
	struct cbor_nb_writer writer;
	struct smp_streamer streamer = {
		.reader = &reader,
		.writer = &writer,
		.smpt = &management_smp_transport,
	};
	struct net_buf *nb;
	int rc;

	if (packet == NULL || packet_len < MGMT_HDR_SIZE) {
		return -EINVAL;
	}
	if (packet_len > mbs_management_smp_effective_max_len_get()) {
		return -EMSGSIZE;
	}
	if (packet_len != MGMT_HDR_SIZE + sys_get_be16(&packet[2])) {
		LOG_DBG("Management SMP length mismatch: actual=%u declared=%u",
			(unsigned int)packet_len,
			(unsigned int)(MGMT_HDR_SIZE + sys_get_be16(&packet[2])));
		return -EMSGSIZE;
	}
	LOG_DBG("Management SMP request process: len=%u group=%u command=%u",
		(unsigned int)packet_len, (unsigned int)sys_get_be16(&packet[4]),
		(unsigned int)packet[7]);

	nb = smp_packet_alloc();
	if (nb == NULL) {
		return -ENOMEM;
	}
	if (net_buf_tailroom(nb) < packet_len) {
		smp_packet_free(nb);
		return -EMSGSIZE;
	}

	net_buf_add_mem(nb, packet, packet_len);
	atomic_set(&smp_processing, 1);
	rc = smp_process_request_packet(&streamer, nb);
	atomic_clear(&smp_processing);

	return rc == MGMT_ERR_EOK ? 0 : -EIO;
}

static int data_request_handle(const uint8_t *frame,
			       const struct management_frame_header *hdr,
			       const uint8_t *payload)
{
	uint8_t plain[MANAGEMENT_PLAINTEXT_MAX_LEN];
	uint8_t smp[MANAGEMENT_SMP_EFFECTIVE_MAX_LEN];
	size_t plain_len = 0U;
	size_t smp_len = 0U;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t key[MBS_MANAGEMENT_SESSION_KEY_SIZE];
	struct management_return_route reject_route = {0};
	uint8_t inbound_route = MBS_MESHCORE_ROUTE_UNSPECIFIED;
	bool complete = false;
	bool cached_response = false;
	bool selected_candidate = false;
	bool reject_direct_only = false;
	int reject_status = 0;
	int rc;

	if (frame == NULL || hdr == NULL || payload == NULL ||
	    hdr->payload_len <= MBS_MANAGEMENT_AEAD_TAG_SIZE ||
	    hdr->payload_len >
		    MANAGEMENT_PLAINTEXT_MAX_LEN + MBS_MANAGEMENT_AEAD_TAG_SIZE) {
		return -EINVAL;
	}
	if (!anon_response_context_snapshot(public_key, &inbound_route)) {
		return -EHOSTUNREACH;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	expired_state_clear_locked(k_uptime_get());
	if (session_peer_matches(&session, hdr, public_key)) {
		memcpy(key, session.key, sizeof(key));
	} else if (session_peer_matches(&candidate, hdr, public_key)) {
		memcpy(key, candidate.key, sizeof(key));
		selected_candidate = true;
	} else {
		k_mutex_unlock(&management_mutex);
		(void)session_reject_send(public_key, hdr->target_id,
					  hdr->session_id, -ESTALE, NULL);
		management_secure_wipe(public_key, sizeof(public_key));
		return -ESTALE;
	}
	k_mutex_unlock(&management_mutex);

	rc = management_aead_decrypt(key, frame, hdr, payload, plain, &plain_len);
	management_secure_wipe(key, sizeof(key));
	if (rc != 0) {
		if (selected_candidate) {
			k_mutex_lock(&management_mutex, K_FOREVER);
			if (session_peer_matches(&candidate, hdr, public_key)) {
				candidate_clear_locked();
			}
			k_mutex_unlock(&management_mutex);
		}
		(void)session_reject_send(public_key, hdr->target_id,
					  hdr->session_id, -EACCES, NULL);
		management_secure_wipe(plain, sizeof(plain));
		management_secure_wipe(public_key, sizeof(public_key));
		return rc;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (hdr->seq == 0U) {
		reject_status = -EINVAL;
	} else if (selected_candidate) {
		if (!session_peer_matches(&candidate, hdr, public_key)) {
			reject_status = -ESTALE;
		} else if (atomic_get(&smp_processing) != 0 ||
			   atomic_get(&response_send_active) != 0) {
			candidate_clear_locked();
			reject_status = -EBUSY;
		} else {
			candidate_promote_locked();
		}
	} else if (!session_peer_matches(&session, hdr, public_key)) {
		reject_status = -ESTALE;
	} else if (atomic_get(&response_send_active) != 0 &&
		   hdr->seq > session.last_rx_seq) {
		reject_status = -EBUSY;
	}
	if (reject_status == 0) {
		if (hdr->frag_index == 0U) {
			if (plain_len < MGMT_HDR_SIZE) {
				reject_status = -EINVAL;
			} else {
				session.direct_only =
					sys_get_be16(&plain[4]) ==
					meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE;
			}
		}
		if (reject_status == 0 && session.direct_only &&
		    (inbound_route != MBS_MESHCORE_ROUTE_DIRECT ||
		     !session.return_route.present)) {
			reject_status = -EHOSTUNREACH;
		}
		if (reject_status == 0) {
			rc = data_fragment_store_locked(
				hdr, plain, plain_len, &complete, &cached_response,
				smp, &smp_len);
		} else {
			rc = reject_status;
		}
	} else {
		rc = reject_status;
	}
	if (session_peer_matches(&session, hdr, public_key)) {
		reject_direct_only = session.direct_only;
		if (session.return_route.present) {
			reject_route = session.return_route;
		}
	}
	k_mutex_unlock(&management_mutex);
	management_secure_wipe(plain, sizeof(plain));
	if (reject_status != 0) {
		(void)session_reject_send(public_key, hdr->target_id,
					  hdr->session_id, reject_status,
					  reject_direct_only ? &reject_route : NULL);
	} else if (rc == -EALREADY || rc == -ETIMEDOUT || rc == -EACCES) {
		(void)session_reject_send(public_key, hdr->target_id,
					  hdr->session_id, -ESTALE,
					  reject_direct_only ? &reject_route : NULL);
	} else if (rc == 0 && !complete && !cached_response &&
		   hdr->frag_index + 1U < hdr->frag_count) {
		rc = request_fragment_ack_send(public_key, hdr);
	}
	management_secure_wipe(public_key, sizeof(public_key));
	if (rc != 0 || !complete) {
		management_secure_wipe(smp, sizeof(smp));
		if (rc == 0 && cached_response) {
			return response_cache_resend(hdr->seq);
		}
		return rc;
	}

	if (!remote_smp_packet_allowed(smp, smp_len)) {
		management_secure_wipe(smp, sizeof(smp));
		k_mutex_lock(&management_mutex, K_FOREVER);
		session_clear_after_response_or_now_locked();
		k_mutex_unlock(&management_mutex);
		return -EACCES;
	}

	rc = smp_packet_process(smp, smp_len);
	management_secure_wipe(smp, sizeof(smp));
	return rc;
}

/* -------------------------------------------------------------------------- */
/* SMP Transport Output                                                       */
/* -------------------------------------------------------------------------- */

static int response_frame_send(const struct management_session_snapshot *snapshot,
			       const uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE],
			       uint32_t seq, uint8_t frag_index, uint8_t frag_count,
			       const uint8_t *plain, size_t plain_len)
{
	uint8_t frame[MBS_MANAGEMENT_HEADER_SIZE + MANAGEMENT_PLAINTEXT_MAX_LEN +
		      MBS_MANAGEMENT_AEAD_TAG_SIZE];
	struct management_frame_header hdr;
	int rc;

	if (snapshot == NULL || public_key == NULL) {
		return -EINVAL;
	}

	hdr.type = MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE;
	hdr.session_id = snapshot->session_id;
	hdr.seq = seq;
	hdr.frag_index = frag_index;
	hdr.frag_count = frag_count;
	hdr.payload_len = plain_len + MBS_MANAGEMENT_AEAD_TAG_SIZE;
	memcpy(hdr.target_id, snapshot->target_id, sizeof(hdr.target_id));
	management_frame_header_write(frame, &hdr);

	rc = management_aead_encrypt(snapshot->key, frame, plain, plain_len);
	if (rc != 0) {
		management_secure_wipe(frame, sizeof(frame));
		return rc;
	}

	if (snapshot->direct_only && snapshot->return_route.present) {
		rc = mbs_meshcore_anon_data_send_via_path_delayed(
			public_key, frame,
			MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len,
			snapshot->return_route.path,
			snapshot->return_route.path_byte_len,
			snapshot->return_route.path_hash_size,
			CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
	} else {
		rc = mbs_meshcore_anon_data_send_delayed(
			public_key, frame,
			MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len,
			CONFIG_MBS_MANAGEMENT_REPLY_DELAY_MS);
	}
	LOG_DBG("Management DATA_RESPONSE send: target=%02x%02x%02x%02x seq=%u frag=%u/%u len=%u rc=%d",
		public_key[0], public_key[1], public_key[2], public_key[3],
		(unsigned int)seq, (unsigned int)(frag_index + 1U),
		(unsigned int)frag_count,
		(unsigned int)(MBS_MANAGEMENT_HEADER_SIZE + hdr.payload_len),
		rc);
	management_secure_wipe(frame, sizeof(frame));
	return rc;
}

static int response_fragment_ack_handle(
	const uint8_t *frame, const struct management_frame_header *hdr,
	const uint8_t *payload)
{
	struct management_session_snapshot snapshot;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t plain[1];
	size_t plain_len = 0U;
	uint8_t expected_frag;
	bool final_ack = false;
	bool firmware_activate = false;
	int rc;

	if (frame == NULL || hdr == NULL || payload == NULL ||
	    hdr->type != MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK ||
	    hdr->payload_len != sizeof(plain) +
			       MBS_MANAGEMENT_AEAD_TAG_SIZE ||
	    !anon_response_context_snapshot(public_key, NULL)) {
		return -EINVAL;
	}

	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	if (!response_send_state.active || !response_send_state.waiting_ack ||
	    response_send_state.next_frag == 0U ||
	    response_send_state.next_frag > response_send_state.frag_count) {
		k_mutex_unlock(&management_response_send_mutex);
		management_secure_wipe(public_key, sizeof(public_key));
		return -ESTALE;
	}
	expected_frag = response_send_state.next_frag - 1U;
	if (hdr->session_id != response_send_state.snapshot.session_id ||
	    hdr->seq != response_send_state.seq ||
	    hdr->frag_index != expected_frag ||
	    hdr->frag_count != response_send_state.frag_count ||
	    memcmp(hdr->target_id, response_send_state.snapshot.target_id,
		   sizeof(hdr->target_id)) != 0 ||
	    memcmp(public_key, response_send_state.public_key,
		   sizeof(public_key)) != 0) {
		k_mutex_unlock(&management_response_send_mutex);
		management_secure_wipe(public_key, sizeof(public_key));
		return -EINVAL;
	}
	memcpy(&snapshot, &response_send_state.snapshot, sizeof(snapshot));
	k_mutex_unlock(&management_response_send_mutex);

	rc = management_aead_decrypt(snapshot.key, frame, hdr, payload, plain,
				     &plain_len);
	if (rc != 0 || plain_len != sizeof(plain) || plain[0] != expected_frag) {
		management_secure_wipe(&snapshot, sizeof(snapshot));
		management_secure_wipe(public_key, sizeof(public_key));
		management_secure_wipe(plain, sizeof(plain));
		return rc != 0 ? rc : -EINVAL;
	}

	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	if (!response_send_state.active || !response_send_state.waiting_ack ||
	    response_send_state.snapshot.session_id != snapshot.session_id ||
	    response_send_state.seq != hdr->seq ||
	    response_send_state.next_frag != expected_frag + 1U) {
		rc = -ESTALE;
	} else {
		final_ack = response_send_state.next_frag ==
			    response_send_state.frag_count;
		firmware_activate = response_send_state.firmware_activate;
		response_send_state.waiting_ack = false;
		rc = 0;
	}
	k_mutex_unlock(&management_response_send_mutex);

	if (rc == 0) {
		(void)k_work_cancel_delayable(&response_ack_timeout_work);
		k_mutex_lock(&management_mutex, K_FOREVER);
		if (session.active && session.session_id == snapshot.session_id &&
		    memcmp(session.peer_public_key, public_key,
			   sizeof(public_key)) == 0) {
			session.last_active_ms = k_uptime_get();
		}
		k_mutex_unlock(&management_mutex);
		if (final_ack) {
#if defined(CONFIG_MBS_FIRMWARE)
			if (firmware_activate) {
				int handoff_rc =
					mbs_firmware_delta_activate_handoff();

				if (handoff_rc != 0) {
					LOG_WRN("Firmware activation handoff failed: %d",
						handoff_rc);
				}
			}
#else
			ARG_UNUSED(firmware_activate);
#endif
			response_send_finish(0);
		} else {
			rc = k_work_reschedule(
				&response_send_work,
				K_MSEC(CONFIG_MBS_MANAGEMENT_RESPONSE_FRAGMENT_DELAY_MS));
			if (rc >= 0) {
				rc = 0;
			}
		}
	}
	LOG_DBG("Management DATA_RESPONSE_ACK receive: seq=%u frag=%u/%u rc=%d",
		(unsigned int)hdr->seq,
		(unsigned int)(hdr->frag_index + 1U),
		(unsigned int)hdr->frag_count, rc);

	management_secure_wipe(&snapshot, sizeof(snapshot));
	management_secure_wipe(public_key, sizeof(public_key));
	management_secure_wipe(plain, sizeof(plain));
	return rc;
}

static void response_send_finish(int rc)
{
	(void)k_work_cancel_delayable(&response_ack_timeout_work);
	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	response_send_state.active = false;
	management_secure_wipe(&response_send_state.snapshot,
		    sizeof(response_send_state.snapshot));
	management_secure_wipe(response_send_state.public_key,
		    sizeof(response_send_state.public_key));
	response_send_state.len = 0U;
	response_send_state.seq = 0U;
	response_send_state.frag_count = 0U;
	response_send_state.next_frag = 0U;
	response_send_state.waiting_ack = false;
	response_send_state.restart_requested = false;
	response_send_state.firmware_activate = false;
	atomic_clear(&response_send_active);
	k_mutex_unlock(&management_response_send_mutex);

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (session_clear_pending) {
		session_clear_locked();
	}
	k_mutex_unlock(&management_mutex);

	if (rc != 0) {
		LOG_WRN("Management DATA_RESPONSE send failed: rc=%d", rc);
	}
}

static void response_ack_timeout_work_handler(struct k_work *work)
{
	bool restart = false;
	int rc;

	ARG_UNUSED(work);

	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	if (response_send_state.active &&
	    response_send_state.restart_requested) {
		response_send_state.next_frag = 0U;
		response_send_state.waiting_ack = false;
		response_send_state.restart_requested = false;
		restart = true;
	}
	k_mutex_unlock(&management_response_send_mutex);

	if (restart) {
		rc = k_work_reschedule(&response_send_work, K_NO_WAIT);
		if (rc < 0) {
			response_send_finish(rc);
		}
		return;
	}

	response_send_finish(-ETIMEDOUT);
}

static void response_send_work_handler(struct k_work *work)
{
	struct management_session_snapshot snapshot;
	uint8_t public_key[MBS_MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t plain[MANAGEMENT_PLAINTEXT_MAX_LEN];
	size_t frag_len = 0U;
	size_t offset;
	uint32_t seq;
	uint8_t frag_index;
	uint8_t frag_count;
	int rc;

	ARG_UNUSED(work);

	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	if (!response_send_state.active ||
	    response_send_state.waiting_ack ||
	    response_send_state.next_frag >= response_send_state.frag_count) {
		k_mutex_unlock(&management_response_send_mutex);
		return;
	}

	memcpy(&snapshot, &response_send_state.snapshot, sizeof(snapshot));
	memcpy(public_key, response_send_state.public_key, sizeof(public_key));
	seq = response_send_state.seq;
	frag_index = response_send_state.next_frag;
	frag_count = response_send_state.frag_count;
	offset = frag_index * MANAGEMENT_PLAINTEXT_MAX_LEN;
	frag_len = MIN(response_send_state.len - offset,
		       (size_t)MANAGEMENT_PLAINTEXT_MAX_LEN);
	response_send_state.next_frag++;
	response_send_state.waiting_ack = true;
	k_mutex_unlock(&management_response_send_mutex);

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (packet_state_kind != MANAGEMENT_PACKET_STATE_RESPONSE_CACHE ||
	    packet_state == NULL || !packet_state->response_cache.valid ||
	    packet_state->response_cache.session_id != snapshot.session_id ||
	    packet_state->response_cache.request_seq != seq ||
	    offset + frag_len > packet_state->response_cache.len) {
		rc = -ESTALE;
	} else {
		memcpy(plain, &packet_state->response_cache.response[offset], frag_len);
		rc = 0;
	}
	k_mutex_unlock(&management_mutex);

	if (rc == 0) {
		rc = response_frame_send(&snapshot, public_key, seq, frag_index,
					 frag_count, plain, frag_len);
	}
	management_secure_wipe(&snapshot, sizeof(snapshot));
	management_secure_wipe(public_key, sizeof(public_key));
	management_secure_wipe(plain, sizeof(plain));

	if (rc != 0) {
		response_send_finish(rc);
		return;
	}

	(void)k_work_reschedule(&response_ack_timeout_work,
				K_MSEC(MANAGEMENT_FRAGMENT_ACK_TIMEOUT_MS));
}

static int response_send_schedule(
	const struct management_session_snapshot *snapshot, uint32_t seq,
	size_t len, bool firmware_activate)
{
	uint8_t frag_count;
	bool restart = false;
	int rc;

	if (snapshot == NULL || len == 0U) {
		return -EINVAL;
	}
	if (len > mbs_management_smp_effective_max_len_get()) {
		return -EMSGSIZE;
	}
	frag_count = DIV_ROUND_UP(len, MANAGEMENT_PLAINTEXT_MAX_LEN);
	if (frag_count == 0U ||
	    frag_count > CONFIG_MBS_MANAGEMENT_MAX_FRAGMENTS) {
		return -EMSGSIZE;
	}

	k_mutex_lock(&management_response_send_mutex, K_FOREVER);
	if (response_send_state.active) {
		if (response_send_state.waiting_ack &&
		    response_send_state.snapshot.session_id ==
			    snapshot->session_id &&
		    response_send_state.seq == seq &&
		    response_send_state.len == len &&
		    memcmp(response_send_state.public_key,
			   snapshot->peer_public_key,
			   sizeof(response_send_state.public_key)) == 0) {
			response_send_state.restart_requested = true;
			restart = true;
		}
		k_mutex_unlock(&management_response_send_mutex);
		if (!restart) {
			return -EBUSY;
		}

		rc = k_work_reschedule(&response_ack_timeout_work, K_NO_WAIT);
		if (rc < 0) {
			k_mutex_lock(&management_response_send_mutex, K_FOREVER);
			if (response_send_state.active &&
			    response_send_state.snapshot.session_id ==
				    snapshot->session_id &&
			    response_send_state.seq == seq) {
				response_send_state.restart_requested = false;
			}
			k_mutex_unlock(&management_response_send_mutex);
			return rc;
		}
		return 0;
	}

	memset(&response_send_state, 0, sizeof(response_send_state));
	response_send_state.active = true;
	memcpy(&response_send_state.snapshot, snapshot,
	       sizeof(*snapshot));
	memcpy(response_send_state.public_key, snapshot->peer_public_key,
	       sizeof(response_send_state.public_key));
	response_send_state.len = len;
	response_send_state.seq = seq;
	response_send_state.frag_count = frag_count;
	response_send_state.firmware_activate = firmware_activate;
	atomic_set(&response_send_active, 1);
	k_mutex_unlock(&management_response_send_mutex);

	rc = k_work_reschedule(&response_send_work, K_NO_WAIT);
	if (rc < 0) {
		response_send_finish(rc);
		return rc;
	}

	return 0;
}

static int response_packet_send_paced(const uint8_t *response, size_t len)
{
	struct management_session_snapshot snapshot;
	uint32_t seq;
	int rc;

	if (response == NULL || len == 0U ||
	    len > mbs_management_smp_effective_max_len_get()) {
		return -EINVAL;
	}

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (!session.active || session.last_rx_seq == 0U) {
		k_mutex_unlock(&management_mutex);
		return -EACCES;
	}

	session_snapshot_get_locked(&snapshot);
	seq = session.last_rx_seq;
	packet_state_clear_locked();
	packet_state = k_calloc(1, sizeof(*packet_state));
	if (packet_state == NULL) {
		k_mutex_unlock(&management_mutex);
		management_secure_wipe(&snapshot, sizeof(snapshot));
		return -ENOMEM;
	}
	packet_state_kind = MANAGEMENT_PACKET_STATE_RESPONSE_CACHE;
	packet_state->response_cache.valid = true;
	packet_state->response_cache.session_id = session.session_id;
	packet_state->response_cache.request_seq = seq;
	packet_state->response_cache.len = len;
	memcpy(packet_state->response_cache.response, response, len);
	session.last_active_ms = k_uptime_get();
	rc = response_send_schedule(
		&snapshot, seq, len,
		len >= MGMT_HDR_SIZE &&
		sys_get_be16(&response[4]) ==
			meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE &&
		response[7] ==
			meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_ACTIVATE);
	k_mutex_unlock(&management_mutex);

	management_secure_wipe(&snapshot, sizeof(snapshot));
	return rc;
}

static int response_cache_resend(uint32_t request_seq)
{
	struct management_session_snapshot snapshot;
	int rc;

	k_mutex_lock(&management_mutex, K_FOREVER);
	if (!session.active ||
	    packet_state_kind != MANAGEMENT_PACKET_STATE_RESPONSE_CACHE ||
	    packet_state == NULL || !packet_state->response_cache.valid ||
	    packet_state->response_cache.session_id != session.session_id ||
	    packet_state->response_cache.request_seq != request_seq) {
		k_mutex_unlock(&management_mutex);
		return -ESTALE;
	}

	session_snapshot_get_locked(&snapshot);
	session.last_active_ms = k_uptime_get();
	rc = response_send_schedule(
		&snapshot, request_seq, packet_state->response_cache.len,
		packet_state->response_cache.len >= MGMT_HDR_SIZE &&
		sys_get_be16(&packet_state->response_cache.response[4]) ==
			meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE &&
		packet_state->response_cache.response[7] ==
			meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_ACTIVATE);
	k_mutex_unlock(&management_mutex);
	management_secure_wipe(&snapshot, sizeof(snapshot));
	return rc;
}

static int management_smp_output(struct net_buf *nb)
{
	struct management_session_snapshot snapshot = {0};
	uint8_t response[MANAGEMENT_SMP_EFFECTIVE_MAX_LEN];
	bool reject = false;
	size_t len;
	int rc;

	if (nb == NULL) {
		return MGMT_ERR_EINVAL;
	}
	len = nb->len;
	if (len > sizeof(response)) {
		smp_packet_free(nb);
		k_mutex_lock(&management_mutex, K_FOREVER);
		if (session.active) {
			session_snapshot_get_locked(&snapshot);
			reject = true;
		}
		k_mutex_unlock(&management_mutex);
		if (reject) {
			(void)session_reject_send(snapshot.peer_public_key,
					  snapshot.target_id,
					  snapshot.session_id, -EMSGSIZE,
					  snapshot.direct_only
						  ? &snapshot.return_route : NULL);
		}
		management_secure_wipe(&snapshot, sizeof(snapshot));
		return MGMT_ERR_EMSGSIZE;
	}

	memcpy(response, nb->data, len);
	smp_packet_free(nb);

	rc = response_packet_send_paced(response, len);
	management_secure_wipe(response, sizeof(response));
	return rc == 0 ? MGMT_ERR_EOK : MGMT_ERR_EUNKNOWN;
}

static uint16_t management_smp_get_mtu(const struct net_buf *nb)
{
	ARG_UNUSED(nb);

	return (uint16_t)mbs_management_smp_effective_max_len_get();
}

static bool remote_smp_packet_allowed(const uint8_t *packet, size_t packet_len)
{
	uint16_t payload_len;
	uint16_t group;
	uint8_t command;
	uint8_t op;

	if (packet == NULL || packet_len < MGMT_HDR_SIZE) {
		return false;
	}

	op = packet[0] & 0x07U;
	payload_len = sys_get_be16(&packet[2]);
	group = sys_get_be16(&packet[4]);
	command = packet[7];
	if ((size_t)payload_len != packet_len - MGMT_HDR_SIZE) {
		return false;
	}

	switch (group) {
	case MGMT_GROUP_ID_OS:
		switch (command) {
		case OS_MGMT_ID_ECHO:
			return op == MGMT_OP_READ || op == MGMT_OP_WRITE;
		case OS_MGMT_ID_MCUMGR_PARAMS:
		case OS_MGMT_ID_INFO:
		case OS_MGMT_ID_BOOTLOADER_INFO:
			return op == MGMT_OP_READ;
		default:
			return false;
		}
	case meshbus_ManagementGroupId_MANAGEMENT_GROUP_ID_MESHBUS_MANAGEMENT:
		return command == meshbus_ManagementCommandId_MANAGEMENT_COMMAND_ID_SECRET &&
		       op == MGMT_OP_WRITE;
	case meshbus_RadioMgmtGroupId_RADIO_MGMT_GROUP_ID_MESHBUS_RADIO:
		if (command == meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_STATUS) {
			return op == MGMT_OP_READ;
		}
		return command == meshbus_RadioMgmtCommandId_RADIO_MGMT_COMMAND_ID_CONFIG &&
		       (op == MGMT_OP_READ || op == MGMT_OP_WRITE);
	case meshbus_PowerMgmtGroupId_POWER_MGMT_GROUP_ID_MESHBUS_POWER:
		if (command == meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_STATUS) {
			return op == MGMT_OP_READ;
		}
		return command == meshbus_PowerMgmtCommandId_POWER_MGMT_COMMAND_ID_CONFIG &&
		       (op == MGMT_OP_READ || op == MGMT_OP_WRITE);
#if defined(CONFIG_MBS_FIRMWARE)
	case meshbus_FirmwareMgmtGroupId_FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE:
		if (command == meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_STATUS) {
			return op == MGMT_OP_READ;
		}
		return command >= meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_BEGIN &&
		       command <= meshbus_FirmwareMgmtCommandId_FIRMWARE_MGMT_COMMAND_ID_DELTA_ABORT &&
		       op == MGMT_OP_WRITE;
#endif
	default:
		return false;
	}
}

static int local_target_id_get(uint8_t out[MBS_MANAGEMENT_TARGET_ID_SIZE])
{
	mbs_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}

	rc = mbs_meshcore_config_get(&cfg);
	if (rc != 0) {
		goto out;
	}
	if (cfg.public_key.size != MBS_MESHCORE_PUBLIC_KEY_SIZE) {
		rc = -ENOENT;
		goto out;
	}

	rc = management_target_id_from_public_key(cfg.public_key.bytes, out);

out:
	management_secure_wipe(&cfg, sizeof(cfg));
	return rc;
}

static int mbs_management_frame_receive(const uint8_t *frame, size_t len)
{
	struct management_frame_header hdr;
	const uint8_t *payload = NULL;
	int rc;

	rc = management_frame_header_parse(frame, len, &hdr, &payload);
	if (rc != 0) {
		LOG_DBG("Management frame rejected: parse len=%u rc=%d",
			(unsigned int)len, rc);
		return rc;
	}

	LOG_DBG("Management frame receive: type=%u session=%u seq=%u len=%u",
		(unsigned int)hdr.type, (unsigned int)hdr.session_id,
		(unsigned int)hdr.seq, (unsigned int)len);

	switch (hdr.type) {
	case MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT:
		rc = session_init_handle(&hdr, payload);
		break;
	case MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST:
		rc = data_request_handle(frame, &hdr, payload);
		break;
	case MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK:
		rc = response_fragment_ack_handle(frame, &hdr, payload);
		break;
	default:
		rc = -ENOTSUP;
		break;
	}

	if (rc != 0) {
		LOG_DBG("Management frame handle failed: type=%u rc=%d",
			(unsigned int)hdr.type, rc);
	}
	return rc;
}

static void management_session_request_frame_receive(const uint8_t *frame, size_t len)
{
	struct management_frame_header hdr;
	const uint8_t *payload = NULL;
	int rc;

	rc = management_frame_header_parse(frame, len, &hdr, &payload);
	if (rc != 0) {
		if (frame != NULL && len >= MBS_MANAGEMENT_HEADER_SIZE &&
		    frame[MANAGEMENT_OFF_MAGIC0] == MBS_MANAGEMENT_MAGIC0 &&
		    frame[MANAGEMENT_OFF_MAGIC1] == MBS_MANAGEMENT_MAGIC1 &&
		    frame[MANAGEMENT_OFF_VERSION] == MBS_MANAGEMENT_VERSION) {
			LOG_DBG("Management request frame rejected: len=%u rc=%d",
				(unsigned int)len, rc);
		}
		return;
	}

	if (hdr.type != MBS_MANAGEMENT_FRAME_TYPE_SESSION_INIT &&
	    hdr.type != MBS_MANAGEMENT_FRAME_TYPE_DATA_REQUEST &&
	    hdr.type != MBS_MANAGEMENT_FRAME_TYPE_DATA_RESPONSE_ACK) {
		return;
	}

	(void)mbs_management_frame_receive(frame, len);
}

void management_session_anon_data_response_handle(
	const struct mbs_meshcore_anon_data_response_event *event)
{
	if (event == NULL || event->payload_len == 0U) {
		return;
	}

	anon_response_context_set(event);
	management_session_request_frame_receive(event->payload, event->payload_len);
	anon_response_context_clear();
}

void management_sessions_clear(void)
{
	/* Session teardown is an explicit cancellation boundary.  Do not leave a
	 * cached response waiting for an ACK after its encryption state is gone.
	 */
	response_send_finish(0);
	k_mutex_lock(&management_mutex, K_FOREVER);
	session_clear_after_response_or_now_locked();
	k_mutex_unlock(&management_mutex);
#if defined(CONFIG_MBS_MANAGEMENT_OPERATOR)
	management_smp_session_clear();
#endif
}

void management_session_init(void)
{
	k_work_init_delayable(&response_send_work, response_send_work_handler);
	k_work_init_delayable(&response_ack_timeout_work,
			      response_ack_timeout_work_handler);
	management_smp_transport.functions.output = management_smp_output;
	management_smp_transport.functions.get_mtu = management_smp_get_mtu;
	(void)smp_transport_init(&management_smp_transport);
}
