// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_REFERENCE_CHAT_HARNESS_H_
#define FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_REFERENCE_CHAT_HARNESS_H_

#include <stddef.h>
#include <stdint.h>

#include <helpers/BaseChatMesh.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>

extern "C" {
#include "meshcore/runtime.h"
#include "meshcore/types.h"
#include "meshcore_identity.h"
}

class ReferenceChatHarness : public BaseChatMesh {
public:
	enum route_kind {
		ROUTE_NONE = 0,
		ROUTE_ZERO_HOP,
		ROUTE_FLOOD,
		ROUTE_DIRECT,
	};

	struct observed_packet {
		bool sent;
		int raw_len;
		uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN];
		route_kind route;
		bool has_transport_codes;
		uint8_t payload_type;
	};

	struct observed_peer_path_event {
		bool published;
		bool is_discover;
		uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
		int8_t response_snr;
		uint8_t out_path[MESHCORE_MAX_PATH_LEN];
		uint8_t out_path_len;
		uint8_t path_hash_size;
		int8_t out_path_snr[MESHCORE_MAX_PATH_LEN];
		uint8_t out_path_snr_count;
		int8_t return_path_snr[MESHCORE_MAX_PATH_LEN];
		uint8_t return_path_snr_count;
	};

	struct observed_trace_event {
		bool published;
		uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
		uint32_t tag;
		uint8_t flags;
		uint8_t state;
		int8_t response_snr;
		int8_t out_path_snr[MESHCORE_MAX_PATH_LEN];
		uint8_t out_path_snr_count;
		int8_t return_path_snr[MESHCORE_MAX_PATH_LEN];
		uint8_t return_path_snr_count;
	};

		struct observed_telemetry_event {
			bool published;
			uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
			uint32_t tag;
			uint8_t payload[MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN];
			uint8_t payload_len;
		};

	struct observed_advert_event {
		bool observed;
		bool is_new;
		uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
		char name[32];
		uint8_t type;
		uint32_t advert_timestamp;
		bool has_position;
		int32_t latitude;
		int32_t longitude;
		uint8_t path_len;
		uint8_t path[MESHCORE_MAX_PATH_LEN];
	};

	ReferenceChatHarness();

	void reset();
	void set_self_identity(const struct meshcore_local_identity &identity);
	void set_node_name(const char *name);
	void set_advert_profile(bool has_position, double latitude, double longitude);
	void set_local_path_hash_size(uint8_t path_hash_size);
	void set_random_bytes(const uint8_t *data, size_t len);
	void set_require_local_identity_for_send(bool enabled);
	void set_require_known_group_channel(bool enabled);
	bool add_known_group_channel_secret(const uint8_t *secret, size_t secret_len);
	bool upsert_contact(const uint8_t *public_key, const char *name, uint8_t type);
	bool set_contact_out_path(const uint8_t *public_key, const uint8_t *path,
				  uint8_t path_len_bytes, uint8_t path_hash_size);
	bool send_self_advert(bool flood);
	bool replay_peer_advert_zero_hop(const uint8_t *raw, size_t len);
	bool send_message_to_contact(const uint8_t *public_key, bool flood, uint8_t attempt,
				     const uint8_t *payload, size_t payload_len);
	bool send_group_message(const uint8_t *secret, size_t secret_len,
				const uint8_t *payload, size_t payload_len);
	bool send_group_data(const uint8_t *secret, size_t secret_len,
			     const uint8_t *path, uint8_t path_len,
			     uint16_t data_type, const uint8_t *payload,
			     size_t payload_len);
	bool send_discover_request(const uint8_t *public_key);
	bool send_trace_request(const uint8_t *public_key);
	bool send_telemetry_request(const uint8_t *public_key, uint8_t permission_mask);
	bool send_binary_request(const uint8_t *public_key, const uint8_t *payload,
				 size_t payload_len);
	bool inject_discover_response(const uint8_t *public_key,
				      const uint8_t *out_path, uint8_t out_path_len,
				      uint8_t path_hash_size,
				      const int8_t *out_path_snr,
				      uint8_t out_path_snr_count,
				      const int8_t *return_path_snr,
				      uint8_t return_path_snr_count,
				      int8_t response_snr);
	bool inject_discover_response_for(const uint8_t *public_key,
					  const uint8_t *out_path,
					  uint8_t out_path_len,
					  uint8_t path_hash_size,
					  uint8_t extra_type,
					  const uint8_t *extra,
					  uint8_t extra_len,
					  int8_t response_snr);
	bool inject_trace_result(uint8_t flags, const int8_t *path_snrs,
				 uint8_t path_snr_count, int8_t response_snr);
	bool inject_trace_result_for(uint32_t tag, uint8_t flags,
				     const int8_t *path_snrs,
				     uint8_t path_snr_count, int8_t response_snr);
	bool inject_telemetry_response(const uint8_t *public_key,
				       const uint8_t *payload, size_t payload_len,
				       int8_t response_snr);
	bool inject_telemetry_response_for(const uint8_t *public_key, uint32_t tag,
					   const uint8_t *payload, size_t payload_len,
					   int8_t response_snr);
	bool inject_raw_packet(const uint8_t *raw, size_t len);
	bool inject_advert(const uint8_t *raw, size_t len);
	bool build_inbound_path_raw(const uint8_t *public_key,
				    const uint8_t *out_path, uint8_t out_path_len,
				    uint8_t path_hash_size, uint8_t extra_type,
				    const uint8_t *extra, uint8_t extra_len,
				    int8_t response_snr, observed_packet *out);
	bool build_inbound_response_raw(const uint8_t *public_key,
					const uint8_t *data, size_t data_len,
					int8_t response_snr, observed_packet *out);
	bool drive_until_packets_sent(uint32_t expected_packets);
	bool capture_last_packet(observed_packet *out) const;
	bool capture_last_peer_path_event(observed_peer_path_event *out) const;
	bool capture_last_trace_event(observed_trace_event *out) const;
	bool capture_last_telemetry_event(observed_telemetry_event *out) const;
	bool capture_last_advert_event(observed_advert_event *out) const;
	uint32_t sent_packets() const;
	uint32_t peer_path_publish_count() const;
	uint32_t trace_publish_count() const;
	uint32_t telemetry_publish_count() const;
	uint32_t advert_observed_count() const;
	uint32_t contact_path_update_count() const;
	bool pending_discovery_active() const;
	bool pending_discovery_get(uint8_t *key_prefix,
				   uint32_t *tag = nullptr) const;
	bool pending_trace_active() const;
	bool pending_trace_get(uint32_t *tag, uint8_t *key_prefix) const;
	bool pending_telemetry_active() const;
	bool pending_telemetry_get(uint32_t *tag, uint8_t *key_prefix,
				   uint8_t *permission_mask) const;
	unsigned long pending_discovery_expires_at() const;
	unsigned long pending_trace_expires_at() const;
	unsigned long pending_telemetry_expires_at() const;
	void advance_time(unsigned long delta_ms);

protected:
	void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code,
			 uint8_t flags, const uint8_t *path_snrs,
			 const uint8_t *path_hashes, uint8_t path_len) override;
	void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len,
				 const uint8_t *path) override;
	ContactInfo *processAck(const uint8_t *data) override;
	void onContactPathUpdated(const ContactInfo &contact) override;
	void onMessageRecv(const ContactInfo &contact, mesh::Packet *pkt,
			   uint32_t sender_timestamp, const char *text) override;
	void onCommandDataRecv(const ContactInfo &contact, mesh::Packet *pkt,
			       uint32_t sender_timestamp, const char *text) override;
	void onSignedMessageRecv(const ContactInfo &contact, mesh::Packet *pkt,
				 uint32_t sender_timestamp, const uint8_t *sender_prefix,
				 const char *text) override;
	uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
	uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis,
					    uint8_t path_len) const override;
	void onSendTimeout() override;
	void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt,
				  uint32_t timestamp, const char *text) override;
	uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp,
				 const uint8_t *data, uint8_t len,
				 uint8_t *reply) override;
	void onContactResponse(const ContactInfo &contact, const uint8_t *data,
			       uint8_t len) override;

private:
	class harness_millis_clock;
	class harness_rtc_clock;
	class harness_rng;
	class harness_radio;

	struct contact_meta {
		bool used;
		uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
		uint8_t path_hash_size;
	};

	struct pending_discovery_state {
		bool valid;
		uint32_t tag;
		uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
		unsigned long expires_at_ms;
	};

	struct pending_trace_state {
		bool valid;
		uint32_t tag;
		uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
		unsigned long expires_at_ms;
	};

	struct pending_telemetry_state {
		bool valid;
		uint32_t tag;
		uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
		uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
		uint8_t permission_mask;
		uint8_t expected_payload_len;
		unsigned long expires_at_ms;
	};

	struct known_group_channel_secret {
		bool used;
		uint8_t secret[MESHCORE_CHANNEL_SECRET_MAX_LEN];
		uint8_t secret_len;
	};

	ContactInfo *lookup_contact(const uint8_t *public_key);
	const contact_meta *lookup_contact_meta(const uint8_t *public_key) const;
	contact_meta *lookup_contact_meta_mutable(const uint8_t *public_key);
	const contact_meta *lookup_contact_meta_by_hash(const uint8_t *hash,
							size_t hash_len) const;
	bool dispatch_inbound_packet(mesh::Packet *packet);
	bool build_inbound_path_packet(const uint8_t *public_key, const uint8_t *out_path,
				       uint8_t out_path_len, uint8_t path_hash_size,
				       uint8_t extra_type, const uint8_t *extra,
				       uint8_t extra_len, int8_t response_snr);
	bool build_inbound_response_packet(const uint8_t *public_key,
					   const uint8_t *data, size_t data_len,
					   int8_t response_snr);
	void cleanup_expired_pending(void);
	static void copy_key_prefix(uint8_t *dest, const uint8_t *public_key);
	bool build_group_channel(const uint8_t *secret, size_t secret_len,
				 mesh::GroupChannel *out) const;
	bool has_known_group_channel_secret(const uint8_t *secret,
					    size_t secret_len) const;
	bool has_local_identity_for_send() const;

	harness_millis_clock *millis_clock_;
	harness_rtc_clock *rtc_clock_;
	harness_rng *rng_;
	harness_radio *radio_;
	StaticPoolPacketManager packet_manager_;
	SimpleMeshTables tables_;
	char node_name_[32];
	bool advert_has_position_;
	double advert_latitude_;
	double advert_longitude_;
	uint8_t local_path_hash_size_;
	bool require_local_identity_for_send_;
	bool require_known_group_channel_;
	known_group_channel_secret known_group_channels_[8];
	contact_meta contact_meta_[8];
	pending_discovery_state pending_discovery_;
	pending_trace_state pending_trace_;
	pending_telemetry_state pending_telemetry_;
	observed_peer_path_event last_peer_path_event_;
	observed_trace_event last_trace_event_;
	observed_telemetry_event last_telemetry_event_;
	observed_advert_event last_advert_event_;
	uint32_t peer_path_publish_count_;
	uint32_t trace_publish_count_;
	uint32_t telemetry_publish_count_;
	uint32_t advert_observed_count_;
	uint32_t contact_path_update_count_;
};

#endif /* FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_REFERENCE_CHAT_HARNESS_H_ */
