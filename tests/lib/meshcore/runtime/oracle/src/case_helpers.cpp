// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "case_helpers.h"

#include <errno.h>

static ReferenceChatHarness g_oracle;

ReferenceChatHarness &shared_oracle(void)
{
	return g_oracle;
}

void build_inbound_advert_from_oracle(
	const runtime_fixture_data *fixture,
	ReferenceChatHarness::observed_packet *out_packet)
{
	ReferenceChatHarness source;

	zassert_not_null(fixture, "fixture is null");
	zassert_not_null(out_packet, "out_packet is null");

	source.reset();
	source.set_self_identity(fixture->inbound_identity);
	source.set_node_name("inbound_peer");
	source.set_advert_profile(true, 22.0, 113.0);
	zassert_true(source.send_self_advert(false),
		     "oracle inbound advert build failed");
	zassert_true(source.drive_until_packets_sent(1U),
		     "oracle inbound advert send did not complete");
	zassert_true(source.capture_last_packet(out_packet),
		     "oracle inbound advert packet missing");
}

void build_inbound_path_raw_from_peer(
	const runtime_fixture_data *fixture, uint8_t extra_type,
	const uint8_t *extra, uint8_t extra_len,
	ReferenceChatHarness::observed_packet *out_packet)
{
	ReferenceChatHarness source;

	zassert_not_null(fixture, "fixture is null");
	zassert_not_null(out_packet, "out_packet is null");

	source.reset();
	source.set_self_identity(fixture->peer_identity);
	zassert_true(source.build_inbound_path_raw(
			     fixture->local_identity.identity.pub_key,
			     k_trace_out_path, sizeof(k_trace_out_path), 2U,
			     extra_type, extra, extra_len, 6, out_packet),
		     "oracle inbound path raw build failed");
}

int target_trace_request_from_out_path(
	const uint8_t *public_key, const uint8_t *out_path,
	uint8_t out_path_len, uint8_t path_hash_size, uint32_t *tag)
{
	uint8_t trace_path[MESHCORE_MAX_PATH_LEN] = {};
	uint8_t trace_len = 0U;

	if (public_key == nullptr || out_path == nullptr || out_path_len == 0U ||
	    path_hash_size == 0U || path_hash_size > 3U ||
	    (out_path_len % path_hash_size) != 0U) {
		return -EINVAL;
	}

	memcpy(trace_path, out_path, out_path_len);
	trace_len = out_path_len;
	for (uint8_t hop = out_path_len; hop >= (uint8_t)(2U * path_hash_size);
	     hop = (uint8_t)(hop - path_hash_size)) {
		if ((size_t)trace_len + path_hash_size > sizeof(trace_path)) {
			return -EINVAL;
		}
		memcpy(&trace_path[trace_len],
		       &out_path[hop - (uint8_t)(2U * path_hash_size)],
		       path_hash_size);
		trace_len = (uint8_t)(trace_len + path_hash_size);
	}

	return meshcore_node_trace_request(trace_path, trace_len, path_hash_size, tag);
}

void target_process_until_packets_sent_lenient(uint32_t expected_packets)
{
	uint32_t now_ms = 0U;

	(void)meshcore_timer_fired(now_ms);
	for (int i = 0; i < 24 &&
			meshcore_hal_test_radio_get_packets_sent() < expected_packets;
	     i++) {
		if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
			meshcore_hal_test_radio_on_send_finished();
			(void)meshcore_radio_tx_done(now_ms, true);
		}
		now_ms += 250U;
		meshcore_hal_test_millis_set(now_ms);
		(void)meshcore_timer_fired(now_ms);
	}
	if (meshcore_test_runtime_dispatcher_has_active_outbound()) {
		meshcore_hal_test_radio_on_send_finished();
		(void)meshcore_radio_tx_done(now_ms, true);
	}

	zassert_true(meshcore_hal_test_radio_get_packets_sent() >= expected_packets,
		     "lenient sent packet count mismatch");
	meshcore_hal_test_rng_clear();
}
