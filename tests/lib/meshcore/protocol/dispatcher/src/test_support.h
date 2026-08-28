// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_SUPPORT_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_SUPPORT_H_

#include <stdint.h>

#include "test_packet_bridge.h"

namespace meshcore_dispatcher_tdd {

constexpr int kPoolSize = 8;
constexpr unsigned long kStartMillis = 1000UL;
constexpr uint32_t kStartRtc = 1700000000U;

/*
 * Dispatcher module tests treat owner callbacks as controlled inputs.
 * This script only feeds dispatcher branches; owner business logic is covered
 * by protocol/mesh and protocol/dispatcher_mesh.
 */
using DispatcherInputScript = meshcore_hal_test_dispatcher_script_t;

struct DispatcherSnapshot {
	unsigned long total_air_time;
	unsigned long rx_air_time;
	unsigned long remaining_tx_budget;
	uint32_t num_sent_flood;
	uint32_t num_sent_direct;
	uint32_t num_recv_flood;
	uint32_t num_recv_direct;
	uint16_t err_flags;
	int free_count;
	int outbound_total;
	int log_rx_raw_count;
	int log_rx_count;
	int log_tx_count;
	int log_tx_fail_count;
	uint32_t radio_packets_recv;
	uint32_t radio_packets_sent;
	uint32_t noise_floor_calibrate_count;
	uint32_t agc_reset_count;
	uint32_t on_send_finished_count;
	int last_send_len;
	uint8_t first_outbound_len;
	uint8_t first_outbound_raw[MESHCORE_MAX_TRANS_UNIT_LEN];
};

DispatcherInputScript default_script(void);
void reset_fake_runtime(void);
void init_reference_packet(mesh::Packet *packet, uint8_t route_type,
			   uint8_t seed, uint8_t payload_len = 3U);
void init_target_packet(struct meshcore_packet *packet, uint8_t route_type,
			uint8_t seed, uint8_t payload_len = 3U);
uint8_t build_raw_packet(uint8_t route_type, uint8_t seed,
			 uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
			 uint8_t payload_len = 3U);
uint8_t build_raw_transport_packet(uint8_t route_type, uint16_t transport_code0,
				   uint16_t transport_code1, uint8_t seed,
				   uint8_t raw[MESHCORE_MAX_TRANS_UNIT_LEN],
				   uint8_t payload_len = 3U);
void expect_snapshot_match(const DispatcherSnapshot &expected,
			   const DispatcherSnapshot &actual,
			   const char *label);

} // namespace meshcore_dispatcher_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_SUPPORT_H_ */
