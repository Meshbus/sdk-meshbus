// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "Packet.h"
#include "StaticPoolPacketManager.h"

extern "C" {
#include "meshcore_packet_manager.h"
}

static void tag_reference_packet(mesh::Packet *packet, uint8_t tag)
{
	zassert_not_null(packet, "reference packet must not be null");
	packet->header = tag;
	packet->payload_len = 1;
	packet->payload[0] = (uint8_t)(tag ^ 0x5A);
}

static void tag_actual_packet(struct meshcore_packet *packet, uint8_t tag)
{
	zassert_not_null(packet, "actual packet must not be null");
	memset(packet, 0, sizeof(*packet));
	packet->header = tag;
	packet->payload_len = 1;
	packet->payload[0] = (uint8_t)(tag ^ 0x5A);
}

static uint8_t reference_packet_tag(const mesh::Packet *packet)
{
	return packet == NULL ? 0xFFU : packet->header;
}

static uint8_t actual_packet_tag(const struct meshcore_packet *packet)
{
	return packet == NULL ? 0xFFU : packet->header;
}

static void expect_queue_get_matches_reference(PacketQueue &expected,
					       struct meshcore_packet_queue *actual,
					       uint32_t now)
{
	mesh::Packet *expected_packet = expected.get(now);
	struct meshcore_packet *actual_packet =
		meshcore_packet_queue_get(actual, now);

	zassert_equal(reference_packet_tag(expected_packet),
		      actual_packet_tag(actual_packet),
		      "queue get mismatch at now=%u", now);
}

static void expect_queue_remove_by_idx_matches_reference(
	PacketQueue &expected, struct meshcore_packet_queue *actual, int idx)
{
	mesh::Packet *expected_packet = expected.removeByIdx(idx);
	struct meshcore_packet *actual_packet =
		meshcore_packet_queue_remove_by_idx(actual, idx);

	zassert_equal(reference_packet_tag(expected_packet),
		      actual_packet_tag(actual_packet),
		      "queue removeByIdx mismatch at idx=%d", idx);
}

static void expect_outbound_get_matches_reference(
	StaticPoolPacketManager &expected,
	struct meshcore_packet_queue_manager *actual, uint32_t now)
{
	mesh::Packet *expected_packet = expected.getNextOutbound(now);
	struct meshcore_packet *actual_packet =
		meshcore_packet_queue_manager_get_next_outbound(actual, now);

	zassert_equal(reference_packet_tag(expected_packet),
		      actual_packet_tag(actual_packet),
		      "getNextOutbound mismatch at now=%u", now);
}

static void expect_inbound_get_matches_reference(
	StaticPoolPacketManager &expected,
	struct meshcore_packet_queue_manager *actual, uint32_t now)
{
	mesh::Packet *expected_packet = expected.getNextInbound(now);
	struct meshcore_packet *actual_packet =
		meshcore_packet_queue_manager_get_next_inbound(actual, now);

	zassert_equal(reference_packet_tag(expected_packet),
		      actual_packet_tag(actual_packet),
		      "getNextInbound mismatch at now=%u", now);
}

ZTEST(meshcore_packet_manager_tdd, test_packet_queue_init_and_empty_state_matches_reference)
{
	PacketQueue expected(3);
	struct meshcore_packet_queue actual = {};

	zassert_true(meshcore_packet_queue_init(&actual, 3),
		     "queue init should succeed");
	zassert_equal(expected.count(), meshcore_packet_queue_count(&actual),
		      "empty count mismatch");
	zassert_equal(expected.countBefore(0U),
		      meshcore_packet_queue_count_before(&actual, 0U),
		      "empty countBefore(0) mismatch");
	zassert_equal(expected.countBefore(UINT32_MAX),
		      meshcore_packet_queue_count_before(&actual, UINT32_MAX),
		      "empty countBefore(UINT32_MAX) mismatch");
	expect_queue_get_matches_reference(expected, &actual, 0U);
	expect_queue_remove_by_idx_matches_reference(expected, &actual, 0);

	meshcore_packet_queue_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_count_before_and_sentinel_match_reference)
{
	PacketQueue expected(4);
	struct meshcore_packet_queue actual = {};
	mesh::Packet expected_packets[3];
	struct meshcore_packet actual_packets[3];

	zassert_true(meshcore_packet_queue_init(&actual, 4),
		     "queue init should succeed");

	for (int i = 0; i < 3; i++) {
		tag_reference_packet(&expected_packets[i], (uint8_t)(0x10 + i));
		tag_actual_packet(&actual_packets[i], (uint8_t)(0x10 + i));
	}

	zassert_equal(expected.add(&expected_packets[0], 5U, 80U),
		      meshcore_packet_queue_add(&actual, &actual_packets[0], 5U, 80U),
		      "add result mismatch for packet 0");
	zassert_equal(expected.add(&expected_packets[1], 1U, 120U),
		      meshcore_packet_queue_add(&actual, &actual_packets[1], 1U, 120U),
		      "add result mismatch for packet 1");
	zassert_equal(expected.add(&expected_packets[2], 3U, 160U),
		      meshcore_packet_queue_add(&actual, &actual_packets[2], 3U, 160U),
		      "add result mismatch for packet 2");

	zassert_equal(expected.countBefore(100U),
		      meshcore_packet_queue_count_before(&actual, 100U),
		      "countBefore(100) mismatch");
	zassert_equal(expected.countBefore(150U),
		      meshcore_packet_queue_count_before(&actual, 150U),
		      "countBefore(150) mismatch");
	zassert_equal(expected.countBefore(UINT32_MAX),
		      meshcore_packet_queue_count_before(&actual, UINT32_MAX),
		      "countBefore(UINT32_MAX) mismatch");

	meshcore_packet_queue_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_get_honors_schedule_priority_and_fifo_matches_reference)
{
	PacketQueue expected(5);
	struct meshcore_packet_queue actual = {};
	mesh::Packet expected_packets[4];
	struct meshcore_packet actual_packets[4];
	const uint32_t now = 100U;

	zassert_true(meshcore_packet_queue_init(&actual, 5),
		     "queue init should succeed");

	for (int i = 0; i < 4; i++) {
		tag_reference_packet(&expected_packets[i], (uint8_t)(0x20 + i));
		tag_actual_packet(&actual_packets[i], (uint8_t)(0x20 + i));
	}

	zassert_equal(expected.add(&expected_packets[0], 5U, now + 10U),
		      meshcore_packet_queue_add(&actual, &actual_packets[0], 5U,
						 now + 10U),
		      "add result mismatch for future packet");
	zassert_equal(expected.add(&expected_packets[1], 2U, now),
		      meshcore_packet_queue_add(&actual, &actual_packets[1], 2U, now),
		      "add result mismatch for ready packet 1");
	zassert_equal(expected.add(&expected_packets[2], 2U, now),
		      meshcore_packet_queue_add(&actual, &actual_packets[2], 2U, now),
		      "add result mismatch for ready packet 2");
	zassert_equal(expected.add(&expected_packets[3], 1U, now),
		      meshcore_packet_queue_add(&actual, &actual_packets[3], 1U, now),
		      "add result mismatch for highest priority packet");

	zassert_equal(expected.countBefore(now),
		      meshcore_packet_queue_count_before(&actual, now),
		      "countBefore(now) mismatch");
	expect_queue_get_matches_reference(expected, &actual, now);
	expect_queue_get_matches_reference(expected, &actual, now);
	expect_queue_get_matches_reference(expected, &actual, now);
	expect_queue_get_matches_reference(expected, &actual, now);
	expect_queue_get_matches_reference(expected, &actual, now + 10U);
	expect_queue_get_matches_reference(expected, &actual, now + 10U);

	meshcore_packet_queue_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_capacity_item_at_and_remove_by_idx_match_reference)
{
	PacketQueue expected(3);
	struct meshcore_packet_queue actual = {};
	mesh::Packet expected_packets[4];
	struct meshcore_packet actual_packets[4];

	zassert_true(meshcore_packet_queue_init(&actual, 3),
		     "queue init should succeed");

	for (int i = 0; i < 4; i++) {
		tag_reference_packet(&expected_packets[i], (uint8_t)(0x30 + i));
		tag_actual_packet(&actual_packets[i], (uint8_t)(0x30 + i));
	}

	for (int i = 0; i < 4; i++) {
		zassert_equal(expected.add(&expected_packets[i], (uint8_t)i, 0U),
			      meshcore_packet_queue_add(&actual, &actual_packets[i],
						       (uint8_t)i, 0U),
			      "add result mismatch at idx=%d", i);
	}

	zassert_equal(expected.count(), meshcore_packet_queue_count(&actual),
		      "count after add mismatch");
	zassert_equal(reference_packet_tag(expected.itemAt(0)),
		      actual_packet_tag(meshcore_packet_queue_item_at(&actual, 0)),
		      "itemAt(0) mismatch");
	zassert_equal(reference_packet_tag(expected.itemAt(1)),
		      actual_packet_tag(meshcore_packet_queue_item_at(&actual, 1)),
		      "itemAt(1) mismatch");
	zassert_equal(reference_packet_tag(expected.itemAt(2)),
		      actual_packet_tag(meshcore_packet_queue_item_at(&actual, 2)),
		      "itemAt(2) mismatch");

	expect_queue_remove_by_idx_matches_reference(expected, &actual, 1);
	zassert_equal(expected.count(), meshcore_packet_queue_count(&actual),
		      "count after remove mismatch");
	zassert_equal(reference_packet_tag(expected.itemAt(0)),
		      actual_packet_tag(meshcore_packet_queue_item_at(&actual, 0)),
		      "itemAt(0) after remove mismatch");
	zassert_equal(reference_packet_tag(expected.itemAt(1)),
		      actual_packet_tag(meshcore_packet_queue_item_at(&actual, 1)),
		      "itemAt(1) after remove mismatch");
	expect_queue_remove_by_idx_matches_reference(expected, &actual, 5);

	meshcore_packet_queue_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_manager_alloc_and_free_match_reference)
{
	StaticPoolPacketManager expected(4);
	struct meshcore_packet_queue_manager actual = {};
	mesh::Packet *expected_allocs[5] = {};
	struct meshcore_packet *actual_allocs[5] = {};

	meshcore_packet_queue_manager_prepare(&actual, 4);

	zassert_equal(expected.getFreeCount(),
		      meshcore_packet_queue_manager_get_free_count(&actual),
		      "initial free count mismatch");

	for (int i = 0; i < 5; i++) {
		expected_allocs[i] = expected.allocNew();
		actual_allocs[i] = meshcore_packet_queue_manager_alloc_new(&actual);

		zassert_equal(expected_allocs[i] == NULL, actual_allocs[i] == NULL,
			      "alloc null-state mismatch at idx=%d", i);
		if (i < 4) {
			tag_reference_packet(expected_allocs[i], (uint8_t)(0x40 + i));
			tag_actual_packet(actual_allocs[i], (uint8_t)(0x40 + i));
		}
	}

	zassert_equal(expected.getFreeCount(),
		      meshcore_packet_queue_manager_get_free_count(&actual),
		      "free count after exhaustion mismatch");

	expected.free(expected_allocs[1]);
	meshcore_packet_queue_manager_free(&actual, actual_allocs[1]);
	zassert_equal(expected.getFreeCount(),
		      meshcore_packet_queue_manager_get_free_count(&actual),
		      "free count after free mismatch");

	zassert_equal(expected_allocs[1], expected.allocNew(),
		      "reference should reuse freed packet");
	zassert_equal(actual_allocs[1],
		      meshcore_packet_queue_manager_alloc_new(&actual),
		      "actual should reuse freed packet");
	zassert_equal(expected.getFreeCount(),
		      meshcore_packet_queue_manager_get_free_count(&actual),
		      "free count after reuse mismatch");

	meshcore_packet_queue_manager_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_manager_nonpositive_pool_is_empty)
{
	struct meshcore_packet_queue_manager actual = {};

	meshcore_packet_queue_manager_prepare(&actual, 0);
	zassert_false(actual.initialized, "zero pool should not initialize");
	zassert_equal(meshcore_packet_queue_manager_get_free_count(&actual), 0,
		      "zero pool should report no free packets");
	zassert_is_null(meshcore_packet_queue_manager_alloc_new(&actual),
			"zero pool should not allocate packets");

	meshcore_packet_queue_manager_prepare(&actual, -1);
	zassert_false(actual.initialized, "negative pool should not initialize");
	zassert_equal(meshcore_packet_queue_manager_get_free_count(&actual), 0,
		      "negative pool should report no free packets");
	zassert_is_null(meshcore_packet_queue_manager_alloc_new(&actual),
			"negative pool should not allocate packets");

	meshcore_packet_queue_manager_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_manager_outbound_flow_matches_reference)
{
	StaticPoolPacketManager expected(4);
	struct meshcore_packet_queue_manager actual = {};
	mesh::Packet *expected_packets[4];
	struct meshcore_packet *actual_packets[4];
	const uint32_t now = 200U;

	meshcore_packet_queue_manager_prepare(&actual, 4);

	for (int i = 0; i < 4; i++) {
		expected_packets[i] = expected.allocNew();
		actual_packets[i] = meshcore_packet_queue_manager_alloc_new(&actual);
		tag_reference_packet(expected_packets[i], (uint8_t)(0x50 + i));
		tag_actual_packet(actual_packets[i], (uint8_t)(0x50 + i));
	}

	expected.queueOutbound(expected_packets[0], 5U, now + 10U);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[0], 5U,
						      now + 10U);
	expected.queueOutbound(expected_packets[1], 2U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[1], 2U,
						      now);
	expected.queueOutbound(expected_packets[2], 2U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[2], 2U,
						      now);
	expected.queueOutbound(expected_packets[3], 1U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[3], 1U,
						      now);

	zassert_equal(expected.getOutboundCount(now),
		      meshcore_packet_queue_manager_get_outbound_count(&actual, now),
		      "getOutboundCount(now) mismatch");
	zassert_equal(expected.getOutboundCount(UINT32_MAX),
		      meshcore_packet_queue_manager_get_outbound_count(&actual,
								      UINT32_MAX),
		      "getOutboundCount(UINT32_MAX) mismatch");
	zassert_equal(expected.getOutboundTotal(),
		      meshcore_packet_queue_manager_get_outbound_total(&actual),
		      "getOutboundTotal mismatch");
	zassert_equal(expected.getFreeCount(),
		      meshcore_packet_queue_manager_get_free_count(&actual),
		      "free count after queueOutbound mismatch");

	for (int i = 0; i < 4; i++) {
		zassert_equal(reference_packet_tag(expected.getOutboundByIdx(i)),
			      actual_packet_tag(
				      meshcore_packet_queue_manager_get_outbound_by_idx(
					      &actual, i)),
			      "getOutboundByIdx mismatch at idx=%d", i);
	}

	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now + 10U);

	meshcore_packet_queue_manager_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_manager_remove_outbound_by_idx_matches_reference)
{
	StaticPoolPacketManager expected(3);
	struct meshcore_packet_queue_manager actual = {};
	mesh::Packet *expected_packets[3];
	struct meshcore_packet *actual_packets[3];
	const uint32_t now = 500U;

	meshcore_packet_queue_manager_prepare(&actual, 3);

	for (int i = 0; i < 3; i++) {
		expected_packets[i] = expected.allocNew();
		actual_packets[i] = meshcore_packet_queue_manager_alloc_new(&actual);
		tag_reference_packet(expected_packets[i], (uint8_t)(0x60 + i));
		tag_actual_packet(actual_packets[i], (uint8_t)(0x60 + i));
	}

	expected.queueOutbound(expected_packets[0], 3U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[0], 3U,
						      now);
	expected.queueOutbound(expected_packets[1], 2U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[1], 2U,
						      now);
	expected.queueOutbound(expected_packets[2], 1U, now);
	meshcore_packet_queue_manager_queue_outbound(&actual, actual_packets[2], 1U,
						      now);

	zassert_equal(reference_packet_tag(expected.removeOutboundByIdx(1)),
		      actual_packet_tag(
			      meshcore_packet_queue_manager_remove_outbound_by_idx(
				      &actual, 1)),
		      "removeOutboundByIdx mismatch");
	zassert_equal(expected.getOutboundTotal(),
		      meshcore_packet_queue_manager_get_outbound_total(&actual),
		      "getOutboundTotal after remove mismatch");
	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now);
	expect_outbound_get_matches_reference(expected, &actual, now);

	meshcore_packet_queue_manager_deinit(&actual);
}

ZTEST(meshcore_packet_manager_tdd,
      test_packet_queue_manager_inbound_flow_matches_reference)
{
	StaticPoolPacketManager expected(3);
	struct meshcore_packet_queue_manager actual = {};
	mesh::Packet *expected_packets[3];
	struct meshcore_packet *actual_packets[3];
	const uint32_t now = 700U;

	meshcore_packet_queue_manager_prepare(&actual, 3);

	for (int i = 0; i < 3; i++) {
		expected_packets[i] = expected.allocNew();
		actual_packets[i] = meshcore_packet_queue_manager_alloc_new(&actual);
		tag_reference_packet(expected_packets[i], (uint8_t)(0x70 + i));
		tag_actual_packet(actual_packets[i], (uint8_t)(0x70 + i));
	}

	expected.queueInbound(expected_packets[0], now);
	meshcore_packet_queue_manager_queue_inbound(&actual, actual_packets[0], now);
	expected.queueInbound(expected_packets[1], now + 5U);
	meshcore_packet_queue_manager_queue_inbound(&actual, actual_packets[1],
						     now + 5U);
	expected.queueInbound(expected_packets[2], now);
	meshcore_packet_queue_manager_queue_inbound(&actual, actual_packets[2], now);

	expect_inbound_get_matches_reference(expected, &actual, now);
	expect_inbound_get_matches_reference(expected, &actual, now);
	expect_inbound_get_matches_reference(expected, &actual, now);
	expect_inbound_get_matches_reference(expected, &actual, now + 5U);
	expect_inbound_get_matches_reference(expected, &actual, now + 5U);

	meshcore_packet_queue_manager_deinit(&actual);
}

ZTEST_SUITE(meshcore_packet_manager_tdd, NULL, NULL, NULL, NULL, NULL);
