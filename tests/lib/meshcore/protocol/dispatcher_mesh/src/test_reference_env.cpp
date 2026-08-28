// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"

#include <string.h>

inline void *operator new(size_t, void *ptr) noexcept
{
	return ptr;
}

namespace meshcore_dispatcher_mesh_tdd {

static ReferenceEnv *g_reference_env;
static bool g_reference_env_inited;
alignas(ReferenceEnv) static unsigned char g_reference_env_storage[sizeof(ReferenceEnv)];

unsigned long CommonMillisecondClock::getMillis()
{
	return meshcore_hal_millis_get();
}

void CommonFakeRadio::begin()
{
	meshcore_hal_radio_begin();
}

int CommonFakeRadio::recvRaw(uint8_t *bytes, int sz)
{
	return meshcore_hal_test_radio_recv_raw(bytes, (size_t)sz);
}

uint32_t CommonFakeRadio::getEstAirtimeFor(int len_bytes)
{
	return meshcore_hal_radio_airtime((size_t)len_bytes);
}

float CommonFakeRadio::packetScore(float snr, int packet_len)
{
	return meshcore_hal_radio_packet_score((int8_t)(snr * 4.0f),
					       (size_t)packet_len);
}

bool CommonFakeRadio::startSendRaw(const uint8_t *bytes, int len)
{
	return meshcore_hal_radio_packet_send(bytes, (size_t)len) != 0;
}

bool CommonFakeRadio::isSendComplete()
{
	return meshcore_hal_test_radio_tx_complete_get();
}

void CommonFakeRadio::onSendFinished()
{
	meshcore_hal_test_radio_on_send_finished();
}

void CommonFakeRadio::loop()
{
	meshcore_hal_test_radio_loop();
}

void CommonFakeRadio::triggerNoiseFloorCalibrate(int threshold)
{
	meshcore_hal_radio_noise_floor_calibrate(threshold);
}

void CommonFakeRadio::resetAGC()
{
	meshcore_hal_radio_agc_reset();
}

bool CommonFakeRadio::isInRecvMode() const
{
	return meshcore_hal_radio_in_rx_mode_get();
}

bool CommonFakeRadio::isReceiving()
{
	return meshcore_hal_radio_receiving_get();
}

float CommonFakeRadio::getLastRSSI() const
{
	return meshcore_hal_test_radio_last_rssi_get();
}

float CommonFakeRadio::getLastSNR() const
{
	return meshcore_hal_test_radio_last_snr_get();
}

void CommonRng::random(uint8_t *dest, size_t sz)
{
	meshcore_hal_rng_random(dest, sz);
}

uint32_t CommonRtcClock::getCurrentTime()
{
	return meshcore_hal_rtc_get_current_time();
}

void CommonRtcClock::setCurrentTime(uint32_t time)
{
	meshcore_hal_test_rtc_set_current_time(time);
}

ReferenceHarness::ReferenceHarness(mesh::Radio &radio,
				   mesh::MillisecondClock &clock,
				   mesh::RNG &rng, mesh::RTCClock &rtc,
				   mesh::PacketManager &manager,
				   mesh::MeshTables &tables, MeshScript *script)
	: mesh::Mesh(radio, clock, rng, rtc, manager, tables), script_(script)
{
}

uint16_t ReferenceHarness::getErrFlags() const
{
	return _err_flags;
}

bool ReferenceHarness::filterRecvFloodPacket(mesh::Packet *packet)
{
	if (script_->override_filter_recv_flood_packet) {
		return script_->filter_recv_flood_packet_value;
	}
	return mesh::Mesh::filterRecvFloodPacket(packet);
}

bool ReferenceHarness::allowPacketForward(const mesh::Packet *packet)
{
	if (script_->override_allow_packet_forward) {
		return script_->allow_packet_forward_value;
	}
	return mesh::Mesh::allowPacketForward(packet);
}

uint32_t ReferenceHarness::getRetransmitDelay(const mesh::Packet *packet)
{
	if (script_->override_get_retransmit_delay) {
		return script_->get_retransmit_delay_value;
	}
	return mesh::Mesh::getRetransmitDelay(packet);
}

uint32_t ReferenceHarness::getDirectRetransmitDelay(const mesh::Packet *packet)
{
	if (script_->override_get_direct_retransmit_delay) {
		return script_->get_direct_retransmit_delay_value;
	}
	return mesh::Mesh::getDirectRetransmitDelay(packet);
}

uint8_t ReferenceHarness::getExtraAckTransmitCount() const
{
	if (script_->override_get_extra_ack_transmit_count) {
		return script_->get_extra_ack_transmit_count_value;
	}
	return mesh::Mesh::getExtraAckTransmitCount();
}

uint32_t ReferenceHarness::getCADFailRetryDelay() const
{
	if (script_->override_get_cad_fail_retry_delay) {
		return script_->get_cad_fail_retry_delay_value;
	}
	return mesh::Mesh::getCADFailRetryDelay();
}

int ReferenceHarness::searchPeersByHash(const uint8_t *hash)
{
	if (script_->override_search_peers_by_hash) {
		(void)hash;
		return script_->search_peers_by_hash_value;
	}
	return mesh::Mesh::searchPeersByHash(hash);
}

void ReferenceHarness::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx)
{
	(void)peer_idx;
	if (script_->override_get_peer_shared_secret) {
		memcpy(dest_secret, script_->peer_shared_secret,
		       sizeof(script_->peer_shared_secret));
		return;
	}
	mesh::Mesh::getPeerSharedSecret(dest_secret, peer_idx);
}

void ReferenceHarness::onPeerDataRecv(mesh::Packet *packet, uint8_t type,
				      int sender_idx, const uint8_t *secret,
				      uint8_t *data, size_t len)
{
	(void)packet;
	(void)sender_idx;
	(void)secret;
	(void)data;
	script_->on_peer_data_recv_count++;
	script_->last_peer_data_type = type;
	script_->last_peer_data_len = (uint16_t)len;
}

void ReferenceHarness::onTraceRecv(mesh::Packet *packet, uint32_t tag,
				   uint32_t auth_code, uint8_t flags,
				   const uint8_t *path_snrs,
				   const uint8_t *path_hashes, uint8_t path_len)
{
	(void)packet;
	(void)path_snrs;
	(void)path_hashes;
	(void)path_len;
	script_->on_trace_recv_count++;
	script_->last_trace_tag = tag;
	script_->last_trace_auth_code = auth_code;
	script_->last_trace_flags = flags;
}

bool ReferenceHarness::onPeerPathRecv(mesh::Packet *packet, int sender_idx,
				      const uint8_t *secret, uint8_t *path,
				      uint8_t path_len, uint8_t extra_type,
				      uint8_t *extra, uint8_t extra_len)
{
	(void)packet;
	(void)sender_idx;
	(void)secret;
	(void)path;
	(void)extra;
	script_->on_peer_path_recv_count++;
	script_->last_peer_path_len = path_len;
	script_->last_peer_path_extra_type = extra_type;
	script_->last_peer_path_extra_len = extra_len;
	if (script_->override_on_peer_path_recv) {
		return script_->on_peer_path_recv_value;
	}
	return mesh::Mesh::onPeerPathRecv(packet, sender_idx, secret, path, path_len,
					  extra_type, extra, extra_len);
}

void ReferenceHarness::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id,
				    uint32_t timestamp, const uint8_t *app_data,
				    size_t app_data_len)
{
	(void)packet;
	(void)id;
	(void)timestamp;
	(void)app_data;
	(void)app_data_len;
	script_->on_advert_recv_count++;
}

void ReferenceHarness::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret,
				      const mesh::Identity &sender, uint8_t *data,
				      size_t len)
{
	(void)packet;
	(void)secret;
	(void)sender;
	(void)data;
	script_->on_anon_data_recv_count++;
	script_->last_anon_data_len = (uint16_t)len;
}

void ReferenceHarness::onControlDataRecv(mesh::Packet *packet)
{
	(void)packet;
	script_->on_control_data_recv_count++;
}

void ReferenceHarness::onRawDataRecv(mesh::Packet *packet)
{
	(void)packet;
	script_->on_raw_data_recv_count++;
}

int ReferenceHarness::searchChannelsByHash(const uint8_t *hash,
					   mesh::GroupChannel channels[],
					   int max_matches)
{
	if (script_->override_search_channels_by_hash) {
		(void)hash;
		if (script_->override_search_channels_fill && max_matches > 0) {
			memcpy(channels[0].hash, script_->search_channel_hash,
			       sizeof(channels[0].hash));
			memcpy(channels[0].secret, script_->search_channel_secret,
			       sizeof(channels[0].secret));
		}
		return script_->search_channels_by_hash_value;
	}
	return mesh::Mesh::searchChannelsByHash(hash, channels, max_matches);
}

void ReferenceHarness::onGroupDataRecv(mesh::Packet *packet, uint8_t type,
				       const mesh::GroupChannel &channel,
				       uint8_t *data, size_t len)
{
	(void)packet;
	(void)channel;
	(void)data;
	script_->on_group_data_recv_count++;
	script_->last_group_data_type = type;
	script_->last_group_data_len = (uint16_t)len;
}

void ReferenceHarness::onAckRecv(mesh::Packet *packet, uint32_t ack_crc)
{
	(void)packet;
	script_->on_ack_recv_count++;
	script_->last_ack_crc = ack_crc;
}

ReferenceEnv::ReferenceEnv()
	: script(default_script()),
	  manager(kPoolSize),
	  mesh(radio, clock, rng, rtc, manager, tables, &script)
{
}

static void reference_env_settle_and_drain(ReferenceEnv &env)
{
	env.script = default_script();

	meshcore_hal_test_radio_set_send_result(true);
	meshcore_hal_test_radio_set_send_never_complete(false);
	meshcore_hal_test_radio_force_in_rx_mode(false, false);
	meshcore_hal_test_radio_force_receiving(false, false);
	meshcore_hal_test_radio_set_send_delay_per_byte(1U);

	for (int i = 0; i < kPoolSize * 4; i++) {
		env.mesh.loop();
		meshcore_hal_test_millis_advance(100000U);
		env.mesh.loop();
	}

	for (;;) {
		mesh::Packet *packet = env.manager.removeOutboundByIdx(0);

		if (packet == nullptr) {
			break;
		}
		env.manager.free(packet);
	}

	for (int i = 0; i < kPoolSize * 4; i++) {
		mesh::Packet *packet =
			env.manager.getNextInbound(meshcore_hal_millis_get());

		if (packet != nullptr) {
			env.manager.free(packet);
			continue;
		}
		meshcore_hal_test_millis_advance(100000U);
	}
}

void *reference_env_suite_setup(void)
{
	if (!g_reference_env_inited) {
		g_reference_env = ::new (static_cast<void *>(g_reference_env_storage))
			ReferenceEnv();
		g_reference_env_inited = true;
	}

	return g_reference_env;
}

void reference_env_before_each(void *fixture)
{
	ReferenceEnv *env = static_cast<ReferenceEnv *>(fixture);

	reset_fake_runtime();
	reference_env_settle_and_drain(*env);
	reset_fake_runtime();
	env->script = default_script();
	env->tables = SimpleMeshTables();
	env->mesh.~ReferenceHarness();
	::new (static_cast<void *>(&env->mesh))
		ReferenceHarness(env->radio, env->clock, env->rng, env->rtc,
				 env->manager, env->tables, &env->script);

	zassert_equal(env->manager.getOutboundTotal(), 0,
		      "reference outbound queue not empty after reset");
	zassert_equal(env->manager.getFreeCount(), kPoolSize,
		      "reference pool not fully restored after reset");
}

ReferenceEnv &reference_env_get(void)
{
	zassert_not_null(g_reference_env, "reference env not initialized");
	return *g_reference_env;
}

JointSnapshot capture_reference_snapshot(ReferenceEnv &env)
{
	JointSnapshot snapshot = {};
	mesh::Packet *outbound = env.manager.getOutboundTotal() > 0
					 ? env.manager.getOutboundByIdx(0)
					 : nullptr;

	snapshot.total_air_time = env.mesh.getTotalAirTime();
	snapshot.rx_air_time = env.mesh.getReceiveAirTime();
	snapshot.remaining_tx_budget = env.mesh.getRemainingTxBudget();
	snapshot.num_sent_flood = env.mesh.getNumSentFlood();
	snapshot.num_sent_direct = env.mesh.getNumSentDirect();
	snapshot.num_recv_flood = env.mesh.getNumRecvFlood();
	snapshot.num_recv_direct = env.mesh.getNumRecvDirect();
	snapshot.err_flags = env.mesh.getErrFlags();
	snapshot.free_count = env.manager.getFreeCount();
	snapshot.outbound_total = env.manager.getOutboundTotal();
	snapshot.on_peer_data_recv_count = env.script.on_peer_data_recv_count;
	snapshot.on_trace_recv_count = env.script.on_trace_recv_count;
	snapshot.on_peer_path_recv_count = env.script.on_peer_path_recv_count;
	snapshot.on_advert_recv_count = env.script.on_advert_recv_count;
	snapshot.on_anon_data_recv_count = env.script.on_anon_data_recv_count;
	snapshot.on_control_data_recv_count = env.script.on_control_data_recv_count;
	snapshot.on_raw_data_recv_count = env.script.on_raw_data_recv_count;
	snapshot.on_group_data_recv_count = env.script.on_group_data_recv_count;
	snapshot.on_ack_recv_count = env.script.on_ack_recv_count;
	snapshot.last_ack_crc = env.script.last_ack_crc;
	snapshot.last_peer_data_type = env.script.last_peer_data_type;
	snapshot.last_peer_data_len = env.script.last_peer_data_len;
	snapshot.last_group_data_type = env.script.last_group_data_type;
	snapshot.last_group_data_len = env.script.last_group_data_len;
	snapshot.last_anon_data_len = env.script.last_anon_data_len;
	snapshot.last_peer_path_len = env.script.last_peer_path_len;
	snapshot.last_peer_path_extra_type = env.script.last_peer_path_extra_type;
	snapshot.last_peer_path_extra_len = env.script.last_peer_path_extra_len;
	snapshot.last_trace_tag = env.script.last_trace_tag;
	snapshot.last_trace_auth_code = env.script.last_trace_auth_code;
	snapshot.last_trace_flags = env.script.last_trace_flags;
	snapshot.radio_packets_recv = meshcore_hal_test_radio_get_packets_recv();
	snapshot.radio_packets_sent = meshcore_hal_test_radio_get_packets_sent();
	snapshot.noise_floor_calibrate_count =
		meshcore_hal_test_radio_get_noise_floor_calibrate_count();
	snapshot.agc_reset_count =
		meshcore_hal_test_radio_get_agc_reset_count();
	snapshot.on_send_finished_count =
		meshcore_hal_test_radio_get_on_send_finished_count();
	snapshot.last_send_len = meshcore_hal_test_radio_get_last_send_len();
	if (outbound != nullptr) {
		snapshot.first_outbound_len =
			outbound->writeTo(snapshot.first_outbound_raw);
	}
	return snapshot;
}

} // namespace meshcore_dispatcher_mesh_tdd
