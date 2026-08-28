// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_CASE_HELPERS_H_
#define FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_CASE_HELPERS_H_

#include <string.h>

#include "test_support.h"

ReferenceChatHarness &shared_oracle(void);

void build_inbound_advert_from_oracle(
	const runtime_fixture_data *fixture,
	ReferenceChatHarness::observed_packet *out_packet);

void build_inbound_path_raw_from_peer(
	const runtime_fixture_data *fixture, uint8_t extra_type,
	const uint8_t *extra, uint8_t extra_len,
	ReferenceChatHarness::observed_packet *out_packet);

int target_trace_request_from_out_path(
	const uint8_t *public_key, const uint8_t *out_path,
	uint8_t out_path_len, uint8_t path_hash_size, uint32_t *tag);

void target_process_until_packets_sent_lenient(uint32_t expected_packets);

#endif /* FOBE_TESTS_LIB_MESHCORE_BLACKBOX_RUNTIME_ORACLE_SRC_CASE_HELPERS_H_ */
