// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "Packet.h"

extern "C" {
#include "meshcore_packet.h"
}

static void fill_sequence(uint8_t *dest, size_t len, uint8_t first)
{
	for (size_t i = 0; i < len; i++) {
		dest[i] = (uint8_t)(first + i);
	}
}

static void copy_reference_to_actual(const mesh::Packet &src,
				     struct meshcore_packet *dest)
{
	dest->header = src.header;
	dest->payload_len = src.payload_len;
	dest->path_len = src.path_len;
	memcpy(dest->transport_codes, src.transport_codes, sizeof(dest->transport_codes));
	memcpy(dest->path, src.path, sizeof(dest->path));
	memcpy(dest->payload, src.payload, sizeof(dest->payload));
	dest->snr_q4 = src._snr;
}

static void expect_packet_core_matches_reference(const mesh::Packet &expected,
						 const struct meshcore_packet *actual)
{
	zassert_equal(expected.header, actual->header, "header mismatch");
	zassert_equal(expected.payload_len, actual->payload_len, "payload_len mismatch");
	zassert_equal(expected.path_len, actual->path_len, "path_len mismatch");
}

static void expect_packet_decoded_state_matches_reference(
	const mesh::Packet &expected, const struct meshcore_packet *actual)
{
	expect_packet_core_matches_reference(expected, actual);
	zassert_mem_equal(expected.transport_codes, actual->transport_codes,
			  sizeof(actual->transport_codes), "transport_codes mismatch");
	zassert_mem_equal(expected.path, actual->path, expected.getPathByteLen(),
			  "path mismatch");
	zassert_mem_equal(expected.payload, actual->payload, expected.payload_len,
			  "payload mismatch");
}

static void init_sample_packet(mesh::Packet *expected, struct meshcore_packet *actual)
{
	expected->header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			   (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) |
			   ROUTE_TYPE_TRANSPORT_DIRECT;
	expected->transport_codes[0] = 0x1234U;
	expected->transport_codes[1] = 0x5678U;
	expected->setPathHashSizeAndCount(2, 3);
	fill_sequence(expected->path, expected->getPathByteLen(), 0x10);
	expected->payload_len = 18;
	fill_sequence(expected->payload, expected->payload_len, 0x40);
	expected->_snr = 14;

	copy_reference_to_actual(*expected, actual);
}

static void expect_read_from_matches_reference(const uint8_t *src, uint8_t len)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};
	bool expected_ok;
	bool actual_ok;

	expected_ok = expected.readFrom(src, len);
	actual_ok = meshcore_packet_read_from(&actual, src, len);

	zassert_equal(expected_ok, actual_ok, "read_from result mismatch");
	if (expected_ok) {
		expect_packet_decoded_state_matches_reference(expected, &actual);
	}
}

ZTEST(meshcore_packet_tdd, test_packet_init_matches_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};

	meshcore_packet_init(&actual);
	expect_packet_core_matches_reference(expected, &actual);
}

ZTEST(meshcore_packet_tdd, test_packet_route_and_payload_helpers_match_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};

	init_sample_packet(&expected, &actual);

	zassert_equal(expected.getRouteType(), meshcore_packet_get_route_type(&actual),
		      "route type mismatch");
	zassert_equal(expected.isRouteFlood(), meshcore_packet_is_route_flood(&actual),
		      "isRouteFlood mismatch");
	zassert_equal(expected.isRouteDirect(), meshcore_packet_is_route_direct(&actual),
		      "isRouteDirect mismatch");
	zassert_equal(expected.hasTransportCodes(),
		      meshcore_packet_has_transport_codes(&actual),
		      "hasTransportCodes mismatch");
	zassert_equal(expected.getPayloadType(), meshcore_packet_get_payload_type(&actual),
		      "payload type mismatch");
	zassert_equal(expected.getPayloadVer(), meshcore_packet_get_payload_ver(&actual),
		      "payload version mismatch");
}

ZTEST(meshcore_packet_tdd, test_packet_route_bits_separate_route_from_transport_codes)
{
	struct route_case {
		uint8_t route_type;
		bool is_flood;
		bool is_direct;
		bool has_transport_codes;
	};
	static const struct route_case cases[] = {
		{ ROUTE_TYPE_TRANSPORT_FLOOD, true, false, true },
		{ ROUTE_TYPE_FLOOD, true, false, false },
		{ ROUTE_TYPE_DIRECT, false, true, false },
		{ ROUTE_TYPE_TRANSPORT_DIRECT, false, true, true },
	};
	struct meshcore_packet packet = {};

	for (size_t i = 0U; i < ARRAY_SIZE(cases); i++) {
		packet.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
				(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
				cases[i].route_type;
		zassert_equal(meshcore_packet_is_route_flood(&packet), cases[i].is_flood,
			      "flood route mismatch for case %u", (unsigned int)i);
		zassert_equal(meshcore_packet_is_route_direct(&packet), cases[i].is_direct,
			      "direct route mismatch for case %u", (unsigned int)i);
		zassert_equal(meshcore_packet_has_transport_codes(&packet),
			      cases[i].has_transport_codes,
			      "transport-code mismatch for case %u", (unsigned int)i);
	}
}

ZTEST(meshcore_packet_tdd, test_packet_path_helpers_match_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};
	uint8_t expected_path[MAX_PATH_SIZE] = { 0 };
	uint8_t actual_path[MAX_PATH_SIZE] = { 0 };
	size_t expected_written;
	size_t actual_written;
	uint8_t expected_copied;
	uint8_t actual_copied;

	init_sample_packet(&expected, &actual);

	zassert_equal(expected.getPathHashSize(),
		      meshcore_packet_get_path_hash_size(&actual),
		      "getPathHashSize mismatch");
	zassert_equal(expected.getPathHashCount(),
		      meshcore_packet_get_path_hash_count(&actual),
		      "getPathHashCount mismatch");
	zassert_equal(expected.getPathByteLen(),
		      meshcore_packet_get_path_byte_len(&actual),
		      "getPathByteLen mismatch");

	expected_written = mesh::Packet::writePath(expected_path, expected.path, expected.path_len);
	actual_written = meshcore_packet_write_path(actual_path, actual.path, actual.path_len);
	zassert_equal(expected_written, actual_written, "writePath length mismatch");
	zassert_mem_equal(expected_path, actual_path, actual_written, "writePath mismatch");

	memset(expected_path, 0, sizeof(expected_path));
	memset(actual_path, 0, sizeof(actual_path));
	expected_copied = mesh::Packet::copyPath(expected_path, expected.path, expected.path_len);
	actual_copied = meshcore_packet_copy_path(actual_path, actual.path, actual.path_len);
	zassert_equal(expected_copied, actual_copied, "copyPath return mismatch");
	zassert_mem_equal(expected_path, actual_path, expected.getPathByteLen(),
			  "copyPath mismatch");

	expected.setPathHashCount(5);
	meshcore_packet_set_path_hash_count(&actual, 5);
	zassert_equal(expected.path_len, actual.path_len, "setPathHashCount mismatch");

	expected.setPathHashSizeAndCount(3, 7);
	meshcore_packet_set_path_hash_size_and_count(&actual, 3, 7);
	zassert_equal(expected.path_len, actual.path_len,
		      "setPathHashSizeAndCount mismatch");
}

ZTEST(meshcore_packet_tdd, test_packet_is_valid_path_len_matches_reference)
{
	static const uint8_t samples[] = {
		0x00,
		0x03,
		(uint8_t)(((1U - 1U) << 6) | 64U),
		(uint8_t)(((2U - 1U) << 6) | 32U),
		(uint8_t)(((3U - 1U) << 6) | 21U),
		(uint8_t)(((4U - 1U) << 6) | 1U),
		(uint8_t)(((5U - 1U) << 6) | 13U),
	};

	for (size_t i = 0; i < ARRAY_SIZE(samples); i++) {
		zassert_equal(mesh::Packet::isValidPathLen(samples[i]),
			      meshcore_packet_is_valid_path_len(samples[i]),
			      "isValidPathLen mismatch for 0x%02x", samples[i]);
	}
}

ZTEST(meshcore_packet_tdd, test_packet_mark_do_not_retransmit_matches_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};

	meshcore_packet_init(&actual);
	expected.markDoNotRetransmit();
	meshcore_packet_mark_do_not_retransmit(&actual);

	zassert_equal(expected.isMarkedDoNotRetransmit(),
		      meshcore_packet_is_marked_do_not_retransmit(&actual),
		      "isMarkedDoNotRetransmit mismatch");
	zassert_equal(expected.header, actual.header, "header mismatch after mark");
}

ZTEST(meshcore_packet_tdd, test_packet_get_snr_and_raw_length_match_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};

	init_sample_packet(&expected, &actual);

	zassert_true(meshcore_packet_get_snr(&actual) == expected.getSNR(),
		     "getSNR mismatch");
	zassert_equal(expected.getRawLength(), meshcore_packet_get_raw_length(&actual),
		      "getRawLength mismatch");
}

ZTEST(meshcore_packet_tdd, test_packet_calculate_hash_matches_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};
	uint8_t expected_hash[MAX_HASH_SIZE] = { 0 };
	uint8_t actual_hash[MESHCORE_PACKET_HASH_SIZE] = { 0 };

	init_sample_packet(&expected, &actual);
	expected.calculatePacketHash(expected_hash);
	meshcore_packet_calculate_hash(&actual, actual_hash);

	zassert_mem_equal(expected_hash, actual_hash, sizeof(expected_hash),
			  "calculatePacketHash mismatch");
}

ZTEST(meshcore_packet_tdd, test_packet_write_to_matches_reference)
{
	mesh::Packet expected {};
	struct meshcore_packet actual = {};
	uint8_t expected_raw[256] = { 0 };
	uint8_t actual_raw[256] = { 0 };
	uint8_t expected_len;
	uint8_t actual_len;

	init_sample_packet(&expected, &actual);
	expected_len = expected.writeTo(expected_raw);
	actual_len = meshcore_packet_write_to(&actual, actual_raw);

	zassert_equal(expected_len, actual_len, "writeTo length mismatch");
	zassert_mem_equal(expected_raw, actual_raw, expected_len, "writeTo bytes mismatch");
}

ZTEST(meshcore_packet_tdd, test_packet_read_from_matches_reference)
{
	mesh::Packet expected {};
	uint8_t raw[256] = { 0 };
	uint8_t raw_len;

	expected.header = (PAYLOAD_VER_1 << PH_VER_SHIFT) |
			  (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
			  ROUTE_TYPE_FLOOD;
	expected.setPathHashSizeAndCount(2, 2);
	fill_sequence(expected.path, expected.getPathByteLen(), 0x21);
	expected.payload_len = 9;
	fill_sequence(expected.payload, expected.payload_len, 0x61);
	raw_len = expected.writeTo(raw);

	expect_read_from_matches_reference(raw, raw_len);
}

ZTEST(meshcore_packet_tdd, test_packet_read_from_rejects_invalid_encodings)
{
	uint8_t invalid_reserved_hash_size[] = {
		(uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD),
		(uint8_t)(((4U - 1U) << 6) | 1U),
		0x11, 0x22, 0x33,
	};
	uint8_t invalid_oversize_path[] = {
		(uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD),
		(uint8_t)(((5U - 1U) << 6) | 20U),
		0x11, 0x22, 0x33,
	};
	uint8_t invalid_no_payload[] = {
		(uint8_t)((PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD),
		0x00,
	};

	expect_read_from_matches_reference(invalid_reserved_hash_size,
					   sizeof(invalid_reserved_hash_size));
	expect_read_from_matches_reference(invalid_oversize_path,
					   sizeof(invalid_oversize_path));
	expect_read_from_matches_reference(invalid_no_payload,
					   sizeof(invalid_no_payload));
}

ZTEST_SUITE(meshcore_packet_tdd, NULL, NULL, NULL, NULL, NULL);
