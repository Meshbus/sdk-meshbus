// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "reference_chat_harness.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

extern "C" {
#include "meshcore/types.h"
#include "meshcore_packet.h"
}

namespace {

static constexpr uint16_t kSnrTransportCodes[2] = {
	0x534EU,
	0x0001U,
};
static constexpr uint8_t kReqTypeGetTelemetryData = 0x03U;
static constexpr unsigned long kPendingTimeoutMs = 10000UL;

static bool encode_path_len_field(uint8_t path_hash_size, uint8_t path_len_bytes,
				      uint8_t *out)
{
	uint8_t hop_count;

	if (out == NULL || path_hash_size < 1U || path_hash_size > 3U) {
		return false;
	}
	if (path_len_bytes == 0U) {
		*out = (uint8_t)((path_hash_size - 1U) << 6);
		return true;
	}
	if ((path_len_bytes % path_hash_size) != 0U) {
		return false;
	}

	hop_count = (uint8_t)(path_len_bytes / path_hash_size);
	if (hop_count > 63U) {
		return false;
	}
	*out = (uint8_t)(((path_hash_size - 1U) << 6) | hop_count);
	return true;
}

class noop_contact_finder {
public:
	static ContactInfo *find(BaseChatMesh *mesh, const uint8_t *public_key)
	{
		return mesh->lookupContactByPubKey(public_key, PUB_KEY_SIZE);
	}
};

} // namespace

class ReferenceChatHarness::harness_millis_clock : public mesh::MillisecondClock {
public:
	unsigned long now = 0U;

	unsigned long getMillis() override
	{
		return now;
	}
};

class ReferenceChatHarness::harness_rtc_clock : public mesh::RTCClock {
public:
	uint32_t now = 0U;

	uint32_t getCurrentTime() override
	{
		return now;
	}

	void setCurrentTime(uint32_t time) override
	{
		now = time;
	}
};

class ReferenceChatHarness::harness_rng : public mesh::RNG {
public:
	uint8_t buffer[64] = {0};
	size_t len = 0U;
	size_t idx = 0U;

	void set_bytes(const uint8_t *data, size_t size)
	{
		len = size > sizeof(buffer) ? sizeof(buffer) : size;
		idx = 0U;
		memset(buffer, 0, sizeof(buffer));
		if (data != NULL && len > 0U) {
			memcpy(buffer, data, len);
		}
	}

	void random(uint8_t *dest, size_t sz) override
	{
		for (size_t i = 0U; i < sz; i++) {
			dest[i] = idx < len ? buffer[idx++] : 0U;
		}
	}
};

class ReferenceChatHarness::harness_radio : public mesh::Radio {
public:
	bool send_in_progress = false;
	bool send_complete = false;
	uint32_t send_finished_count = 0U;
	int last_len = 0;
	uint8_t last_raw[MESHCORE_MAX_TRANS_UNIT_LEN] = {0};

	int recvRaw(uint8_t *bytes, int sz) override
	{
		ARG_UNUSED(bytes);
		ARG_UNUSED(sz);
		return 0;
	}

	uint32_t getEstAirtimeFor(int len_bytes) override
	{
		return (uint32_t)(len_bytes < 1 ? 1 : len_bytes);
	}

	float packetScore(float snr, int packet_len) override
	{
		ARG_UNUSED(snr);
		ARG_UNUSED(packet_len);
		return 1.0f;
	}

	bool startSendRaw(const uint8_t *bytes, int len) override
	{
		if (bytes == NULL || len <= 0 || len > (int)sizeof(last_raw)) {
			return false;
		}

		memcpy(last_raw, bytes, (size_t)len);
		last_len = len;
		send_in_progress = true;
		send_complete = true;
		return true;
	}

	bool isSendComplete() override
	{
		return send_in_progress && send_complete;
	}

	void onSendFinished() override
	{
		send_in_progress = false;
		send_complete = false;
		send_finished_count++;
	}

	bool isInRecvMode() const override
	{
		return !send_in_progress;
	}
};

ReferenceChatHarness::ReferenceChatHarness()
	: BaseChatMesh(*(radio_ = new harness_radio()),
		       *(millis_clock_ = new harness_millis_clock()),
		       *(rng_ = new harness_rng()),
		       *(rtc_clock_ = new harness_rtc_clock()), packet_manager_,
		       tables_),
	  packet_manager_(16)
{
	reset();
}

void ReferenceChatHarness::reset()
{
	*millis_clock_ = harness_millis_clock();
	*rtc_clock_ = harness_rtc_clock();
	*rng_ = harness_rng();
	*radio_ = harness_radio();
	memset(node_name_, 0, sizeof(node_name_));
	(void)snprintf(node_name_, sizeof(node_name_), "%s", "runtime_node");
	advert_has_position_ = true;
	advert_latitude_ = 31.0;
	advert_longitude_ = 121.0;
	local_path_hash_size_ = 1U;
	require_local_identity_for_send_ = false;
	require_known_group_channel_ = false;
	memset(known_group_channels_, 0, sizeof(known_group_channels_));
	memset(contact_meta_, 0, sizeof(contact_meta_));
	memset(&pending_discovery_, 0, sizeof(pending_discovery_));
	memset(&pending_trace_, 0, sizeof(pending_trace_));
	memset(&pending_telemetry_, 0, sizeof(pending_telemetry_));
	memset(&last_peer_path_event_, 0, sizeof(last_peer_path_event_));
	memset(&last_trace_event_, 0, sizeof(last_trace_event_));
	memset(&last_telemetry_event_, 0, sizeof(last_telemetry_event_));
	memset(&last_advert_event_, 0, sizeof(last_advert_event_));
	peer_path_publish_count_ = 0U;
	trace_publish_count_ = 0U;
	telemetry_publish_count_ = 0U;
	advert_observed_count_ = 0U;
	contact_path_update_count_ = 0U;
	resetContacts();
	memset(&self_id, 0, sizeof(self_id));
}

void ReferenceChatHarness::set_self_identity(
	const struct meshcore_local_identity &identity)
{
	uint8_t raw[PRV_KEY_SIZE + PUB_KEY_SIZE];

	memcpy(raw, identity.prv_key, PRV_KEY_SIZE);
	memcpy(&raw[PRV_KEY_SIZE], identity.identity.pub_key, PUB_KEY_SIZE);
	self_id.readFrom(raw, sizeof(raw));
}

void ReferenceChatHarness::set_node_name(const char *name)
{
	if (name == NULL) {
		node_name_[0] = '\0';
		return;
	}

	(void)snprintf(node_name_, sizeof(node_name_), "%s", name);
}

void ReferenceChatHarness::set_advert_profile(bool has_position, double latitude,
					      double longitude)
{
	advert_has_position_ = has_position;
	advert_latitude_ = latitude;
	advert_longitude_ = longitude;
}

void ReferenceChatHarness::set_local_path_hash_size(uint8_t path_hash_size)
{
	if (path_hash_size >= 1U && path_hash_size <= 3U) {
		local_path_hash_size_ = path_hash_size;
	}
}

void ReferenceChatHarness::set_random_bytes(const uint8_t *data, size_t len)
{
	rng_->set_bytes(data, len);
}

void ReferenceChatHarness::set_require_local_identity_for_send(bool enabled)
{
	require_local_identity_for_send_ = enabled;
}

void ReferenceChatHarness::set_require_known_group_channel(bool enabled)
{
	require_known_group_channel_ = enabled;
}

bool ReferenceChatHarness::add_known_group_channel_secret(const uint8_t *secret,
							  size_t secret_len)
{
	size_t i;

	if (secret == NULL || secret_len == 0U ||
	    secret_len > MESHCORE_CHANNEL_SECRET_MAX_LEN) {
		return false;
	}

	for (i = 0U; i < ARRAY_SIZE(known_group_channels_); i++) {
		if (known_group_channels_[i].used &&
		    known_group_channels_[i].secret_len == secret_len &&
		    memcmp(known_group_channels_[i].secret, secret, secret_len) == 0) {
			return true;
		}
	}

	for (i = 0U; i < ARRAY_SIZE(known_group_channels_); i++) {
		if (!known_group_channels_[i].used) {
			known_group_channels_[i].used = true;
			known_group_channels_[i].secret_len = (uint8_t)secret_len;
			memcpy(known_group_channels_[i].secret, secret, secret_len);
			return true;
		}
	}

	return false;
}

bool ReferenceChatHarness::upsert_contact(const uint8_t *public_key, const char *name,
					  uint8_t type)
{
	ContactInfo *contact;
	contact_meta *meta = lookup_contact_meta_mutable(public_key);

	if (public_key == NULL) {
		return false;
	}

	contact = lookup_contact(public_key);
	if (contact == NULL) {
		ContactInfo init = {};

		init.id = mesh::Identity(public_key);
		init.type = type;
		init.out_path_len = OUT_PATH_UNKNOWN;
		init.shared_secret_valid = false;
		if (name != NULL) {
			StrHelper::strncpy(init.name, name, sizeof(init.name));
		}
		if (!addContact(init)) {
			return false;
		}
		contact = lookup_contact(public_key);
	}

	if (contact == NULL) {
		return false;
	}
	if (name != NULL) {
		StrHelper::strncpy(contact->name, name, sizeof(contact->name));
	}
	contact->type = type;
	if (meta != NULL) {
		meta->used = true;
		memcpy(meta->public_key, public_key, sizeof(meta->public_key));
		if (meta->path_hash_size == 0U) {
			meta->path_hash_size = 1U;
		}
	}
	return true;
}

bool ReferenceChatHarness::set_contact_out_path(const uint8_t *public_key,
						const uint8_t *path,
						uint8_t path_len_bytes,
						uint8_t path_hash_size)
{
	ContactInfo *contact = lookup_contact(public_key);
	contact_meta *meta = lookup_contact_meta_mutable(public_key);

	if (contact == NULL || meta == NULL) {
		return false;
	}

	if (path == NULL || path_len_bytes == 0U) {
		contact->out_path_len = OUT_PATH_UNKNOWN;
		memset(contact->out_path, 0, sizeof(contact->out_path));
		meta->path_hash_size = path_hash_size;
		return true;
	}

	contact->out_path_len = mesh::Packet::copyPath(contact->out_path, path,
							 path_len_bytes);
	meta->path_hash_size = path_hash_size;
	return true;
}

bool ReferenceChatHarness::send_self_advert(bool flood)
{
	mesh::Packet *packet = advert_has_position_ ?
		createSelfAdvert(node_name_, advert_latitude_, advert_longitude_) :
		createSelfAdvert(node_name_);

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (packet == NULL) {
		return false;
	}
	if (flood) {
		sendFlood(packet, 0U, local_path_hash_size_);
	} else {
		sendZeroHop(packet, 0U);
	}
	return true;
}

bool ReferenceChatHarness::replay_peer_advert_zero_hop(const uint8_t *raw, size_t len)
{
	mesh::Packet *packet = obtainNewPacket();

	if (packet == NULL || raw == NULL || len == 0U ||
	    len > MESHCORE_MAX_TRANS_UNIT_LEN) {
		return false;
	}
	if (!packet->readFrom(raw, (uint8_t)len) ||
	    packet->getPayloadType() != PAYLOAD_TYPE_ADVERT) {
		releasePacket(packet);
		return false;
	}

	sendZeroHop(packet, 0U);
	return true;
}

bool ReferenceChatHarness::send_message_to_contact(const uint8_t *public_key, bool flood,
						   uint8_t attempt, const uint8_t *payload,
						   size_t payload_len)
{
	ContactInfo *contact = lookup_contact(public_key);
	ContactInfo send_contact;
	uint32_t expected_ack = 0U;
	uint32_t est_timeout = 0U;
	char text[MESHCORE_MAX_MESSAGE_TX_LEN + 1U];

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (contact == NULL || payload == NULL ||
	    payload_len > MESHCORE_MAX_MESSAGE_TX_LEN) {
		return false;
	}

	memset(text, 0, sizeof(text));
	memcpy(text, payload, payload_len);
	send_contact = *contact;
	if (flood) {
		send_contact.out_path_len = OUT_PATH_UNKNOWN;
		memset(send_contact.out_path, 0, sizeof(send_contact.out_path));
	}

	return sendMessage(send_contact, getRTCClock()->getCurrentTime(), attempt, text,
			  expected_ack, est_timeout) != MSG_SEND_FAILED;
}

bool ReferenceChatHarness::build_group_channel(const uint8_t *secret, size_t secret_len,
					       mesh::GroupChannel *out) const
{
	if (secret == NULL || out == NULL ||
	    secret_len > sizeof(out->secret) || secret_len == 0U) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	memcpy(out->secret, secret, secret_len);
	mesh::Utils::sha256(out->hash, PATH_HASH_SIZE, secret, (int)secret_len);
	return true;
}

bool ReferenceChatHarness::has_known_group_channel_secret(const uint8_t *secret,
							  size_t secret_len) const
{
	size_t i;

	if (secret == NULL || secret_len == 0U ||
	    secret_len > MESHCORE_CHANNEL_SECRET_MAX_LEN) {
		return false;
	}

	for (i = 0U; i < ARRAY_SIZE(known_group_channels_); i++) {
		if (!known_group_channels_[i].used) {
			continue;
		}
		if (known_group_channels_[i].secret_len == secret_len &&
		    memcmp(known_group_channels_[i].secret, secret, secret_len) == 0) {
			return true;
		}
	}

	return false;
}

bool ReferenceChatHarness::has_local_identity_for_send() const
{
	for (size_t i = 0U; i < PUB_KEY_SIZE; i++) {
		if (self_id.pub_key[i] != 0U) {
			return true;
		}
	}
	return false;
}

bool ReferenceChatHarness::send_group_message(const uint8_t *secret, size_t secret_len,
					      const uint8_t *payload, size_t payload_len)
{
	mesh::GroupChannel channel;
	char text[MESHCORE_MAX_MESSAGE_TX_LEN + 1U];

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (require_known_group_channel_ &&
	    !has_known_group_channel_secret(secret, secret_len)) {
		return false;
	}
	if (!build_group_channel(secret, secret_len, &channel) || payload == NULL ||
	    payload_len > MESHCORE_MAX_MESSAGE_TX_LEN) {
		return false;
	}

	memset(text, 0, sizeof(text));
	memcpy(text, payload, payload_len);
	return sendGroupMessage(getRTCClock()->getCurrentTime(), channel, node_name_, text,
			       (int)payload_len);
}

bool ReferenceChatHarness::send_group_data(const uint8_t *secret, size_t secret_len,
					   const uint8_t *path, uint8_t path_len,
					   uint16_t data_type,
					   const uint8_t *payload,
					   size_t payload_len)
{
	mesh::GroupChannel channel;
	uint8_t path_copy[MAX_PATH_SIZE] = {0};

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (require_known_group_channel_ &&
	    !has_known_group_channel_secret(secret, secret_len)) {
		return false;
	}
	if (!build_group_channel(secret, secret_len, &channel) ||
	    (payload == NULL && payload_len > 0U) ||
	    payload_len > MAX_GROUP_DATA_LENGTH) {
		return false;
	}

	if (path_len != OUT_PATH_UNKNOWN) {
		size_t path_bytes =
			(size_t)(path_len & 63U) * (size_t)((path_len >> 6U) + 1U);
		if (path == NULL || !mesh::Packet::isValidPathLen(path_len)) {
			return false;
		}
		memcpy(path_copy, path, path_bytes);
	}

	return sendGroupData(channel, path_copy, path_len, data_type, payload,
			     (int)payload_len);
}

bool ReferenceChatHarness::send_discover_request(const uint8_t *public_key)
{
	mesh::Identity recipient(public_key);
	mesh::Packet *packet;
	uint8_t secret[PUB_KEY_SIZE];
	uint8_t req_data[9];
	uint8_t data[13];
	uint32_t tag;

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (public_key == NULL) {
		return false;
	}

	tag = getRTCClock()->getCurrentTimeUnique();
	req_data[0] = kReqTypeGetTelemetryData;
	req_data[1] = (uint8_t)~MESHCORE_TELEM_PERM_BASE;
	memset(&req_data[2], 0, 3U);
	getRNG()->random(&req_data[5], 4U);
	memcpy(data, &tag, sizeof(tag));
	memcpy(&data[4], req_data, sizeof(req_data));

	self_id.calcSharedSecret(secret, recipient);
	packet = createDatagram(PAYLOAD_TYPE_REQ, recipient, secret, data, sizeof(data));
	memset(secret, 0, sizeof(secret));
	if (packet == NULL) {
		return false;
	}

	sendFlood(packet, 0U, local_path_hash_size_);
	pending_discovery_.valid = true;
	pending_discovery_.tag = tag;
	memcpy(pending_discovery_.public_key, public_key,
	       sizeof(pending_discovery_.public_key));
	pending_discovery_.expires_at_ms = millis_clock_->now + kPendingTimeoutMs;
	return true;
}

bool ReferenceChatHarness::send_trace_request(const uint8_t *public_key)
{
	ContactInfo *contact = lookup_contact(public_key);
	const contact_meta *meta = lookup_contact_meta(public_key);
	mesh::Packet *packet;
	uint8_t trace_out_path[MAX_PATH_SIZE];
	uint8_t trace_path[MAX_PACKET_PAYLOAD];
	uint8_t peer_hash_size;
	uint8_t trace_hash_size;
	uint8_t flags = 0U;
	uint32_t tag;
	uint32_t auth_code;
	size_t trace_out_len;
	size_t trace_len = 0U;
	size_t hop_count;
	size_t hop;

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (contact == NULL || meta == NULL || contact->out_path_len == OUT_PATH_UNKNOWN ||
	    contact->out_path_len == 0U) {
		return false;
	}

	peer_hash_size = meta->path_hash_size;
	if (peer_hash_size == 0U || peer_hash_size > 3U ||
	    (contact->out_path_len % peer_hash_size) != 0U) {
		return false;
	}

	trace_hash_size = peer_hash_size == 3U ? 2U : peer_hash_size;
	if (trace_hash_size == 2U) {
		flags = 1U;
	} else if (trace_hash_size != 1U) {
		return false;
	}

	trace_out_len = contact->out_path_len;
	if (peer_hash_size != trace_hash_size) {
		hop_count = contact->out_path_len / peer_hash_size;
		trace_out_len = hop_count * trace_hash_size;
		for (hop = 0U; hop < hop_count; hop++) {
			memcpy(&trace_out_path[hop * trace_hash_size],
			       &contact->out_path[hop * peer_hash_size], trace_hash_size);
		}
	} else {
		memcpy(trace_out_path, contact->out_path, trace_out_len);
	}

	tag = getRTCClock()->getCurrentTimeUnique();
	auth_code = millis_clock_->now & 0x00FFFFFFUL;
	packet = createTrace(tag, auth_code, flags);
	if (packet == NULL) {
		return false;
	}
	if (trace_out_len > MAX_PACKET_PAYLOAD - 9U) {
		releasePacket(packet);
		return false;
	}

	memcpy(trace_path, trace_out_path, trace_out_len);
	trace_len = trace_out_len;
	for (hop = trace_out_len; hop >= (size_t)(2U * trace_hash_size);
	     hop -= trace_hash_size) {
		memcpy(&trace_path[trace_len],
		       &trace_out_path[hop - (2U * trace_hash_size)], trace_hash_size);
		trace_len += trace_hash_size;
	}

	sendDirect(packet, trace_path, (uint8_t)trace_len, 0U);
	pending_trace_.valid = true;
	pending_trace_.tag = tag;
	copy_key_prefix(pending_trace_.key_prefix, public_key);
	pending_trace_.expires_at_ms = millis_clock_->now + kPendingTimeoutMs;
	return true;
}

bool ReferenceChatHarness::send_telemetry_request(const uint8_t *public_key,
						  uint8_t permission_mask)
{
	ContactInfo *contact = lookup_contact(public_key);
	uint8_t req_data[9];
	uint32_t tag;
	uint32_t est_timeout = 0U;

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (contact == NULL) {
		return false;
	}

	req_data[0] = kReqTypeGetTelemetryData;
	req_data[1] = (uint8_t)~permission_mask;
	memset(&req_data[2], 0, 3U);
	getRNG()->random(&req_data[5], 4U);
	if (sendRequest(*contact, req_data, sizeof(req_data), tag,
			est_timeout) == MSG_SEND_FAILED) {
		return false;
	}

	pending_telemetry_.valid = true;
	pending_telemetry_.tag = tag;
	memcpy(pending_telemetry_.public_key, public_key,
	       sizeof(pending_telemetry_.public_key));
	copy_key_prefix(pending_telemetry_.key_prefix, public_key);
	pending_telemetry_.permission_mask = permission_mask;
	pending_telemetry_.expected_payload_len = 0U;
	pending_telemetry_.expires_at_ms = millis_clock_->now + kPendingTimeoutMs;
	return true;
}

bool ReferenceChatHarness::send_binary_request(const uint8_t *public_key,
					       const uint8_t *payload,
					       size_t payload_len)
{
	ContactInfo *contact = lookup_contact(public_key);
	uint32_t tag = 0U;
	uint32_t est_timeout = 0U;

	if (require_local_identity_for_send_ && !has_local_identity_for_send()) {
		return false;
	}
	if (contact == NULL || payload == NULL || payload_len == 0U ||
	    payload_len > MESHCORE_MAX_SERVICE_REQUEST_PAYLOAD_LEN) {
		return false;
	}

	return sendRequest(*contact, payload, (uint8_t)payload_len, tag,
			   est_timeout) != MSG_SEND_FAILED;
}

bool ReferenceChatHarness::inject_discover_response(
	const uint8_t *public_key, const uint8_t *out_path, uint8_t out_path_len,
	uint8_t path_hash_size, const int8_t *out_path_snr,
	uint8_t out_path_snr_count, const int8_t *return_path_snr,
	uint8_t return_path_snr_count, int8_t response_snr)
{
	uint8_t extra[sizeof(uint32_t)];
	uint8_t extra_type = PAYLOAD_TYPE_RESPONSE;
	uint8_t extra_len = sizeof(extra);

	(void)out_path_snr;
	(void)out_path_snr_count;
	(void)return_path_snr;
	(void)return_path_snr_count;

	if (!pending_discovery_.valid || public_key == NULL ||
	    memcmp(pending_discovery_.public_key, public_key,
		   sizeof(pending_discovery_.public_key)) != 0) {
		return false;
	}
	memcpy(extra, &pending_discovery_.tag, sizeof(pending_discovery_.tag));

	if (!inject_discover_response_for(public_key, out_path, out_path_len,
					  path_hash_size, extra_type,
					  extra,
					  extra_len, response_snr)) {
		return false;
	}

	memset(&last_peer_path_event_, 0, sizeof(last_peer_path_event_));
	last_peer_path_event_.published = true;
	last_peer_path_event_.is_discover = true;
	copy_key_prefix(last_peer_path_event_.key_prefix, public_key);
	last_peer_path_event_.response_snr = response_snr;
	last_peer_path_event_.out_path_len = out_path_len;
	last_peer_path_event_.path_hash_size = path_hash_size;
	if (out_path != NULL && out_path_len > 0U) {
		memcpy(last_peer_path_event_.out_path, out_path, out_path_len);
	}
	peer_path_publish_count_++;
	memset(&pending_discovery_, 0, sizeof(pending_discovery_));
	return true;
}

bool ReferenceChatHarness::inject_discover_response_for(
	const uint8_t *public_key, const uint8_t *out_path, uint8_t out_path_len,
	uint8_t path_hash_size, uint8_t extra_type, const uint8_t *extra,
	uint8_t extra_len, int8_t response_snr)
{
	if (public_key == NULL) {
		return false;
	}

	return build_inbound_path_packet(public_key, out_path, out_path_len,
					 path_hash_size, extra_type, extra,
					 extra_len, response_snr);
}

bool ReferenceChatHarness::inject_trace_result(uint8_t flags, const int8_t *path_snrs,
					       uint8_t path_snr_count,
					       int8_t response_snr)
{
	if (!pending_trace_.valid || path_snrs == NULL || path_snr_count == 0U ||
	    (path_snr_count % 2U) == 0U) {
		return false;
	}

	return inject_trace_result_for(pending_trace_.tag, flags, path_snrs,
				       path_snr_count, response_snr);
}

bool ReferenceChatHarness::inject_trace_result_for(uint32_t tag, uint8_t flags,
						   const int8_t *path_snrs,
						   uint8_t path_snr_count,
						   int8_t response_snr)
{
	mesh::Packet *packet;
	uint8_t hash_size;
	uint8_t path_hash_bytes;
	uint32_t auth_code = 0x13572468U;

	if (path_snrs == NULL || path_snr_count == 0U) {
		return false;
	}

	hash_size = (uint8_t)(1U << (flags & 0x03U));
	if (hash_size == 0U || hash_size == 4U) {
		return false;
	}
	path_hash_bytes = (uint8_t)(path_snr_count * hash_size);
	packet = obtainNewPacket();
	if (packet == NULL) {
		return false;
	}

	packet->header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet->path_len = path_snr_count;
	memcpy(packet->path, path_snrs, path_snr_count);
	memcpy(packet->payload, &tag, sizeof(tag));
	memcpy(&packet->payload[4], &auth_code, sizeof(auth_code));
	packet->payload[8] = flags;
	for (uint8_t i = 0U; i < path_hash_bytes; i++) {
		packet->payload[9U + i] = (uint8_t)(0x40U + i);
	}
	packet->payload_len = (uint16_t)(9U + path_hash_bytes);
	packet->_snr = response_snr;
	return dispatch_inbound_packet(packet);
}

bool ReferenceChatHarness::inject_telemetry_response(const uint8_t *public_key,
						     const uint8_t *payload,
						     size_t payload_len,
						     int8_t response_snr)
{
	uint8_t data[sizeof(uint32_t) + MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN];

	if (!pending_telemetry_.valid || public_key == NULL || payload == NULL ||
	    payload_len > MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN ||
	    memcmp(pending_telemetry_.public_key, public_key,
		   sizeof(pending_telemetry_.public_key)) != 0) {
		return false;
	}

	memcpy(data, &pending_telemetry_.tag, sizeof(pending_telemetry_.tag));
	if (payload_len > 0U) {
		memcpy(&data[sizeof(pending_telemetry_.tag)], payload, payload_len);
	}
	pending_telemetry_.expected_payload_len = (uint8_t)payload_len;

	return inject_telemetry_response_for(public_key, pending_telemetry_.tag,
					     payload, payload_len, response_snr);
}

bool ReferenceChatHarness::inject_telemetry_response_for(
	const uint8_t *public_key, uint32_t tag, const uint8_t *payload,
	size_t payload_len, int8_t response_snr)
{
	uint8_t data[sizeof(tag) + MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN];

	if (public_key == NULL || payload == NULL ||
	    payload_len > MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN) {
		return false;
	}

	memcpy(data, &tag, sizeof(tag));
	if (payload_len > 0U) {
		memcpy(&data[sizeof(tag)], payload, payload_len);
	}

	return build_inbound_response_packet(public_key, data, sizeof(tag) + payload_len,
					     response_snr);
}

bool ReferenceChatHarness::inject_raw_packet(const uint8_t *raw, size_t len)
{
	mesh::Packet *packet;

	if (raw == NULL || len == 0U || len > UINT8_MAX) {
		return false;
	}

	packet = obtainNewPacket();
	if (packet == NULL) {
		return false;
	}
	if (!packet->readFrom(raw, (uint8_t)len)) {
		releasePacket(packet);
		return false;
	}

	return dispatch_inbound_packet(packet);
}

bool ReferenceChatHarness::inject_advert(const uint8_t *raw, size_t len)
{
	if (raw == NULL || len == 0U || len > UINT8_MAX) {
		return false;
	}
	if (!importContact(raw, (uint8_t)len)) {
		return false;
	}
	loop();
	return advert_observed_count_ > 0U;
}

bool ReferenceChatHarness::build_inbound_path_raw(
	const uint8_t *public_key, const uint8_t *out_path, uint8_t out_path_len,
	uint8_t path_hash_size, uint8_t extra_type, const uint8_t *extra,
	uint8_t extra_len, int8_t response_snr, observed_packet *out)
{
	mesh::Identity peer(public_key);
	mesh::Packet packet;
	uint8_t secret[PUB_KEY_SIZE];
	uint8_t data[MAX_PACKET_PAYLOAD];
	uint8_t path_len_field;
	int payload_len = 0;
	int data_len = 0;
	uint8_t raw_len;

	if (out == NULL || public_key == NULL ||
	    !encode_path_len_field(path_hash_size, out_path_len, &path_len_field)) {
		return false;
	}

	data[0] = path_len_field;
	if (out_path != NULL && out_path_len > 0U) {
		memcpy(&data[1], out_path, out_path_len);
	}
	data_len = 1 + out_path_len;
	if (extra != NULL && extra_len > 0U) {
		if ((size_t)data_len + 1U + extra_len > sizeof(data)) {
			return false;
		}
		data[data_len++] = extra_type;
		memcpy(&data[data_len], extra, extra_len);
		data_len += extra_len;
	}

	memset(&packet, 0, sizeof(packet));
	packet.header = (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet.path_len = 0U;
	packet._snr = response_snr;
	payload_len += self_id.copyHashTo(&packet.payload[payload_len]);
	payload_len += peer.copyHashTo(&packet.payload[payload_len]);
	self_id.calcSharedSecret(secret, peer);
	payload_len += mesh::Utils::encryptThenMAC(secret,
						   &packet.payload[payload_len],
						   data, data_len);
	memset(secret, 0, sizeof(secret));
	if (payload_len <= 0 || payload_len > MAX_PACKET_PAYLOAD) {
		return false;
	}

	packet.payload_len = (uint16_t)payload_len;
	memset(out, 0, sizeof(*out));
	raw_len = packet.writeTo(out->raw);
	if (raw_len == 0U || raw_len > sizeof(out->raw)) {
		return false;
	}

	out->sent = true;
	out->raw_len = raw_len;
	out->route = ROUTE_DIRECT;
	out->has_transport_codes = false;
	out->payload_type = PAYLOAD_TYPE_PATH;
	return true;
}

bool ReferenceChatHarness::build_inbound_response_raw(
	const uint8_t *public_key, const uint8_t *data, size_t data_len,
	int8_t response_snr, observed_packet *out)
{
	mesh::Identity peer(public_key);
	mesh::Packet packet;
	uint8_t secret[PUB_KEY_SIZE];
	int payload_len = 0;
	uint8_t raw_len;

	if (out == NULL || public_key == NULL || data == NULL || data_len == 0U ||
	    data_len > MAX_PACKET_PAYLOAD) {
		return false;
	}

	memset(&packet, 0, sizeof(packet));
	packet.header =
		(PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet.path_len = 0U;
	packet._snr = response_snr;
	payload_len += self_id.copyHashTo(&packet.payload[payload_len]);
	payload_len += peer.copyHashTo(&packet.payload[payload_len]);
	self_id.calcSharedSecret(secret, peer);
	payload_len += mesh::Utils::encryptThenMAC(secret,
						   &packet.payload[payload_len],
						   data, data_len);
	memset(secret, 0, sizeof(secret));
	if (payload_len <= 0 || payload_len > MAX_PACKET_PAYLOAD) {
		return false;
	}

	packet.payload_len = (uint16_t)payload_len;
	memset(out, 0, sizeof(*out));
	raw_len = packet.writeTo(out->raw);
	if (raw_len == 0U || raw_len > sizeof(out->raw)) {
		return false;
	}

	out->sent = true;
	out->raw_len = raw_len;
	out->route = ROUTE_ZERO_HOP;
	out->has_transport_codes = false;
	out->payload_type = PAYLOAD_TYPE_RESPONSE;
	return true;
}

bool ReferenceChatHarness::drive_until_packets_sent(uint32_t expected_packets)
{
	for (int i = 0; i < 256; i++) {
		millis_clock_->now += 10U;
		loop();
		millis_clock_->now += 10U;
		loop();
		if (radio_->send_finished_count >= expected_packets) {
			return true;
		}
	}

	return radio_->send_finished_count >= expected_packets;
}

bool ReferenceChatHarness::capture_last_packet(observed_packet *out) const
{
	mesh::Packet packet;

	if (out == NULL || radio_->last_len <= 0) {
		return false;
	}
	if (!packet.readFrom(radio_->last_raw, (uint8_t)radio_->last_len)) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->sent = true;
	out->raw_len = radio_->last_len;
	memcpy(out->raw, radio_->last_raw, (size_t)radio_->last_len);
	if (packet.isRouteFlood()) {
		out->route = ROUTE_FLOOD;
	} else if (packet.isRouteDirect() &&
		   packet.getPayloadType() == PAYLOAD_TYPE_TRACE) {
		out->route = ROUTE_DIRECT;
	} else if (packet.isRouteDirect() && packet.path_len == 0U) {
		out->route = ROUTE_ZERO_HOP;
	} else if (packet.isRouteDirect()) {
		out->route = ROUTE_DIRECT;
	} else {
		out->route = ROUTE_NONE;
	}
	out->has_transport_codes = packet.hasTransportCodes();
	out->payload_type = packet.getPayloadType();
	return true;
}

bool ReferenceChatHarness::capture_last_peer_path_event(
	observed_peer_path_event *out) const
{
	if (out == NULL || !last_peer_path_event_.published) {
		return false;
	}
	*out = last_peer_path_event_;
	return true;
}

bool ReferenceChatHarness::capture_last_trace_event(observed_trace_event *out) const
{
	if (out == NULL || !last_trace_event_.published) {
		return false;
	}
	*out = last_trace_event_;
	return true;
}

bool ReferenceChatHarness::capture_last_telemetry_event(
	observed_telemetry_event *out) const
{
	if (out == NULL || !last_telemetry_event_.published) {
		return false;
	}
	*out = last_telemetry_event_;
	return true;
}

bool ReferenceChatHarness::capture_last_advert_event(observed_advert_event *out) const
{
	if (out == NULL || !last_advert_event_.observed) {
		return false;
	}
	*out = last_advert_event_;
	return true;
}

uint32_t ReferenceChatHarness::sent_packets() const
{
	return radio_->send_finished_count;
}

uint32_t ReferenceChatHarness::peer_path_publish_count() const
{
	return peer_path_publish_count_;
}

uint32_t ReferenceChatHarness::trace_publish_count() const
{
	return trace_publish_count_;
}

uint32_t ReferenceChatHarness::telemetry_publish_count() const
{
	return telemetry_publish_count_;
}

uint32_t ReferenceChatHarness::advert_observed_count() const
{
	return advert_observed_count_;
}

uint32_t ReferenceChatHarness::contact_path_update_count() const
{
	return contact_path_update_count_;
}

bool ReferenceChatHarness::pending_discovery_active() const
{
	return pending_discovery_.valid;
}

bool ReferenceChatHarness::pending_discovery_get(uint8_t *key_prefix,
						 uint32_t *tag) const
{
	if (!pending_discovery_.valid) {
		return false;
	}
	if (key_prefix != NULL) {
		copy_key_prefix(key_prefix, pending_discovery_.public_key);
	}
	if (tag != NULL) {
		*tag = pending_discovery_.tag;
	}
	return true;
}

bool ReferenceChatHarness::pending_trace_active() const
{
	return pending_trace_.valid;
}

bool ReferenceChatHarness::pending_trace_get(uint32_t *tag,
					     uint8_t *key_prefix) const
{
	if (!pending_trace_.valid) {
		return false;
	}
	if (tag != NULL) {
		*tag = pending_trace_.tag;
	}
	if (key_prefix != NULL) {
		memcpy(key_prefix, pending_trace_.key_prefix, MESHCORE_NODE_KEY_PREFIX_BYTES);
	}
	return true;
}

bool ReferenceChatHarness::pending_telemetry_active() const
{
	return pending_telemetry_.valid;
}

bool ReferenceChatHarness::pending_telemetry_get(uint32_t *tag,
						 uint8_t *key_prefix,
						 uint8_t *permission_mask) const
{
	if (!pending_telemetry_.valid) {
		return false;
	}
	if (tag != NULL) {
		*tag = pending_telemetry_.tag;
	}
	if (key_prefix != NULL) {
		memcpy(key_prefix, pending_telemetry_.key_prefix,
		       MESHCORE_NODE_KEY_PREFIX_BYTES);
	}
	if (permission_mask != NULL) {
		*permission_mask = pending_telemetry_.permission_mask;
	}
	return true;
}

unsigned long ReferenceChatHarness::pending_discovery_expires_at() const
{
	return pending_discovery_.expires_at_ms;
}

unsigned long ReferenceChatHarness::pending_trace_expires_at() const
{
	return pending_trace_.expires_at_ms;
}

unsigned long ReferenceChatHarness::pending_telemetry_expires_at() const
{
	return pending_telemetry_.expires_at_ms;
}

void ReferenceChatHarness::advance_time(unsigned long delta_ms)
{
	millis_clock_->now += delta_ms;
	cleanup_expired_pending();
}

void ReferenceChatHarness::onTraceRecv(mesh::Packet *packet, uint32_t tag,
				       uint32_t auth_code, uint8_t flags,
				       const uint8_t *path_snrs,
				       const uint8_t *path_hashes,
				       uint8_t path_len)
{
	uint8_t hash_size;
	uint8_t hash_count;
	uint8_t forward_hops;

	ARG_UNUSED(auth_code);
	ARG_UNUSED(path_hashes);
	if (!pending_trace_.valid || packet == NULL || path_snrs == NULL ||
	    pending_trace_.tag != tag) {
		return;
	}

	hash_size = (uint8_t)(1U << (flags & 0x03U));
	if (hash_size == 0U || hash_size == 4U || path_len == 0U ||
	    (path_len % hash_size) != 0U) {
		memset(&pending_trace_, 0, sizeof(pending_trace_));
		return;
	}

	hash_count = (uint8_t)(path_len / hash_size);
	if (packet->path_len != hash_count || (hash_count % 2U) == 0U) {
		memset(&pending_trace_, 0, sizeof(pending_trace_));
		return;
	}

	memset(&last_trace_event_, 0, sizeof(last_trace_event_));
	last_trace_event_.published = true;
	memcpy(last_trace_event_.key_prefix, pending_trace_.key_prefix,
	       sizeof(last_trace_event_.key_prefix));
	last_trace_event_.tag = tag;
	last_trace_event_.flags = flags;
	last_trace_event_.state = 1U;
	last_trace_event_.response_snr = packet->_snr;

	forward_hops = (uint8_t)((hash_count - 1U) / 2U);
	if (forward_hops > 0U) {
		memcpy(last_trace_event_.out_path_snr, path_snrs, forward_hops);
		last_trace_event_.out_path_snr_count = forward_hops;
		memcpy(last_trace_event_.return_path_snr,
		       &path_snrs[forward_hops + 1U], forward_hops);
		last_trace_event_.return_path_snr_count = forward_hops;
	}
	last_trace_event_
		.out_path_snr[last_trace_event_.out_path_snr_count++] =
		(int8_t)path_snrs[forward_hops];
	last_trace_event_
		.return_path_snr[last_trace_event_.return_path_snr_count++] =
		packet->_snr;
	trace_publish_count_++;
	memset(&pending_trace_, 0, sizeof(pending_trace_));
}

void ReferenceChatHarness::onDiscoveredContact(ContactInfo &contact, bool is_new,
					       uint8_t path_len, const uint8_t *path)
{
	memset(&last_advert_event_, 0, sizeof(last_advert_event_));
	last_advert_event_.observed = true;
	last_advert_event_.is_new = is_new;
	memcpy(last_advert_event_.public_key, contact.id.pub_key,
	       sizeof(last_advert_event_.public_key));
	StrHelper::strncpy(last_advert_event_.name, contact.name,
			   sizeof(last_advert_event_.name));
	last_advert_event_.type = contact.type;
	last_advert_event_.advert_timestamp = contact.last_advert_timestamp;
	last_advert_event_.has_position =
		contact.gps_lat != 0 || contact.gps_lon != 0;
	last_advert_event_.latitude = contact.gps_lat;
	last_advert_event_.longitude = contact.gps_lon;
	last_advert_event_.path_len = path_len;
	if (path != NULL && path_len > 0U) {
		memcpy(last_advert_event_.path, path, path_len);
	}
	advert_observed_count_++;
}

ContactInfo *ReferenceChatHarness::processAck(const uint8_t *data)
{
	ARG_UNUSED(data);
	return NULL;
}

void ReferenceChatHarness::onContactPathUpdated(const ContactInfo &contact)
{
	contact_path_update_count_++;
	ARG_UNUSED(contact);
}

void ReferenceChatHarness::onMessageRecv(const ContactInfo &contact, mesh::Packet *pkt,
					 uint32_t sender_timestamp, const char *text)
{
	ARG_UNUSED(contact);
	ARG_UNUSED(pkt);
	ARG_UNUSED(sender_timestamp);
	ARG_UNUSED(text);
}

void ReferenceChatHarness::onCommandDataRecv(const ContactInfo &contact,
					     mesh::Packet *pkt,
					     uint32_t sender_timestamp,
					     const char *text)
{
	ARG_UNUSED(contact);
	ARG_UNUSED(pkt);
	ARG_UNUSED(sender_timestamp);
	ARG_UNUSED(text);
}

void ReferenceChatHarness::onCLICommandRecv(const ContactInfo &contact,
					  mesh::Packet *pkt,
					  uint32_t sender_timestamp,
					  const char *text, char *reply)
{
	ARG_UNUSED(contact);
	ARG_UNUSED(pkt);
	ARG_UNUSED(sender_timestamp);
	ARG_UNUSED(text);
	/* Match the target test host's unsupported native CLI policy. */
	reply[0] = '\0';
}

void ReferenceChatHarness::onSignedMessageRecv(const ContactInfo &contact,
					       mesh::Packet *pkt,
					       uint32_t sender_timestamp,
					       const uint8_t *sender_prefix,
					       const char *text)
{
	ARG_UNUSED(contact);
	ARG_UNUSED(pkt);
	ARG_UNUSED(sender_timestamp);
	ARG_UNUSED(sender_prefix);
	ARG_UNUSED(text);
}

uint32_t ReferenceChatHarness::calcFloodTimeoutMillisFor(
	uint32_t pkt_airtime_millis) const
{
	return pkt_airtime_millis * 4U;
}

uint32_t ReferenceChatHarness::calcDirectTimeoutMillisFor(
	uint32_t pkt_airtime_millis, uint8_t path_len) const
{
	ARG_UNUSED(path_len);
	return pkt_airtime_millis * 3U;
}

void ReferenceChatHarness::onSendTimeout()
{
}

void ReferenceChatHarness::onChannelMessageRecv(const mesh::GroupChannel &channel,
						mesh::Packet *pkt, uint32_t timestamp,
						const char *text)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(pkt);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(text);
}

uint8_t ReferenceChatHarness::onContactRequest(const ContactInfo &contact,
					       uint32_t sender_timestamp,
					       const uint8_t *data, uint8_t len,
					       uint8_t *reply)
{
	ARG_UNUSED(contact);
	ARG_UNUSED(sender_timestamp);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(reply);
	return 0U;
}

void ReferenceChatHarness::onContactResponse(const ContactInfo &contact,
					     const uint8_t *data, uint8_t len)
{
	uint32_t tag = 0U;

	if (!pending_telemetry_.valid || data == NULL || len < sizeof(tag)) {
		return;
	}

	memcpy(&tag, data, sizeof(tag));
	if (tag != pending_telemetry_.tag ||
	    memcmp(contact.id.pub_key, pending_telemetry_.public_key,
		   sizeof(pending_telemetry_.public_key)) != 0) {
		return;
	}

	memset(&last_telemetry_event_, 0, sizeof(last_telemetry_event_));
		last_telemetry_event_.published = true;
		memcpy(last_telemetry_event_.key_prefix, pending_telemetry_.key_prefix,
		       sizeof(last_telemetry_event_.key_prefix));
		last_telemetry_event_.tag = tag;
		last_telemetry_event_.payload_len =
			(uint8_t)(len - sizeof(tag));
	if (pending_telemetry_.expected_payload_len > 0U &&
	    last_telemetry_event_.payload_len >
		    pending_telemetry_.expected_payload_len) {
		last_telemetry_event_.payload_len =
			pending_telemetry_.expected_payload_len;
	}
	if (last_telemetry_event_.payload_len > 0U) {
		memcpy(last_telemetry_event_.payload, &data[sizeof(tag)],
		       last_telemetry_event_.payload_len);
	}
	telemetry_publish_count_++;
	memset(&pending_telemetry_, 0, sizeof(pending_telemetry_));
}

ContactInfo *ReferenceChatHarness::lookup_contact(const uint8_t *public_key)
{
	return public_key == NULL ? NULL :
		noop_contact_finder::find(this, public_key);
}

const ReferenceChatHarness::contact_meta *
ReferenceChatHarness::lookup_contact_meta(const uint8_t *public_key) const
{
	if (public_key == NULL) {
		return NULL;
	}

	for (const auto &meta : contact_meta_) {
		if (meta.used &&
		    memcmp(meta.public_key, public_key, sizeof(meta.public_key)) == 0) {
			return &meta;
		}
	}
	return NULL;
}

ReferenceChatHarness::contact_meta *
ReferenceChatHarness::lookup_contact_meta_mutable(const uint8_t *public_key)
{
	contact_meta *free_slot = NULL;

	if (public_key == NULL) {
		return NULL;
	}

	for (auto &meta : contact_meta_) {
		if (meta.used &&
		    memcmp(meta.public_key, public_key, sizeof(meta.public_key)) == 0) {
			return &meta;
		}
		if (!meta.used && free_slot == NULL) {
			free_slot = &meta;
		}
	}
	return free_slot;
}

const ReferenceChatHarness::contact_meta *
ReferenceChatHarness::lookup_contact_meta_by_hash(const uint8_t *hash,
						  size_t hash_len) const
{
	if (hash == NULL || hash_len == 0U) {
		return NULL;
	}

	for (const auto &meta : contact_meta_) {
		if (meta.used && memcmp(meta.public_key, hash, hash_len) == 0) {
			return &meta;
		}
	}
	return NULL;
}

bool ReferenceChatHarness::dispatch_inbound_packet(mesh::Packet *packet)
{
	mesh::DispatcherAction action;

	if (packet == NULL) {
		return false;
	}

	action = onRecvPacket(packet);
	if (action == ACTION_RELEASE) {
		releasePacket(packet);
		return true;
	}
	if (action == ACTION_MANUAL_HOLD) {
		return true;
	}

	_mgr->queueOutbound(packet, (uint8_t)((action >> 24) - 1U),
			    futureMillis((int)(action & 0xFFFFFFU)));
	return true;
}

bool ReferenceChatHarness::build_inbound_path_packet(
	const uint8_t *public_key, const uint8_t *out_path, uint8_t out_path_len,
	uint8_t path_hash_size, uint8_t extra_type, const uint8_t *extra,
	uint8_t extra_len, int8_t response_snr)
{
	mesh::Identity peer(public_key);
	mesh::Packet *packet;
	uint8_t secret[PUB_KEY_SIZE];
	uint8_t data[MAX_PACKET_PAYLOAD];
	uint8_t path_len_field;
	int payload_len = 0;
	int data_len = 0;

	if (public_key == NULL ||
	    !encode_path_len_field(path_hash_size, out_path_len, &path_len_field)) {
		return false;
	}

	packet = obtainNewPacket();
	if (packet == NULL) {
		return false;
	}

	packet->header = (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet->path_len = 0U;
	packet->_snr = response_snr;
	payload_len += self_id.copyHashTo(&packet->payload[payload_len]);
	payload_len += peer.copyHashTo(&packet->payload[payload_len]);

	data[0] = path_len_field;
	if (out_path != NULL && out_path_len > 0U) {
		memcpy(&data[1], out_path, out_path_len);
	}
	data_len = 1 + out_path_len;
	if (extra != NULL && extra_len > 0U) {
		data[data_len++] = extra_type;
		memcpy(&data[data_len], extra, extra_len);
		data_len += extra_len;
	}

	self_id.calcSharedSecret(secret, peer);
	payload_len += mesh::Utils::encryptThenMAC(secret,
						       &packet->payload[payload_len],
						       data, data_len);
	memset(secret, 0, sizeof(secret));
	packet->payload_len = payload_len;
	return dispatch_inbound_packet(packet);
}

bool ReferenceChatHarness::build_inbound_response_packet(
	const uint8_t *public_key, const uint8_t *data, size_t data_len,
	int8_t response_snr)
{
	mesh::Identity peer(public_key);
	mesh::Packet *packet;
	uint8_t secret[PUB_KEY_SIZE];
	int len = 0;

	if (public_key == NULL || data == NULL || data_len == 0U ||
	    data_len > MAX_PACKET_PAYLOAD) {
		return false;
	}

	packet = obtainNewPacket();
	if (packet == NULL) {
		return false;
	}

	packet->header =
		(PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT;
	packet->path_len = 0U;
	packet->_snr = response_snr;
	len += self_id.copyHashTo(&packet->payload[len]);
	len += peer.copyHashTo(&packet->payload[len]);
	self_id.calcSharedSecret(secret, peer);
	len += mesh::Utils::encryptThenMAC(secret, &packet->payload[len], data,
					      data_len);
	memset(secret, 0, sizeof(secret));
	packet->payload_len = len;
	return dispatch_inbound_packet(packet);
}

void ReferenceChatHarness::cleanup_expired_pending(void)
{
	if (pending_discovery_.valid &&
	    millis_clock_->now >= pending_discovery_.expires_at_ms) {
		memset(&pending_discovery_, 0, sizeof(pending_discovery_));
	}
	if (pending_trace_.valid &&
	    millis_clock_->now >= pending_trace_.expires_at_ms) {
		memset(&pending_trace_, 0, sizeof(pending_trace_));
	}
	if (pending_telemetry_.valid &&
	    millis_clock_->now >= pending_telemetry_.expires_at_ms) {
		memset(&pending_telemetry_, 0, sizeof(pending_telemetry_));
	}
}

void ReferenceChatHarness::copy_key_prefix(uint8_t *dest,
					   const uint8_t *public_key)
{
	if (dest == NULL || public_key == NULL) {
		return;
	}

	memcpy(dest, public_key, MESHCORE_NODE_KEY_PREFIX_BYTES);
}
