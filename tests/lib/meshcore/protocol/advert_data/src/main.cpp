// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <helpers/AdvertDataHelpers.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore_advert_data.h"
}

static void expect_builder_encode_matches_reference(uint8_t adv_type,
						    const char *name, bool has_loc,
						    double lat, double lon,
						    uint16_t feat1, uint16_t feat2,
						    const char *label)
{
	AdvertDataBuilder expected =
		has_loc ? AdvertDataBuilder(adv_type, name, lat, lon)
			: (name != NULL ? AdvertDataBuilder(adv_type, name)
					: AdvertDataBuilder(adv_type));
	struct meshcore_advert_data_builder actual = {};
	uint8_t expected_buf[MESHCORE_MAX_ADVERT_DATA_LEN] = { 0 };
	uint8_t actual_buf[MESHCORE_MAX_ADVERT_DATA_LEN] = { 0 };
	uint8_t expected_len;
	uint8_t actual_len;

	memset(expected_buf, 0xA5, sizeof(expected_buf));
	memset(actual_buf, 0xA5, sizeof(actual_buf));

	if (has_loc) {
		meshcore_advert_data_builder_init_with_name_lat_lon(&actual,
								    adv_type,
								    name, lat,
								    lon);
	} else if (name != NULL) {
		meshcore_advert_data_builder_init_with_name(&actual, adv_type, name);
	} else {
		meshcore_advert_data_builder_init(&actual, adv_type);
	}

	if (feat1 != 0U) {
		expected.setFeat1(feat1);
		meshcore_advert_data_builder_set_feat1(&actual, feat1);
	}
	if (feat2 != 0U) {
		expected.setFeat2(feat2);
		meshcore_advert_data_builder_set_feat2(&actual, feat2);
	}

	expected_len = expected.encodeTo(expected_buf);
	actual_len = meshcore_advert_data_builder_encode_to(&actual, actual_buf);

	zassert_equal(expected_len, actual_len, "%s len mismatch", label);
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "%s raw mismatch", label);
}

static void expect_parser_matches_reference(const uint8_t *app_data, uint8_t app_data_len,
					    const char *label)
{
	AdvertDataParser expected(app_data, app_data_len);
	struct meshcore_advert_data_parser actual = {};
	double expected_lat;
	double actual_lat;
	double expected_lon;
	double actual_lon;

	meshcore_advert_data_parser_init(&actual, app_data, app_data_len);

	zassert_equal(expected.isValid(),
		      meshcore_advert_data_parser_is_valid(&actual),
		      "%s valid mismatch", label);
	zassert_equal(expected.getType(),
		      meshcore_advert_data_parser_get_type(&actual),
		      "%s type mismatch", label);
	zassert_equal(expected.getFeat1(),
		      meshcore_advert_data_parser_get_feat1(&actual),
		      "%s feat1 mismatch", label);
	zassert_equal(expected.getFeat2(),
		      meshcore_advert_data_parser_get_feat2(&actual),
		      "%s feat2 mismatch", label);
	zassert_equal(expected.hasName(),
		      meshcore_advert_data_parser_has_name(&actual),
		      "%s hasName mismatch", label);
	zassert_equal(expected.hasLatLon(),
		      meshcore_advert_data_parser_has_lat_lon(&actual),
		      "%s hasLatLon mismatch", label);
	zassert_equal(expected.getIntLat(),
		      meshcore_advert_data_parser_get_int_lat(&actual),
		      "%s int lat mismatch", label);
	zassert_equal(expected.getIntLon(),
		      meshcore_advert_data_parser_get_int_lon(&actual),
		      "%s int lon mismatch", label);
	zassert_equal(0, strcmp(expected.getName(),
				 meshcore_advert_data_parser_get_name(&actual)),
		      "%s name mismatch", label);

	expected_lat = expected.getLat();
	actual_lat = meshcore_advert_data_parser_get_lat(&actual);
	expected_lon = expected.getLon();
	actual_lon = meshcore_advert_data_parser_get_lon(&actual);

	zassert_true(fabs(expected_lat - actual_lat) < 1e-12,
		     "%s lat mismatch", label);
	zassert_true(fabs(expected_lon - actual_lon) < 1e-12,
		     "%s lon mismatch", label);
}

static void expect_parser_invalid_strict(const uint8_t *app_data, uint8_t app_data_len,
					 const char *label)
{
	struct meshcore_advert_data_parser actual = {};

	meshcore_advert_data_parser_init(&actual, app_data, app_data_len);
	zassert_false(meshcore_advert_data_parser_is_valid(&actual),
		      "%s should be invalid", label);
}

ZTEST(meshcore_advert_data_tdd, test_builder_encode_matches_reference)
{
	static const char kLongName[] = "123456789012345678901234567890123456789";

	expect_builder_encode_matches_reference(ADV_TYPE_CHAT, NULL, false, 0.0, 0.0,
						0U, 0U, "type only");
	expect_builder_encode_matches_reference(ADV_TYPE_REPEATER, "node-a", false,
						0.0, 0.0, 0U, 0U, "type name");
	expect_builder_encode_matches_reference(ADV_TYPE_ROOM, "room-1", true,
						22.54321, 114.05787, 0U, 0U,
						"type name latlon");
	expect_builder_encode_matches_reference(ADV_TYPE_SENSOR, NULL, false, 0.0,
						0.0, 0x1122U, 0U, "feat1 only");
	expect_builder_encode_matches_reference(ADV_TYPE_SENSOR, NULL, false, 0.0,
						0.0, 0U, 0x3344U, "feat2 only");
	expect_builder_encode_matches_reference(ADV_TYPE_SENSOR, "combo", true,
						-22.5, 113.9, 0x1020U, 0x3040U,
						"full combo");
	expect_builder_encode_matches_reference(ADV_TYPE_CHAT, "", false, 0.0, 0.0,
						0U, 0U, "empty name");
	expect_builder_encode_matches_reference(ADV_TYPE_CHAT, kLongName, false, 0.0,
						0.0, 0U, 0U, "name truncation");
}

ZTEST(meshcore_advert_data_tdd, test_parser_decode_matches_reference)
{
	uint8_t encoded[MESHCORE_MAX_ADVERT_DATA_LEN] = { 0 };
	struct meshcore_advert_data_builder builder = {};
	uint8_t len;
	uint8_t flags_only[] = { ADV_TYPE_CHAT };
	uint8_t short_latlon[9] = { (uint8_t)(ADV_TYPE_ROOM | ADV_LATLON_MASK),
				    0x01, 0x02, 0x03, 0x04, 0xA1, 0xA2, 0xA3, 0xA4 };
	uint8_t short_feat1[3] = { (uint8_t)(ADV_TYPE_SENSOR | ADV_FEAT1_MASK), 0x55,
				   0x66 };
	uint8_t short_feat2[5] = { (uint8_t)(ADV_TYPE_SENSOR | ADV_FEAT2_MASK), 0x10,
				   0x20, 0x30, 0x40 };
	uint8_t name_only[] = { (uint8_t)(ADV_TYPE_CHAT | ADV_NAME_MASK), 'a', 'b', 'c' };
	uint8_t name_flag_no_body[] = { (uint8_t)(ADV_TYPE_CHAT | ADV_NAME_MASK) };
	uint8_t empty_payload_backing[] = { 0xAE };
	uint8_t oversize_name[33] = { 0 };

	oversize_name[0] = (uint8_t)(ADV_TYPE_CHAT | ADV_NAME_MASK);
	memset(&oversize_name[1], 'N', sizeof(oversize_name) - 1U);

	meshcore_advert_data_builder_init_with_name_lat_lon(&builder, ADV_TYPE_REPEATER,
							    "peer-x", 12.345678,
							    -98.765432);
	meshcore_advert_data_builder_set_feat1(&builder, 0x7788U);
	meshcore_advert_data_builder_set_feat2(&builder, 0x99AAU);
	len = meshcore_advert_data_builder_encode_to(&builder, encoded);
	expect_parser_matches_reference(encoded, len, "parse encoded full combo");

	meshcore_advert_data_builder_init_with_name(&builder, ADV_TYPE_CHAT, "");
	len = meshcore_advert_data_builder_encode_to(&builder, encoded);
	expect_parser_matches_reference(encoded, len, "parse encoded empty name");

	expect_parser_matches_reference(flags_only, sizeof(flags_only), "flags only");
	expect_parser_matches_reference(short_feat2, 4U, "short feat2");
	expect_parser_matches_reference(name_only, sizeof(name_only), "name remainder");
	expect_parser_matches_reference(name_flag_no_body, sizeof(name_flag_no_body),
					"name flag no body");
	expect_parser_invalid_strict(short_latlon, 1U, "short latlon len1");
	expect_parser_invalid_strict(short_latlon, 8U, "short latlon len8");
	expect_parser_invalid_strict(short_feat1, 2U, "short feat1");
	expect_parser_invalid_strict(empty_payload_backing, 0U, "empty payload");
	expect_parser_invalid_strict(oversize_name, sizeof(oversize_name),
				     "oversize name");
}

ZTEST_SUITE(meshcore_advert_data_tdd, NULL, NULL, NULL, NULL, NULL);
