/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include "ble_packets.h"

ZTEST(packets, test_units_and_layout)
{
	struct readings v = {
		.sampled_at = 123456,
		.temperature = {-2, -500000},
		.humidity = {65, 250000},
		.pressure = {100, 880000},
		.air = {.eco2 = 600, .tvoc = 21, .aqi = 2},
		.pm = {{1, 250000}, {2, 500000}, {10, 125000}},
	};
	uint8_t p[3][20];
	weather_encode(&v, 42, p);
	for (int i = 0; i < 3; i++) {
		zassert_equal(p[i][0], 1);
		zassert_equal(sys_get_le16(p[i] + 2), 42);
		zassert_equal(sys_get_le32(p[i] + 4), 123456);
		zassert_true(weather_packet_sizes[i] <= 20);
	}
	zassert_equal(p[0][1], 3);
	zassert_equal((int16_t)sys_get_le16(p[0] + 8), -250);
	zassert_equal(sys_get_le16(p[0] + 10), 6525);
	zassert_equal(sys_get_le32(p[0] + 12), 100880);
	zassert_equal(p[1][1], 1);
	zassert_equal(sys_get_le16(p[1] + 8), 600);
	zassert_equal(sys_get_le16(p[1] + 10), 21);
	zassert_equal(p[1][12], 2);
	zassert_equal(p[2][1], 1);
	zassert_equal(sys_get_le32(p[2] + 8), 1250);
	zassert_equal(sys_get_le32(p[2] + 12), 2500);
	zassert_equal(sys_get_le32(p[2] + 16), 10125);
}
ZTEST(packets, test_conditioning_and_errors)
{
	struct readings v = {.sampled_at = 1,
			     .th_error = -EIO,
			     .pm_error = -EAGAIN,
			     .air = {.validity = 2, .eco2 = 999}};
	uint8_t p[3][20];
	weather_encode(&v, 0xffff, p);
	zassert_equal(p[0][1], 2);
	zassert_equal(p[1][1], 0);
	zassert_equal(p[1][13], 2);
	zassert_equal(sys_get_le16(p[1] + 8), 0);
	zassert_equal(p[2][1], 0);
	v.air_error = -EIO;
	weather_encode(&v, 0, p);
	zassert_equal(p[1][13], 0xff);
}
ZTEST(packets, test_startup_and_range)
{
	struct readings v = {0};
	uint8_t p[3][20];
	weather_encode(&v, 0, p);
	for (int i = 0; i < 3; i++) {
		zassert_equal(p[i][1], 0);
	}
	zassert_equal(p[1][13], 0xff);
	v.sampled_at = 1;
	v.humidity.val1 = 101;
	v.pressure.val1 = -1;
	v.pm[2].val1 = -1;
	weather_encode(&v, 1, p);
	zassert_equal(p[0][1], 0);
	zassert_equal(p[2][1], 0);
}
ZTEST_SUITE(packets, NULL, NULL, NULL, NULL, NULL);

#include "ble_text.h"
ZTEST(packets, test_readable_text)
{
	struct readings v = {.sampled_at = 1,
			     .temperature = {-2, -500000},
			     .humidity = {65, 250000},
			     .pressure = {100, 880000},
			     .air = {.eco2 = 600, .tvoc = 21, .aqi = 2},
			     .pm = {{1, 250000}, {2, 500000}, {10, 125000}}};
	const char *expected[] = {"TEMP -2.50 C",      "RH 65.25 %",        "PRESS 1008.80 hPa",
				  "eCO2 600 ppm",      "TVOC 21 ppb",       "AQI 2",
				  "PM1.0 1.250 ug/m3", "PM2.5 2.500 ug/m3", "PM10 10.125 ug/m3"};
	uint8_t p[3][20];
	char text[21];
	weather_encode(&v, 1, p);
	for (int i = 0; i < 9; i++) {
		zassert_true(weather_text(i, p, text) <= 20);
		zassert_equal(strcmp(text, expected[i]), 0, "%s", text);
	}
	v.air.validity = 2;
	v.pm_error = -EIO;
	weather_encode(&v, 2, p);
	weather_text(3, p, text);
	zassert_equal(strcmp(text, "eCO2 INIT"), 0);
	weather_text(6, p, text);
	zassert_equal(strcmp(text, "PM1.0 N/A"), 0);
	v.air.validity = 1;
	weather_encode(&v, 3, p);
	weather_text(4, p, text);
	zassert_equal(strcmp(text, "TVOC WARM"), 0);
	v.sampled_at = 0;
	weather_encode(&v, 0, p);
	weather_text(0, p, text);
	zassert_equal(strcmp(text, "TEMP WAIT"), 0);
}
ZTEST(packets, test_text_does_not_truncate)
{
	struct readings v = {.sampled_at = 1, .pm = {{1000000, 0}, {0, 0}, {0, 0}}};
	uint8_t p[3][20];
	char text[21];
	weather_encode(&v, 1, p);
	weather_text(6, p, text);
	zassert_equal(strcmp(text, "PM1.0 RANGE"), 0);
	zassert_equal(weather_text(9, p, text), 0);
}
