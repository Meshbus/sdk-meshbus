// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <CayenneLPP.h>

#include <cmath>
#include <errno.h>
#include <map>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

extern "C" {
#include "meshcore_cayenne_lpp_compat.h"
}

namespace meshcore_cayenne_lpp_compat_tdd {

static constexpr uint8_t kGpsChannel = 7U;
static constexpr uint8_t kOtherChannel = 3U;
static constexpr size_t kBufferCap = 64U;
static constexpr uint8_t kLppUnknownType = 0xEEU;
static constexpr uint8_t kLppGenericSensor = 100U;
static constexpr uint8_t kLppTemperature = 103U;
static constexpr uint8_t kLppFrequency = 118U;
static constexpr uint8_t kLppPercentage = 120U;
static constexpr uint8_t kLppConcentration = 125U;
static constexpr uint8_t kLppEnergy = 131U;
static constexpr uint8_t kLppDirection = 132U;
static constexpr uint8_t kLppUnixTime = 133U;
static constexpr uint8_t kLppColour = 135U;
static constexpr uint8_t kLppSwitch = 142U;
static constexpr uint8_t kLppPolyline = 240U;
static constexpr int32_t kGpsE4Scale = 10000;
static constexpr int32_t kGpsE6Scale = 1000000;

struct ExpectedLocation {
	bool has_latitude;
	int32_t latitude_e6;
	bool has_longitude;
	int32_t longitude_e6;
};

static ExpectedLocation expected_location_from_reference(const uint8_t *payload,
						 uint8_t payload_len)
{
	ExpectedLocation location = { false, 0, false, 0 };
	CayenneLPP parser((uint8_t)kBufferCap);
	std::map<uint8_t, CayenneLPPMessage> messages;
	uint8_t buffer[kBufferCap] = { 0 };

	if (payload == NULL || payload_len == 0U) {
		return location;
	}

	zassert_true(payload_len <= sizeof(buffer), "payload too large for oracle");
	memcpy(buffer, payload, payload_len);
	(void)parser.decode(buffer, payload_len, messages);

	const auto it = messages.find(kGpsChannel);
	if (it != messages.end()) {
		const int32_t latitude_e4 =
			(int32_t)std::lround(it->second.gps[0] * (float)kGpsE4Scale);
		const int32_t longitude_e4 =
			(int32_t)std::lround(it->second.gps[1] * (float)kGpsE4Scale);

		location.has_latitude = true;
		location.latitude_e6 = latitude_e4 * (kGpsE6Scale / kGpsE4Scale);
		location.has_longitude = true;
		location.longitude_e6 =
			longitude_e4 * (kGpsE6Scale / kGpsE4Scale);
	}

	return location;
}

static void expect_location_matches(const ExpectedLocation &expected,
				    const struct cayenne_lpp_location &actual,
				    const char *label)
{
	zassert_equal(expected.has_latitude, actual.has_latitude,
		      "%s has_latitude mismatch", label);
	zassert_equal(expected.latitude_e6, actual.latitude_e6,
		      "%s latitude mismatch", label);
	zassert_equal(expected.has_longitude, actual.has_longitude,
		      "%s has_longitude mismatch", label);
	zassert_equal(expected.longitude_e6, actual.longitude_e6,
		      "%s longitude mismatch", label);
}

static void expect_single_value_encode_matches_reference(
	uint8_t (CayenneLPP::*expected_add)(uint8_t, float),
	int (*actual_add)(struct cayenne_lpp_writer *, uint8_t, float),
	float value, size_t cap, const char *label)
{
	CayenneLPP expected((uint8_t)cap);
	struct cayenne_lpp_writer actual = {};
	uint8_t expected_buf[kBufferCap] = { 0 };
	uint8_t actual_buf[kBufferCap] = { 0 };
	uint8_t expected_rc;
	uint8_t expected_len;
	int actual_rc;

	memset(expected_buf, 0xA5, sizeof(expected_buf));
	memset(actual_buf, 0xA5, sizeof(actual_buf));

	cayenne_lpp_init(&actual, actual_buf, cap);

	expected_rc = (expected.*expected_add)(kOtherChannel, value);
	expected_len = expected.copy(expected_buf);
	actual_rc = actual_add(&actual, kOtherChannel, value);

	if (expected_rc == 0U) {
		zassert_equal(LPP_ERROR_OVERFLOW, expected.getError(),
			      "%s expected overflow mismatch", label);
		zassert_equal(-ENOSPC, actual_rc, "%s actual overflow mismatch",
			      label);
		zassert_equal(expected_len, actual.len, "%s len mismatch", label);
		zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
				  "%s raw mismatch", label);
		return;
	}

	zassert_equal(LPP_ERROR_OK, expected.getError(),
		      "%s expected error mismatch", label);
	zassert_equal(0, actual_rc, "%s actual rc mismatch", label);
	zassert_equal(expected_len, actual.len, "%s len mismatch", label);
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "%s raw mismatch", label);
}

static void expect_gps_encode_matches_reference(float latitude, float longitude,
						float altitude, size_t cap,
						const char *label)
{
	CayenneLPP expected((uint8_t)cap);
	struct cayenne_lpp_writer actual = {};
	uint8_t expected_buf[kBufferCap] = { 0 };
	uint8_t actual_buf[kBufferCap] = { 0 };
	uint8_t expected_rc;
	uint8_t expected_len;
	int actual_rc;

	memset(expected_buf, 0xA5, sizeof(expected_buf));
	memset(actual_buf, 0xA5, sizeof(actual_buf));

	cayenne_lpp_init(&actual, actual_buf, cap);

	expected_rc = expected.addGPS(kGpsChannel, latitude, longitude, altitude);
	expected_len = expected.copy(expected_buf);
	actual_rc = cayenne_lpp_add_gps(&actual, kGpsChannel, latitude, longitude,
					 altitude);

	if (expected_rc == 0U) {
		zassert_equal(LPP_ERROR_OVERFLOW, expected.getError(),
			      "%s expected overflow mismatch", label);
		zassert_equal(-ENOSPC, actual_rc, "%s actual overflow mismatch",
			      label);
		zassert_equal(expected_len, actual.len, "%s len mismatch", label);
		zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
				  "%s raw mismatch", label);
		return;
	}

	zassert_equal(LPP_ERROR_OK, expected.getError(),
		      "%s expected error mismatch", label);
	zassert_equal(0, actual_rc, "%s actual rc mismatch", label);
	zassert_equal(expected_len, actual.len, "%s len mismatch", label);
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "%s raw mismatch", label);
}

static void expect_parse_location_matches_reference(const uint8_t *payload,
							    uint8_t payload_len,
							    const char *label)
{
	struct cayenne_lpp_location actual = { false, 0, false, 0 };
	ExpectedLocation expected =
		expected_location_from_reference(payload, payload_len);

	cayenne_lpp_parse_location(payload, payload_len, &actual);
	expect_location_matches(expected, actual, label);
}

static void append_lpp_field(uint8_t *payload, size_t &len, uint8_t channel,
			     uint8_t type, const uint8_t *value, size_t value_len)
{
	zassert_true((len + 2U + value_len) <= kBufferCap, "payload overflow");
	payload[len++] = channel;
	payload[len++] = type;
	memcpy(&payload[len], value, value_len);
	len += value_len;
}

static void append_known_skip_fields(uint8_t *payload, size_t &len)
{
	const uint8_t four_bytes[] = { 0x00U, 0x00U, 0x00U, 0x01U };
	const uint8_t two_bytes[] = { 0x00U, 0x01U };
	const uint8_t one_byte[] = { 0x01U };
	const uint8_t colour[] = { 0x11U, 0x22U, 0x33U };
	const uint8_t polyline[] = {
		8U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U
	};

	append_lpp_field(payload, len, kOtherChannel, kLppGenericSensor, four_bytes,
			 sizeof(four_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppFrequency, four_bytes,
			 sizeof(four_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppPercentage, one_byte,
			 sizeof(one_byte));
	append_lpp_field(payload, len, kOtherChannel, kLppConcentration, two_bytes,
			 sizeof(two_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppEnergy, four_bytes,
			 sizeof(four_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppDirection, two_bytes,
			 sizeof(two_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppUnixTime, four_bytes,
			 sizeof(four_bytes));
	append_lpp_field(payload, len, kOtherChannel, kLppColour, colour,
			 sizeof(colour));
	append_lpp_field(payload, len, kOtherChannel, kLppSwitch, one_byte,
			 sizeof(one_byte));
	append_lpp_field(payload, len, kOtherChannel, kLppPolyline, polyline,
			 sizeof(polyline));
}

ZTEST(meshcore_cayenne_lpp_compat_tdd, test_writer_encode_matches_reference)
{
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addAnalogInput, cayenne_lpp_add_analog_input, 0.0f,
		kBufferCap, "analog zero");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addAnalogInput, cayenne_lpp_add_analog_input, -12.34f,
		kBufferCap, "analog negative");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addBarometricPressure,
		cayenne_lpp_add_barometric_pressure, 1001.2f, kBufferCap,
		"barometric");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addDistance, cayenne_lpp_add_distance, 12.345f,
		kBufferCap, "distance");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addRelativeHumidity,
		cayenne_lpp_add_relative_humidity, 55.5f, kBufferCap,
		"humidity");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addTemperature, cayenne_lpp_add_temperature, -4.2f,
		kBufferCap, "temperature negative");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addVoltage, cayenne_lpp_add_voltage, 4.19f,
		kBufferCap, "voltage");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addCurrent, cayenne_lpp_add_current, 0.123f,
		kBufferCap, "current");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addPower, cayenne_lpp_add_power, 77.0f, kBufferCap,
		"power");
	expect_single_value_encode_matches_reference(
		&CayenneLPP::addAltitude, cayenne_lpp_add_altitude, -15.0f,
		kBufferCap, "altitude negative");
	expect_gps_encode_matches_reference(22.5432f, 114.0579f, 12.34f, kBufferCap,
					    "gps positive");
	expect_gps_encode_matches_reference(-22.5432f, -114.0579f, -1.23f,
					    kBufferCap, "gps negative");
}

ZTEST(meshcore_cayenne_lpp_compat_tdd,
      test_writer_size_overflow_and_invalid_args_match_expected)
{
	CayenneLPP expected(15U);
	struct cayenne_lpp_writer actual = {};
	uint8_t expected_buf[kBufferCap] = { 0 };
	uint8_t actual_buf[kBufferCap] = { 0 };
	uint8_t expected_len;
	size_t len_before;

	memset(expected_buf, 0xA5, sizeof(expected_buf));
	memset(actual_buf, 0xA5, sizeof(actual_buf));

	cayenne_lpp_init(&actual, actual_buf, 15U);
	zassert_equal(0U, cayenne_lpp_size(&actual), "init size mismatch");
	zassert_equal(0U, cayenne_lpp_size(NULL), "NULL size mismatch");

	zassert_equal(4U, expected.addVoltage(kOtherChannel, 4.2f),
		      "reference voltage write failed");
	zassert_equal(0, cayenne_lpp_add_voltage(&actual, kOtherChannel, 4.2f),
		      "actual voltage write failed");
	zassert_equal(15U, expected.addGPS(kGpsChannel, 22.1f, 114.2f, 0.0f),
		      "reference gps write failed");
	zassert_equal(0, cayenne_lpp_add_gps(&actual, kGpsChannel, 22.1f, 114.2f,
					      0.0f),
		      "actual gps write failed");

	expected_len = expected.copy(expected_buf);
	zassert_equal(expected_len, actual.len, "combined len mismatch");
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "combined raw mismatch");

	len_before = actual.len;
	zassert_equal(0U, expected.addPower(kOtherChannel, 11.0f),
		      "reference overflow should fail");
	zassert_equal(LPP_ERROR_OVERFLOW, expected.getError(),
		      "reference overflow code mismatch");
	zassert_equal(-ENOSPC,
		      cayenne_lpp_add_power(&actual, kOtherChannel, 11.0f),
		      "actual overflow code mismatch");
	zassert_equal(len_before, actual.len, "overflow advanced len");
	zassert_mem_equal(expected_buf, actual_buf, sizeof(expected_buf),
			  "overflow mutated raw");

	zassert_equal(-EINVAL, cayenne_lpp_add_voltage(NULL, kOtherChannel, 1.0f),
		      "NULL writer mismatch");
	actual.buf = NULL;
	zassert_equal(-EINVAL,
		      cayenne_lpp_add_voltage(&actual, kOtherChannel, 1.0f),
		      "NULL buffer mismatch");
}

ZTEST(meshcore_cayenne_lpp_compat_tdd, test_parse_location_matches_reference)
{
	CayenneLPP builder((uint8_t)kBufferCap);
	uint8_t payload[kBufferCap] = { 0 };
	uint8_t valid_gps_payload[kBufferCap] = { 0 };
	uint8_t valid_gps_len;
	uint8_t payload_len;

	expect_parse_location_matches_reference(NULL, 0U, "empty payload");

	builder.addVoltage(kOtherChannel, 4.2f);
	builder.addTemperature((uint8_t)(kOtherChannel + 1U), 22.5f);
	payload_len = builder.copy(payload);
	expect_parse_location_matches_reference(payload, payload_len,
						"non gps only");

	builder.reset();
	builder.addGPS(kGpsChannel, 22.5432f, 114.0579f, 0.0f);
	payload_len = builder.copy(payload);
	expect_parse_location_matches_reference(payload, payload_len,
						"single gps");

	builder.reset();
	builder.addVoltage(kOtherChannel, 4.2f);
	builder.addGPS(kGpsChannel, 22.5001f, 114.0002f, 0.0f);
	builder.addPower((uint8_t)(kOtherChannel + 1U), 9.0f);
	payload_len = builder.copy(payload);
	expect_parse_location_matches_reference(payload, payload_len,
						"gps with other fields");

	builder.reset();
	builder.addGPS(kGpsChannel, 1.1111f, 2.2222f, 0.0f);
	builder.addTemperature(kOtherChannel, 20.0f);
	builder.addGPS(kGpsChannel, -3.3333f, -4.4444f, 0.0f);
	payload_len = builder.copy(payload);
	expect_parse_location_matches_reference(payload, payload_len,
						"last gps wins");

	builder.reset();
	builder.addGPS(kGpsChannel, 11.1111f, 22.2222f, 0.0f);
	valid_gps_len = builder.copy(valid_gps_payload);

	memcpy(payload, valid_gps_payload, valid_gps_len);
	payload[valid_gps_len] = kOtherChannel;
	payload[valid_gps_len + 1U] = kLppUnknownType;
	payload[valid_gps_len + 2U] = 0xAAU;
	expect_parse_location_matches_reference(valid_gps_payload, valid_gps_len,
						"gps prefix before unknown");
	{
		struct cayenne_lpp_location actual = { false, 0, false, 0 };
		ExpectedLocation expected = expected_location_from_reference(
			payload, (uint8_t)(valid_gps_len + 3U));

		cayenne_lpp_parse_location(payload, (uint8_t)(valid_gps_len + 3U),
					   &actual);
		expect_location_matches(expected, actual, "unknown after gps");
	}

	payload[0] = kOtherChannel;
	payload[1] = kLppUnknownType;
	payload[2] = 0x55U;
	memcpy(&payload[3], valid_gps_payload, valid_gps_len);
	{
		struct cayenne_lpp_location actual = { true, 1, true, 1 };
		ExpectedLocation expected = expected_location_from_reference(
			payload, (uint8_t)(valid_gps_len + 3U));

		cayenne_lpp_parse_location(payload, (uint8_t)(valid_gps_len + 3U),
					   &actual);
		expect_location_matches(expected, actual, "unknown before gps");
	}

	memcpy(payload, valid_gps_payload, valid_gps_len);
	payload[valid_gps_len] = kOtherChannel;
	payload[valid_gps_len + 1U] = kLppTemperature;
	payload[valid_gps_len + 2U] = 0x00U;
	{
		struct cayenne_lpp_location actual = { false, 0, false, 0 };
		ExpectedLocation expected = expected_location_from_reference(
			payload, (uint8_t)(valid_gps_len + 3U));

		cayenne_lpp_parse_location(payload, (uint8_t)(valid_gps_len + 3U),
					   &actual);
		expect_location_matches(expected, actual, "truncated after gps");
	}

	builder.reset();
	builder.addGPS(kGpsChannel, 12.3456f, 23.4567f, 0.0f);
	valid_gps_len = builder.copy(valid_gps_payload);
	{
		size_t mixed_len = 0U;
		struct cayenne_lpp_location actual = { false, 0, false, 0 };
		ExpectedLocation expected = expected_location_from_reference(
			valid_gps_payload, valid_gps_len);

		append_known_skip_fields(payload, mixed_len);
		zassert_true((mixed_len + valid_gps_len) <= sizeof(payload),
			     "mixed payload overflow");
		memcpy(&payload[mixed_len], valid_gps_payload, valid_gps_len);
		mixed_len += valid_gps_len;

		cayenne_lpp_parse_location(payload, mixed_len, &actual);
		expect_location_matches(expected, actual,
					"known skipped types before gps");
	}

		{
			size_t mixed_len = 0U;
			struct cayenne_lpp_location actual = { true, 1, true, 1 };
			const uint8_t malformed_polyline[] = { 8U, 0x01U, 0x02U };
			ExpectedLocation expected = { false, 0, false, 0 };

			append_lpp_field(payload, mixed_len, kOtherChannel, kLppPolyline,
					 malformed_polyline, sizeof(malformed_polyline));
			zassert_true((mixed_len + valid_gps_len) <= sizeof(payload),
				     "malformed polyline payload overflow");
			memcpy(&payload[mixed_len], valid_gps_payload, valid_gps_len);
			mixed_len += valid_gps_len;

			cayenne_lpp_parse_location(payload, mixed_len, &actual);
			expect_location_matches(expected, actual,
						"malformed polyline before gps");
		}
}

ZTEST_SUITE(meshcore_cayenne_lpp_compat_tdd, NULL, NULL, NULL, NULL, NULL);

} /* namespace meshcore_cayenne_lpp_compat_tdd */
