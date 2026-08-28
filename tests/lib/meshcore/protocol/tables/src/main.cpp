// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "Packet.h"
#include "SimpleMeshTables.h"

extern "C" {
#include "meshcore_packet.h"
#include "meshcore_tables.h"
}

static void copy_reference_to_actual(const mesh::Packet &src,
				     struct meshcore_packet *dest)
{
	dest->header = src.header;
	dest->payload_len = src.payload_len;
	dest->path_len = src.path_len;
	memcpy(dest->transport_codes, src.transport_codes,
	       sizeof(dest->transport_codes));
	memcpy(dest->path, src.path, sizeof(dest->path));
	memcpy(dest->payload, src.payload, sizeof(dest->payload));
	dest->snr_q4 = src._snr;
}

static void init_unique_data_packet(mesh::Packet *expected,
				    struct meshcore_packet *actual,
				    uint8_t route_type, uint8_t seed)
{
	expected->header = 0;
	expected->payload_len = 0;
	expected->path_len = 0;
	expected->_snr = 0;
	memset(expected->transport_codes, 0, sizeof(expected->transport_codes));
	memset(expected->path, 0, sizeof(expected->path));
	memset(expected->payload, 0, sizeof(expected->payload));
	memset(actual, 0, sizeof(*actual));

	expected->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | route_type;
	expected->payload_len = 6;
	expected->payload[0] = seed;
	expected->payload[1] = (uint8_t)(seed + 1U);
	expected->payload[2] = (uint8_t)(seed + 2U);
	expected->payload[3] = (uint8_t)(seed ^ 0x55U);
	expected->payload[4] = (uint8_t)(seed ^ 0xAAU);
	expected->payload[5] = (uint8_t)(seed + 3U);

	copy_reference_to_actual(*expected, actual);
}

static void init_ack_packet(mesh::Packet *expected, struct meshcore_packet *actual,
			    uint8_t route_type, uint32_t ack)
{
	expected->header = 0;
	expected->payload_len = 0;
	expected->path_len = 0;
	expected->_snr = 0;
	memset(expected->transport_codes, 0, sizeof(expected->transport_codes));
	memset(expected->path, 0, sizeof(expected->path));
	memset(expected->payload, 0, sizeof(expected->payload));
	memset(actual, 0, sizeof(*actual));

	expected->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | route_type;
	expected->payload_len = 4;
	memcpy(expected->payload, &ack, sizeof(ack));

	copy_reference_to_actual(*expected, actual);
}

static bool expect_was_seen_matches_reference(SimpleMeshTables *expected,
					      struct meshcore_tables *actual,
					      const mesh::Packet *expected_packet,
					      const struct meshcore_packet *actual_packet)
{
	bool expected_seen = expected->wasSeen(expected_packet);
	bool actual_seen = meshcore_tables_was_seen(actual, actual_packet);

	zassert_equal(expected_seen, actual_seen, "wasSeen result mismatch");
	zassert_equal(expected->getNumDirectDups(),
		      meshcore_tables_get_num_direct_dups(actual),
		      "direct dup count mismatch");
	zassert_equal(expected->getNumFloodDups(),
		      meshcore_tables_get_num_flood_dups(actual),
		      "flood dup count mismatch");
	if (!expected_seen) {
		expected->markSeen(expected_packet);
		meshcore_tables_mark_seen(actual, actual_packet);
	}
	return actual_seen;
}

static void expect_stats_match_reference(const SimpleMeshTables *expected,
					 const struct meshcore_tables *actual)
{
	zassert_equal(expected->getNumDirectDups(),
		      meshcore_tables_get_num_direct_dups(actual),
		      "direct dup count mismatch");
	zassert_equal(expected->getNumFloodDups(),
		      meshcore_tables_get_num_flood_dups(actual),
		      "flood dup count mismatch");
}

ZTEST(meshcore_tables_tdd, test_simple_tables_data_packet_seen_and_clear_match_reference)
{
	SimpleMeshTables expected;
	struct meshcore_tables actual = {};
	mesh::Packet expected_packet {};
	struct meshcore_packet actual_packet = {};

	meshcore_tables_init(&actual);
	init_unique_data_packet(&expected_packet, &actual_packet, ROUTE_TYPE_FLOOD, 0x10U);

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_packet, &actual_packet),
		      "first data packet should not be duplicate");
	zassert_true(expect_was_seen_matches_reference(&expected, &actual,
						       &expected_packet, &actual_packet),
		     "second data packet should be duplicate");

	expected.clear(&expected_packet);
	meshcore_tables_clear(&actual, &actual_packet);
	expect_stats_match_reference(&expected, &actual);

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_packet, &actual_packet),
		      "cleared data packet should no longer be duplicate");
}

ZTEST(meshcore_tables_tdd, test_simple_tables_ack_seen_and_clear_match_reference)
{
	SimpleMeshTables expected;
	struct meshcore_tables actual = {};
	mesh::Packet expected_packet {};
	struct meshcore_packet actual_packet = {};

	meshcore_tables_init(&actual);
	init_ack_packet(&expected_packet, &actual_packet, ROUTE_TYPE_DIRECT, 0x12345678U);

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_packet, &actual_packet),
		      "first ACK should not be duplicate");
	zassert_true(expect_was_seen_matches_reference(&expected, &actual,
						       &expected_packet, &actual_packet),
		     "second ACK should be duplicate");

	expected.clear(&expected_packet);
	meshcore_tables_clear(&actual, &actual_packet);
	expect_stats_match_reference(&expected, &actual);

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_packet, &actual_packet),
		      "cleared ACK should no longer be duplicate");
}

ZTEST(meshcore_tables_tdd, test_simple_tables_duplicate_stats_and_reset_match_reference)
{
	SimpleMeshTables expected;
	struct meshcore_tables actual = {};
	mesh::Packet expected_direct {};
	mesh::Packet expected_flood {};
	struct meshcore_packet actual_direct = {};
	struct meshcore_packet actual_flood = {};

	meshcore_tables_init(&actual);
	init_unique_data_packet(&expected_direct, &actual_direct, ROUTE_TYPE_DIRECT, 0x21U);
	init_unique_data_packet(&expected_flood, &actual_flood, ROUTE_TYPE_FLOOD, 0x31U);

	(void)expect_was_seen_matches_reference(&expected, &actual, &expected_direct,
						&actual_direct);
	(void)expect_was_seen_matches_reference(&expected, &actual, &expected_flood,
						&actual_flood);
	zassert_true(expect_was_seen_matches_reference(&expected, &actual, &expected_direct,
						       &actual_direct),
		     "direct packet should count as duplicate");
	zassert_true(expect_was_seen_matches_reference(&expected, &actual, &expected_flood,
						       &actual_flood),
		     "flood packet should count as duplicate");

	expected.resetStats();
	meshcore_tables_reset_stats(&actual);
	expect_stats_match_reference(&expected, &actual);

	zassert_true(expect_was_seen_matches_reference(&expected, &actual, &expected_direct,
						       &actual_direct),
		     "table entries should remain after resetStats");
}

ZTEST(meshcore_tables_tdd, test_simple_tables_hash_ring_eviction_matches_reference)
{
	SimpleMeshTables expected;
	struct meshcore_tables actual = {};
	mesh::Packet expected_first {};
	mesh::Packet expected_tmp {};
	struct meshcore_packet actual_first = {};
	struct meshcore_packet actual_tmp = {};

	meshcore_tables_init(&actual);
	init_unique_data_packet(&expected_first, &actual_first, ROUTE_TYPE_FLOOD, 0x40U);
	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_first, &actual_first),
		      "first packet should initially be new");

	for (int i = 1; i <= MESHCORE_TABLES_MAX_PACKET_HASHES; i++) {
		init_unique_data_packet(&expected_tmp, &actual_tmp, ROUTE_TYPE_FLOOD,
					(uint8_t)(0x40 + i));
		zassert_false(expect_was_seen_matches_reference(&expected, &actual,
								&expected_tmp, &actual_tmp),
			      "unique packet %d should initially be new", i);
	}

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_first, &actual_first),
		      "oldest hash should be evicted after ring wrap");
}

ZTEST(meshcore_tables_tdd, test_simple_tables_ack_ring_eviction_matches_reference)
{
	SimpleMeshTables expected;
	struct meshcore_tables actual = {};
	mesh::Packet expected_first {};
	mesh::Packet expected_tmp {};
	struct meshcore_packet actual_first = {};
	struct meshcore_packet actual_tmp = {};

	meshcore_tables_init(&actual);
	init_ack_packet(&expected_first, &actual_first, ROUTE_TYPE_DIRECT, 0xA0B0C001U);
	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_first, &actual_first),
		      "first ACK should initially be new");

	for (int i = 1; i <= MESHCORE_TABLES_MAX_PACKET_HASHES; i++) {
		init_ack_packet(&expected_tmp, &actual_tmp, ROUTE_TYPE_DIRECT,
				(uint32_t)(0xA0B0C001U + (uint32_t)i));
		zassert_false(expect_was_seen_matches_reference(&expected, &actual,
								&expected_tmp, &actual_tmp),
			      "unique ACK %d should initially be new", i);
	}

	zassert_false(expect_was_seen_matches_reference(&expected, &actual,
							&expected_first, &actual_first),
		      "oldest ACK should be evicted after ring wrap");
}

ZTEST_SUITE(meshcore_tables_tdd, NULL, NULL, NULL, NULL, NULL);
