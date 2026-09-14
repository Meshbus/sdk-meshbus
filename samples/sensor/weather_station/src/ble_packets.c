/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include "ble_packets.h"

const uint8_t weather_packet_sizes[WEATHER_PACKET_COUNT] = {16, 14, 20};

void weather_encode(const struct readings *value, uint16_t sequence,
		    uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX])
{
	memset(packets, 0, WEATHER_PACKET_COUNT * WEATHER_PACKET_MAX);
	for (size_t i = 0; i < WEATHER_PACKET_COUNT; i++) {
		packets[i][0] = 1;
		sys_put_le16(sequence, packets[i] + 2);
		sys_put_le32((uint32_t)value->sampled_at, packets[i] + 4);
	}
	packets[1][13] = 0xff;
	if (value->sampled_at == 0) {
		return;
	}
	if (value->th_error == 0) {
		int64_t t = sensor_value_to_micro(&value->temperature) / 10000;
		int64_t rh = sensor_value_to_micro(&value->humidity) / 10000;

		if (t >= INT16_MIN && t <= INT16_MAX && rh >= 0 && rh <= 10000) {
			packets[0][1] |= BIT(0);
			sys_put_le16((uint16_t)(int16_t)t, packets[0] + 8);
			sys_put_le16(rh, packets[0] + 10);
		}
	}
	if (value->pressure_error == 0) {
		int64_t pa = sensor_value_to_micro(&value->pressure) / 1000;

		if (pa >= 0 && pa <= UINT32_MAX) {
			packets[0][1] |= BIT(1);
			sys_put_le32(pa, packets[0] + 12);
		}
	}
	/* 0xff: no trustworthy validity report, including a failed fetch. */
	packets[1][13] = value->air_error == 0 ? value->air.validity : 0xff;
	if (value->air_error == 0 && value->air.validity == 0) {
		packets[1][1] = BIT(0);
		sys_put_le16(value->air.eco2, packets[1] + 8);
		sys_put_le16(value->air.tvoc, packets[1] + 10);
		packets[1][12] = value->air.aqi;
	}
	if (value->pm_error == 0) {
		bool valid = true;

		for (size_t i = 0; i < 3; i++) {
			int64_t milli = sensor_value_to_micro(&value->pm[i]) / 1000;

			if (milli < 0 || milli > UINT32_MAX) {
				valid = false;
				break;
			}
			sys_put_le32(milli, packets[2] + 8 + i * 4);
		}
		packets[2][1] = valid ? BIT(0) : 0;
	}
}
