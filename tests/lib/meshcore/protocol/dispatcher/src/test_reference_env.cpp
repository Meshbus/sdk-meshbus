// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_reference_env.h"

#include <stddef.h>
#include <string.h>

inline void *operator new(size_t, void *ptr) noexcept
{
	return ptr;
}

namespace meshcore_dispatcher_tdd {

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

ReferenceHarness::ReferenceHarness(
	mesh::Radio &radio, mesh::MillisecondClock &clock,
	mesh::PacketManager &manager, DispatcherInputScript *script)
	: mesh::Dispatcher(radio, clock, manager), script_(script)
{
}

uint16_t ReferenceHarness::getErrFlags() const
{
	return _err_flags;
}

mesh::DispatcherAction ReferenceHarness::onRecvPacket(
	mesh::Packet *packet)
{
	(void)packet;
	/* Owner callback is intentionally a pure action source in this module. */
	script_->handled_count++;
	return (mesh::DispatcherAction)script_->recv_action;
}

void ReferenceHarness::logRxRaw(float, float, const uint8_t[], int)
{
	script_->log_rx_raw_count++;
}

void ReferenceHarness::logRx(mesh::Packet *, int, float)
{
	script_->log_rx_count++;
}

void ReferenceHarness::logTx(mesh::Packet *, int)
{
	script_->log_tx_count++;
}

void ReferenceHarness::logTxFail(mesh::Packet *, int)
{
	script_->log_tx_fail_count++;
}

const char *ReferenceHarness::getLogDateTime()
{
	return "";
}

float ReferenceHarness::getAirtimeBudgetFactor() const
{
	if (!script_->override_airtime_budget_factor) {
		return mesh::Dispatcher::getAirtimeBudgetFactor();
	}

	return script_->airtime_budget_factor;
}

int ReferenceHarness::calcRxDelay(float score, uint32_t air_time) const
{
	if (!script_->override_rx_delay_ms) {
		return mesh::Dispatcher::calcRxDelay(score, air_time);
	}

	return script_->rx_delay_ms;
}

uint32_t ReferenceHarness::getCADFailRetryDelay() const
{
	return script_->cad_fail_retry_delay_ms;
}

uint32_t ReferenceHarness::getCADFailMaxDuration() const
{
	return script_->cad_fail_max_duration_ms;
}

int ReferenceHarness::getInterferenceThreshold() const
{
	return script_->interference_threshold;
}

int ReferenceHarness::getAGCResetInterval() const
{
	return script_->agc_reset_interval_ms;
}

unsigned long ReferenceHarness::getDutyCycleWindowMs() const
{
	return script_->duty_cycle_window_ms;
}

ReferenceEnv::ReferenceEnv()
	: script(default_script()),
	  manager(kPoolSize),
	  dispatcher(radio, clock, manager, &script)
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
		env.dispatcher.loop();
		meshcore_hal_test_millis_advance(100000U);
		env.dispatcher.loop();
	}

	for (;;) {
		mesh::Packet *packet = env.manager.removeOutboundByIdx(0);
		if (packet == nullptr) {
			break;
		}
		env.manager.free(packet);
	}

	for (;;) {
		mesh::Packet *packet = env.manager.getNextInbound(meshcore_hal_millis_get());
		if (packet == nullptr) {
			break;
		}
		env.manager.free(packet);
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
	env->dispatcher.~ReferenceHarness();
	::new (static_cast<void *>(&env->dispatcher))
		ReferenceHarness(env->radio, env->clock, env->manager, &env->script);

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

DispatcherSnapshot capture_reference_snapshot(ReferenceEnv &env)
{
	DispatcherSnapshot snapshot = {};
	mesh::Packet *outbound;

	snapshot.total_air_time = env.dispatcher.getTotalAirTime();
	snapshot.rx_air_time = env.dispatcher.getReceiveAirTime();
	snapshot.remaining_tx_budget = env.dispatcher.getRemainingTxBudget();
	snapshot.num_sent_flood = env.dispatcher.getNumSentFlood();
	snapshot.num_sent_direct = env.dispatcher.getNumSentDirect();
	snapshot.num_recv_flood = env.dispatcher.getNumRecvFlood();
	snapshot.num_recv_direct = env.dispatcher.getNumRecvDirect();
	snapshot.err_flags = env.dispatcher.getErrFlags();
	snapshot.free_count = env.manager.getFreeCount();
	snapshot.outbound_total = env.manager.getOutboundTotal();
	snapshot.log_rx_raw_count = env.script.log_rx_raw_count;
	snapshot.log_rx_count = env.script.log_rx_count;
	snapshot.log_tx_count = env.script.log_tx_count;
	snapshot.log_tx_fail_count = env.script.log_tx_fail_count;
	snapshot.radio_packets_recv = meshcore_hal_test_radio_get_packets_recv();
	snapshot.radio_packets_sent = meshcore_hal_test_radio_get_packets_sent();
	snapshot.noise_floor_calibrate_count =
		meshcore_hal_test_radio_get_noise_floor_calibrate_count();
	snapshot.agc_reset_count =
		meshcore_hal_test_radio_get_agc_reset_count();
	snapshot.on_send_finished_count =
		meshcore_hal_test_radio_get_on_send_finished_count();
	snapshot.last_send_len = meshcore_hal_test_radio_get_last_send_len();
	outbound = snapshot.outbound_total > 0 ? env.manager.getOutboundByIdx(0)
					       : nullptr;
	if (outbound != nullptr) {
		snapshot.first_outbound_len =
			outbound->writeTo(snapshot.first_outbound_raw);
	}
	return snapshot;
}

} // namespace meshcore_dispatcher_tdd
