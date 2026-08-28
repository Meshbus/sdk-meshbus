// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "test_support.h"

#include <string.h>

namespace meshcore_dispatcher_tdd {

DispatcherInputScript default_script(void)
{
	DispatcherInputScript script = {};

	script.recv_action = MESHCORE_ACTION_RELEASE;
	script.override_airtime_budget_factor = true;
	script.airtime_budget_factor = 2.0f;
	script.override_rx_delay_ms = true;
	script.rx_delay_ms = 0;
	script.cad_fail_retry_delay_ms = 200U;
	script.cad_fail_max_duration_ms = 4000U;
	script.interference_threshold = 0;
	script.agc_reset_interval_ms = 0;
	script.duty_cycle_window_ms = 3600000UL;
	return script;
}

void reset_fake_runtime(void)
{
	meshcore_hal_test_host_state_reset();
	meshcore_hal_test_radio_reset();
	meshcore_hal_test_millis_clear();
	meshcore_hal_test_millis_set(kStartMillis);
	meshcore_hal_test_rtc_set_current_time(kStartRtc);
}

void init_reference_packet(mesh::Packet *packet, uint8_t route_type, uint8_t seed,
			   uint8_t payload_len)
{
	*packet = mesh::Packet();
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	packet->payload_len = payload_len;
	for (uint8_t i = 0; i < payload_len; i++) {
		packet->payload[i] = (uint8_t)(seed + i);
	}
}

void init_target_packet(struct meshcore_packet *packet, uint8_t route_type,
			uint8_t seed, uint8_t payload_len)
{
	memset(packet, 0, sizeof(*packet));
	packet->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			 (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | route_type;
	packet->path_len = 0U;
	packet->payload_len = payload_len;
	for (uint8_t i = 0; i < payload_len; i++) {
		packet->payload[i] = (uint8_t)(seed + i);
	}
}

uint8_t build_raw_packet(uint8_t route_type, uint8_t seed,
			 uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
			 uint8_t payload_len)
{
	mesh::Packet packet;

	init_reference_packet(&packet, route_type, seed, payload_len);
	return packet.writeTo(raw);
}

uint8_t build_raw_transport_packet(uint8_t route_type, uint16_t transport_code0,
				   uint16_t transport_code1, uint8_t seed,
				   uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				   uint8_t payload_len)
{
	mesh::Packet packet;

	init_reference_packet(&packet, route_type, seed, payload_len);
	packet.transport_codes[0] = transport_code0;
	packet.transport_codes[1] = transport_code1;
	return packet.writeTo(raw);
}

void expect_snapshot_match(const DispatcherSnapshot &expected,
			   const DispatcherSnapshot &actual,
			   const char *label)
{
	zassert_equal(expected.total_air_time, actual.total_air_time,
		      "%s total_air_time mismatch", label);
	zassert_equal(expected.rx_air_time, actual.rx_air_time,
		      "%s rx_air_time mismatch", label);
	zassert_equal(expected.remaining_tx_budget, actual.remaining_tx_budget,
		      "%s remaining_tx_budget mismatch", label);
	zassert_equal(expected.num_sent_flood, actual.num_sent_flood,
		      "%s num_sent_flood mismatch", label);
	zassert_equal(expected.num_sent_direct, actual.num_sent_direct,
		      "%s num_sent_direct mismatch", label);
	zassert_equal(expected.num_recv_flood, actual.num_recv_flood,
		      "%s num_recv_flood mismatch", label);
	zassert_equal(expected.num_recv_direct, actual.num_recv_direct,
		      "%s num_recv_direct mismatch", label);
	zassert_equal(expected.err_flags, actual.err_flags,
		      "%s err_flags mismatch", label);
	zassert_equal(expected.free_count, actual.free_count,
		      "%s free_count mismatch", label);
	zassert_equal(expected.outbound_total, actual.outbound_total,
		      "%s outbound_total mismatch: expected=%d actual=%d", label,
		      expected.outbound_total, actual.outbound_total);
	zassert_equal(expected.log_rx_raw_count, actual.log_rx_raw_count,
		      "%s log_rx_raw_count mismatch", label);
	zassert_equal(expected.log_rx_count, actual.log_rx_count,
		      "%s log_rx_count mismatch", label);
	zassert_equal(expected.log_tx_count, actual.log_tx_count,
		      "%s log_tx_count mismatch", label);
	zassert_equal(expected.log_tx_fail_count, actual.log_tx_fail_count,
		      "%s log_tx_fail_count mismatch", label);
	zassert_equal(expected.radio_packets_recv, actual.radio_packets_recv,
		      "%s radio_packets_recv mismatch", label);
	zassert_equal(expected.radio_packets_sent, actual.radio_packets_sent,
		      "%s radio_packets_sent mismatch", label);
	zassert_equal(expected.noise_floor_calibrate_count,
		      actual.noise_floor_calibrate_count,
		      "%s noise_floor_calibrate_count mismatch", label);
	zassert_equal(expected.agc_reset_count, actual.agc_reset_count,
		      "%s agc_reset_count mismatch", label);
	zassert_equal(expected.on_send_finished_count,
		      actual.on_send_finished_count,
		      "%s on_send_finished_count mismatch", label);
	zassert_equal(expected.last_send_len, actual.last_send_len,
		      "%s last_send_len mismatch", label);
	zassert_equal(expected.first_outbound_len, actual.first_outbound_len,
		      "%s first_outbound_len mismatch", label);
	if (expected.first_outbound_len > 0U || actual.first_outbound_len > 0U) {
		zassert_mem_equal(expected.first_outbound_raw, actual.first_outbound_raw,
				  expected.first_outbound_len,
				  "%s first_outbound_raw mismatch", label);
	}
}

} // namespace meshcore_dispatcher_tdd
