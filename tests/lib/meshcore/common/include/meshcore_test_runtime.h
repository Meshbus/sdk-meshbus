#ifndef FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_MESHCORE_TEST_RUNTIME_H_
#define FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_MESHCORE_TEST_RUNTIME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "meshcore/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct meshcore_hal_test_dispatcher_script {
	uint32_t recv_action;
	bool override_airtime_budget_factor;
	float airtime_budget_factor;
	bool override_rx_delay_ms;
	int rx_delay_ms;
	uint32_t cad_fail_retry_delay_ms;
	uint32_t cad_fail_max_duration_ms;
	int interference_threshold;
	int agc_reset_interval_ms;
	unsigned long duty_cycle_window_ms;
	int handled_count;
	int log_rx_raw_count;
	int log_rx_count;
	int log_tx_count;
	int log_tx_fail_count;
} meshcore_hal_test_dispatcher_script_t;

typedef struct meshcore_hal_test_mesh_script {
	bool override_filter_recv_flood_packet;
	bool filter_recv_flood_packet_value;
	bool override_allow_packet_forward;
	bool allow_packet_forward_value;
	bool override_get_retransmit_delay;
	uint32_t get_retransmit_delay_value;
	bool override_get_direct_retransmit_delay;
	uint32_t get_direct_retransmit_delay_value;
	bool override_get_extra_ack_transmit_count;
	uint8_t get_extra_ack_transmit_count_value;
	bool override_get_cad_fail_retry_delay;
	uint32_t get_cad_fail_retry_delay_value;
	bool override_search_peers_by_hash;
	int search_peers_by_hash_value;
	bool override_search_channels_by_hash;
	int search_channels_by_hash_value;
	bool override_get_peer_shared_secret;
	uint8_t peer_shared_secret[MESHCORE_PUBLIC_KEY_SIZE];
	bool override_search_channels_fill;
	uint8_t search_channel_hash[MESHCORE_CHANNEL_HASH_BYTES];
	uint8_t search_channel_secret[MESHCORE_PUBLIC_KEY_SIZE];
	bool override_on_peer_path_recv;
	bool on_peer_path_recv_value;

	uint32_t on_peer_data_recv_count;
	uint32_t on_trace_recv_count;
	uint32_t on_peer_path_recv_count;
	uint32_t on_advert_recv_count;
	uint32_t on_anon_data_recv_count;
	uint32_t on_control_data_recv_count;
	uint32_t on_raw_data_recv_count;
	uint32_t on_group_data_recv_count;
	uint32_t on_ack_recv_count;
	uint32_t last_ack_crc;
	uint8_t last_peer_data_type;
	uint16_t last_peer_data_len;
	uint8_t last_group_data_type;
	uint16_t last_group_data_len;
	uint16_t last_anon_data_len;
	uint8_t last_peer_path_len;
	uint8_t last_peer_path_extra_type;
	uint8_t last_peer_path_extra_len;
	uint32_t last_advert_timestamp;
	uint8_t last_advert_public_key[MESHCORE_PUBLIC_KEY_SIZE];
	uint16_t last_advert_app_data_len;
	uint8_t last_advert_app_data[32];
	uint8_t last_advert_path_len;
	uint32_t last_trace_tag;
	uint32_t last_trace_auth_code;
	uint8_t last_trace_flags;
} meshcore_hal_test_mesh_script_t;

typedef struct meshcore_test_message_event {
	meshcore_common_message_type_t type;
	meshcore_common_message_route_t route;
	uint8_t target[MESHCORE_MESSAGE_TARGET_PREFIX_BYTES];
	char sender_name[MESHCORE_MESSAGE_SENDER_NAME_MAX_LEN];
	uint16_t payload_len;
	uint8_t payload[MESHCORE_MAX_MESSAGE_TX_LEN];
	uint64_t sender_timestamp;
	bool has_rx_snr;
	float rx_snr;
} meshcore_test_message_event_t;

typedef struct meshcore_test_message_ack_event {
	uint8_t target[MESHCORE_MESSAGE_TARGET_PREFIX_BYTES];
	uint8_t attempt;
} meshcore_test_message_ack_event_t;

typedef struct meshcore_test_advert_event {
	char name[MESHCORE_NODE_NAME_MAX_LEN];
	meshcore_common_node_role_t role;
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
	bool is_new;
	uint32_t advert_timestamp;
	bool has_position;
	int32_t latitude;
	int32_t longitude;
	bool has_out_path;
	uint8_t out_path_len;
	uint8_t path_hash_size;
	uint8_t out_path[MESHCORE_MAX_PATH_LEN];
	uint8_t raw_advert_len;
	uint8_t raw_advert[MESHCORE_MAX_RAW_ADVERT_LEN];
} meshcore_test_advert_event_t;

typedef struct meshcore_test_peer_path_event {
	bool is_discover;
	uint32_t tag;
	uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
	uint32_t timestamp;
	bool has_response_snr;
	int8_t response_snr;
	bool has_out_path;
	uint8_t out_path_len;
	uint8_t path_hash_size;
	uint8_t out_path[MESHCORE_MAX_PATH_LEN];
	uint8_t out_path_snr_count;
	int8_t out_path_snr[MESHCORE_MAX_PATH_LEN];
	uint8_t return_path_snr_count;
	int8_t return_path_snr[MESHCORE_MAX_PATH_LEN];
} meshcore_test_peer_path_event_t;

typedef struct meshcore_test_trace_event {
	uint32_t timestamp;
	uint32_t tag;
	uint8_t state;
	bool has_response_snr;
	int8_t response_snr;
	uint8_t out_path_snr_count;
	int8_t out_path_snr[MESHCORE_MAX_PATH_LEN];
	uint8_t return_path_snr_count;
	int8_t return_path_snr[MESHCORE_MAX_PATH_LEN];
} meshcore_test_trace_event_t;

typedef struct meshcore_test_telemetry_event {
	uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	uint8_t payload_len;
	uint8_t payload[MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN];
} meshcore_test_telemetry_event_t;

typedef meshcore_common_binary_request_event_t meshcore_test_binary_request_event_t;

typedef struct meshcore_test_binary_response_event {
	uint8_t key_prefix[MESHCORE_NODE_KEY_PREFIX_BYTES];
	uint32_t timestamp;
	uint32_t tag;
	uint8_t payload_len;
	uint8_t payload[MESHCORE_MAX_SERVICE_RESPONSE_PAYLOAD_LEN];
} meshcore_test_binary_response_event_t;

typedef struct meshcore_test_node_discover_event {
	meshcore_common_node_role_t role;
	uint32_t tag;
	uint8_t public_key_len;
	uint8_t public_key[MESHCORE_PUBLIC_KEY_SIZE];
	uint8_t path_len;
	uint8_t path[MESHCORE_MAX_PATH_LEN];
	int8_t uplink_snr;
	int8_t downlink_snr;
} meshcore_test_node_discover_event_t;

/*
 * Private legacy host shim used by reference-model tests. These names are no
 * longer public MeshCore ABI; production hosts implement meshcore_platform_*
 * hooks declared by meshcore/platform.h.
 */
uint32_t meshcore_hal_radio_airtime(size_t len);
float meshcore_hal_radio_packet_score(int8_t snr_q4, size_t len);
void meshcore_hal_radio_begin(void);
bool meshcore_hal_radio_in_rx_mode_get(void);
bool meshcore_hal_radio_receiving_get(void);
bool meshcore_hal_radio_channel_active_get(void);
bool meshcore_hal_radio_frequency_get(uint64_t *freq_hz);
void meshcore_hal_radio_noise_floor_calibrate(int threshold);
void meshcore_hal_radio_agc_reset(void);
int meshcore_hal_radio_packet_send(const uint8_t *data, size_t len);
int meshcore_hal_node_telemetry_get(
	const meshcore_platform_request_source_t *requester,
	uint8_t permission_mask, meshcore_platform_telemetry_payload_t *out);
void meshcore_hal_rng_random(uint8_t *dest, size_t size);
unsigned long meshcore_hal_millis_get(void);
uint32_t meshcore_hal_rtc_get_current_time(void);
bool meshcore_hal_sha256(uint8_t *hash, size_t hash_len, const uint8_t *msg,
			 int msg_len);
bool meshcore_hal_sha256_two_fragments(uint8_t *hash, size_t hash_len,
				       const uint8_t *frag1, int frag1_len,
				       const uint8_t *frag2, int frag2_len);
bool meshcore_hal_aes128_encrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src);
bool meshcore_hal_aes128_decrypt_block(const uint8_t *key, uint8_t *dest,
				       const uint8_t *src);
bool meshcore_hal_hmac_sha256(uint8_t *mac, size_t mac_len,
			      const uint8_t *key, size_t key_len,
			      const uint8_t *msg, size_t msg_len);

int meshcore_node_identity_get(meshcore_common_node_identity_t *out);
int meshcore_node_config_last_modify_get(uint32_t *out_timestamp);
int meshcore_node_runtime_policy_get(
	meshcore_common_node_runtime_policy_t *out);
int meshcore_node_advert_profile_get(
	meshcore_common_node_advert_profile_t *out);
int meshcore_peer_identity_get_by_prefix(
	const uint8_t *key_prefix, meshcore_common_peer_identity_t *out);
int meshcore_peer_identity_match_by_hash(
	const uint8_t *hash, meshcore_common_peer_identity_t *out,
	size_t out_capacity);
int meshcore_peer_path_get_by_key(const uint8_t *public_key,
				  meshcore_common_peer_path_t *out);
int meshcore_peer_path_update(const uint8_t *public_key,
			      const meshcore_common_peer_path_t *path);
int meshcore_peer_seen_update(const uint8_t *public_key, bool has_snr,
			      int8_t snr_q4);

int meshcore_channel_secret_get_by_hash(
	uint8_t channel_hash, meshcore_common_channel_secret_t *out,
	size_t out_capacity);
int meshcore_channel_secret_match_exists(uint8_t channel_hash,
					 const uint8_t *secret,
					 size_t secret_len);
int meshcore_channel_secret_hash(const uint8_t *secret, size_t secret_len,
				 uint8_t *out_hash);
int meshcore_channel_payload_encrypt(
	const uint8_t *secret, size_t secret_len, const uint8_t *plaintext,
	size_t plaintext_len, uint8_t *out_ciphertext,
	size_t *inout_ciphertext_len);
int meshcore_channel_payload_decrypt(
	const uint8_t *secret, size_t secret_len, const uint8_t *ciphertext,
	size_t ciphertext_len, uint8_t *out_plaintext,
	size_t *inout_plaintext_len);

void meshcore_dispatcher_log_rx_raw(float snr, float rssi,
				    const uint8_t raw[], int len);
void meshcore_dispatcher_log_rx(const meshcore_common_packet_view_t *packet,
				int len, float score);
void meshcore_dispatcher_log_tx(const meshcore_common_packet_view_t *packet,
				int len);
void meshcore_dispatcher_log_tx_fail(
	const meshcore_common_packet_view_t *packet, int len);
void meshcore_runtime_request_error(uint8_t request_type, int err_code);
float meshcore_dispatcher_get_airtime_budget_factor(void);
int meshcore_dispatcher_calc_rx_delay(float score, uint32_t air_time);
uint32_t meshcore_dispatcher_get_cad_fail_max_duration(void);
int meshcore_dispatcher_get_interference_threshold(void);
int meshcore_dispatcher_get_agc_reset_interval(void);
unsigned long meshcore_dispatcher_get_duty_cycle_window_ms(void);

int meshcore_peer_payload_encrypt(const uint8_t *peer_public_key,
				  const uint8_t *plaintext,
				  size_t plaintext_len,
				  uint8_t *out_ciphertext,
				  size_t *inout_ciphertext_len);
int meshcore_peer_payload_decrypt(const uint8_t *peer_public_key,
				  const uint8_t *ciphertext,
				  size_t ciphertext_len,
				  uint8_t *out_plaintext,
				  size_t *inout_plaintext_len);

uint32_t meshcore_mesh_get_cad_fail_retry_delay(void);
bool meshcore_mesh_filter_recv_flood_packet(
	const meshcore_common_packet_view_t *packet);
bool meshcore_mesh_allow_packet_forward(
	const meshcore_common_packet_view_t *packet);
uint32_t meshcore_mesh_get_retransmit_delay(
	const meshcore_common_packet_view_t *packet);
uint32_t meshcore_mesh_get_direct_retransmit_delay(
	const meshcore_common_packet_view_t *packet);
uint8_t meshcore_mesh_get_extra_ack_transmit_count(void);
int meshcore_mesh_next_peer_shared_secret_by_hash(
	const uint8_t *hash, size_t start_slot, size_t *slot_id,
	uint8_t *dest_secret, meshcore_common_peer_identity_t *peer_identity);
int meshcore_mesh_search_channels_by_hash(
	const uint8_t *hash, meshcore_common_channel_view_t *channels,
	int max_matches);
void meshcore_mesh_on_peer_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *data, size_t len);
void meshcore_mesh_on_trace_recv(
	const meshcore_common_packet_view_t *packet, uint32_t tag,
	uint32_t auth_code, uint8_t flags, const uint8_t *path_snrs,
	const uint8_t *path_hashes, uint8_t path_len);
bool meshcore_mesh_on_peer_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_peer_identity_t *sender, const uint8_t *secret,
	uint8_t *path, uint8_t path_len, uint8_t extra_type, uint8_t *extra,
	uint8_t extra_len);
void meshcore_mesh_on_advert_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *identity, uint32_t timestamp,
	const uint8_t *app_data, size_t app_data_len);
void meshcore_mesh_on_anon_data_recv(
	const meshcore_common_packet_view_t *packet, const uint8_t *secret,
	const meshcore_common_identity_view_t *sender, uint8_t *data, size_t len);
void meshcore_mesh_on_path_recv(
	const meshcore_common_packet_view_t *packet,
	const meshcore_common_identity_view_t *sender, uint8_t *path,
	uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len);
void meshcore_mesh_on_control_data_recv(
	const meshcore_common_packet_view_t *packet);
void meshcore_mesh_on_raw_data_recv(const meshcore_common_packet_view_t *packet);
void meshcore_mesh_on_group_data_recv(
	const meshcore_common_packet_view_t *packet, uint8_t type,
	const meshcore_common_channel_view_t *channel, uint8_t *data, size_t len);
void meshcore_mesh_on_ack_recv(const meshcore_common_packet_view_t *packet,
			       uint32_t ack_crc);

int meshcore_message_handler(const meshcore_common_message_t *message);
int meshcore_message_ack_handler(const uint8_t *target, uint8_t attempt);
int meshcore_advert_handler(const meshcore_common_advert_event_t *advert);
int meshcore_peer_path_publish(
	const meshcore_common_peer_path_event_t *peer_path, bool is_discover);
int meshcore_peer_path_handler(
	const meshcore_common_peer_path_event_t *peer_path);
int meshcore_trace_path_handler(uint8_t state, uint32_t tag,
				const int8_t *out_path_snr, uint8_t out_count,
				const int8_t *return_path_snr,
				uint8_t return_count, bool has_response_snr,
				int8_t response_snr, uint32_t timestamp);
int meshcore_telemetry_handler(const uint8_t *key_prefix, uint32_t timestamp,
			       uint32_t tag, const uint8_t *payload, size_t payload_len);
int meshcore_binary_request_handler(
	const meshcore_common_binary_request_event_t *event);
int meshcore_binary_response_handler(const uint8_t *key_prefix,
				     uint32_t timestamp, uint32_t tag,
				     const uint8_t *payload, size_t payload_len);
int meshcore_node_discover_handler(
	const meshcore_common_node_discover_event_t *event);
int meshcore_channel_data_handler(
	const meshcore_common_channel_data_event_t *event);
int meshcore_raw_data_handler(const meshcore_common_raw_data_event_t *event);
int meshcore_control_data_handler(
	const meshcore_common_control_data_event_t *event);

void meshcore_hal_test_host_state_reset(void);
meshcore_hal_test_dispatcher_script_t *meshcore_hal_test_dispatcher_script_get(void);
meshcore_hal_test_mesh_script_t *meshcore_hal_test_mesh_script_get(void);
void meshcore_hal_test_dispatcher_script_mirror_set(
	meshcore_hal_test_dispatcher_script_t *mirror);
void meshcore_hal_test_mesh_script_mirror_set(
	meshcore_hal_test_mesh_script_t *mirror);

void meshcore_hal_test_rng_set_bytes(const uint8_t *src, size_t len);
void meshcore_hal_test_rng_clear(void);
void meshcore_hal_test_crypto_set_encrypt_fail(bool enabled);
void meshcore_hal_test_crypto_set_hmac_fail(bool enabled);

void meshcore_hal_test_peer_identity_set(const uint8_t *public_key, const char *name,
						 meshcore_common_node_role_t role, uint8_t flags);
void meshcore_hal_test_peer_path_set(const uint8_t *public_key, const uint8_t *path,
				     uint8_t path_len, uint8_t path_hash_size);
void meshcore_hal_test_peer_path_unknown_set(const uint8_t *public_key);
void meshcore_hal_test_channel_secret_set(const uint8_t *secret, uint8_t secret_len);
void meshcore_hal_test_node_identity_set(const char *name,
					 meshcore_common_node_role_t role,
					 const uint8_t *public_key,
					 const uint8_t *private_key);
void meshcore_hal_test_node_runtime_policy_set(
	const meshcore_common_node_runtime_policy_t *policy);
void meshcore_hal_test_node_advert_profile_set(
	const meshcore_common_node_advert_profile_t *profile);

void meshcore_hal_test_publish_events_clear(void);
const meshcore_test_message_event_t *meshcore_hal_test_message_event_last(void);
const meshcore_test_message_ack_event_t *meshcore_hal_test_ack_event_last(void);
const meshcore_test_peer_path_event_t *meshcore_hal_test_peer_path_event_last(void);
const meshcore_test_trace_event_t *meshcore_hal_test_trace_event_last(void);
const meshcore_test_telemetry_event_t *meshcore_hal_test_telemetry_event_last(void);
const meshcore_test_binary_request_event_t *
meshcore_hal_test_binary_request_event_last(void);
const meshcore_test_binary_response_event_t *
meshcore_hal_test_binary_response_event_last(void);
const meshcore_test_node_discover_event_t *
meshcore_hal_test_node_discover_event_last(void);
const meshcore_test_advert_event_t *meshcore_hal_test_advert_event_last(void);
bool meshcore_hal_test_message_event_take(meshcore_test_message_event_t *out);
bool meshcore_hal_test_ack_event_take(meshcore_test_message_ack_event_t *out);
bool meshcore_hal_test_peer_path_event_take(meshcore_test_peer_path_event_t *out);
bool meshcore_hal_test_trace_event_take(meshcore_test_trace_event_t *out);
bool meshcore_hal_test_telemetry_event_take(meshcore_test_telemetry_event_t *out);
bool meshcore_hal_test_binary_request_event_take(
	meshcore_test_binary_request_event_t *out);
bool meshcore_hal_test_binary_response_event_take(
	meshcore_test_binary_response_event_t *out);
bool meshcore_hal_test_node_discover_event_take(
	meshcore_test_node_discover_event_t *out);
bool meshcore_hal_test_advert_event_take(meshcore_test_advert_event_t *out);

void meshcore_hal_test_millis_set(unsigned long value);
void meshcore_hal_test_millis_advance(unsigned long delta);
void meshcore_hal_test_millis_clear(void);
void meshcore_hal_test_timer_reset(void);
uint32_t meshcore_hal_test_timer_arm_count_get(void);
uint32_t meshcore_hal_test_timer_cancel_count_get(void);
uint32_t meshcore_hal_test_timer_last_deadline_get(void);
void meshcore_hal_test_rtc_set_current_time(uint32_t value);
void meshcore_hal_test_rtc_advance(uint32_t delta);
uint32_t meshcore_hal_test_rtc_get_current_time(void);

void meshcore_hal_test_radio_reset(void);
bool meshcore_hal_test_radio_inject_receive(const uint8_t *data, int len,
                                            int16_t rssi_dbm, int8_t snr_db,
                                            uint32_t delay_ms);
int meshcore_hal_test_radio_recv_raw(uint8_t *data, size_t capacity);
bool meshcore_hal_test_radio_tx_complete_get(void);
void meshcore_hal_test_radio_on_send_finished(void);
void meshcore_hal_test_radio_loop(void);
float meshcore_hal_test_radio_last_rssi_get(void);
float meshcore_hal_test_radio_last_snr_get(void);
void meshcore_hal_test_radio_set_send_delay_per_byte(uint32_t per_byte_ms);
void meshcore_hal_test_radio_set_send_result(bool success);
void meshcore_hal_test_radio_set_send_never_complete(bool enabled);
void meshcore_hal_test_radio_force_in_rx_mode(bool enabled, bool value);
void meshcore_hal_test_radio_force_receiving(bool enabled, bool value);
uint32_t meshcore_hal_test_radio_get_packets_recv(void);
uint32_t meshcore_hal_test_radio_get_packets_sent(void);
uint32_t meshcore_hal_test_radio_get_noise_floor_calibrate_count(void);
uint32_t meshcore_hal_test_radio_get_agc_reset_count(void);
uint32_t meshcore_hal_test_radio_get_on_send_finished_count(void);
int meshcore_hal_test_radio_get_last_send_len(void);
int meshcore_hal_test_radio_get_last_send(uint8_t *out, size_t capacity);

/*
 * Runtime white-box hooks. Test targets that link runtime sources must compile
 * those sources with MESHCORE_ENABLE_TEST_HOOKS.
 */
bool meshcore_test_runtime_context_is_default(void);
bool meshcore_test_runtime_is_initialized(void);
unsigned long meshcore_test_runtime_dispatcher_last_budget_update_get(void);
unsigned long meshcore_test_runtime_dispatcher_next_floor_calib_time_get(void);
uint32_t meshcore_test_runtime_dispatcher_num_sent_flood_get(void);
uint32_t meshcore_test_runtime_dispatcher_num_sent_direct_get(void);
int meshcore_test_runtime_dispatcher_outbound_total_get(void);
bool meshcore_test_runtime_dispatcher_has_active_outbound(void);
uint32_t meshcore_test_runtime_last_now_ms_get(void);
int meshcore_test_runtime_expected_ack_used_count_get(void);
bool meshcore_test_runtime_expected_ack_peek(uint32_t *ack_crc, uint8_t *target,
					     uint8_t *attempt,
					     unsigned long *expires_at_ms);
bool meshcore_test_runtime_pending_discovery_is_valid(void);
bool meshcore_test_runtime_pending_discovery_get(uint32_t *tag,
						 uint8_t *key_prefix,
						 unsigned long *expires_at_ms);
bool meshcore_test_runtime_pending_trace_is_valid(void);
bool meshcore_test_runtime_pending_trace_get(uint32_t *tag,
					     unsigned long *expires_at_ms);
bool meshcore_test_runtime_pending_telemetry_is_valid(void);
bool meshcore_test_runtime_pending_telemetry_get(uint32_t *tag,
						 uint8_t *key_prefix,
						 uint8_t *permission_mask,
						 unsigned long *expires_at_ms);
bool meshcore_test_runtime_pending_binary_is_valid(void);
bool meshcore_test_runtime_pending_binary_get(uint32_t *tag,
					      uint8_t *key_prefix,
					      unsigned long *expires_at_ms);
bool meshcore_test_runtime_simulate_ack_recv(uint32_t ack_crc);
bool meshcore_test_runtime_simulate_peer_path_recv(
	const uint8_t *key_prefix, const uint8_t *out_path, uint8_t out_path_len,
	uint8_t path_hash_size, uint8_t extra_type, const int8_t *out_path_snr,
	uint8_t out_path_snr_count, const int8_t *return_path_snr,
	uint8_t return_path_snr_count, int8_t response_snr);
bool meshcore_test_runtime_simulate_trace_recv(uint32_t tag, uint8_t flags,
					       const int8_t *path_snrs,
					       uint8_t path_snr_count,
					       int8_t response_snr);
bool meshcore_test_runtime_simulate_telemetry_response_recv(
	const uint8_t *key_prefix, uint32_t tag, const uint8_t *payload,
	size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_MESHCORE_TEST_RUNTIME_H_ */
