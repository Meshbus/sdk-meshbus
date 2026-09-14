/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include "ble_text.h"

size_t weather_text(size_t index, const uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX],
		    char text[WEATHER_TEXT_MAX + 1])
{
	static const char *const labels[] = {"TEMP", "RH",    "PRESS", "eCO2", "TVOC",
					     "AQI",  "PM1.0", "PM2.5", "PM10"};
	const size_t size = WEATHER_TEXT_MAX + 1;
	const uint8_t *packet;
	const char *state = NULL;
	int written;

	if (index >= WEATHER_TEXT_COUNT) {
		text[0] = '\0';
		return 0;
	}
	packet = packets[index < 3 ? 0 : index < 6 ? 1 : 2];
	if (packet[2] == 0 && packet[3] == 0 && sys_get_le32(packet + 4) == 0) {
		state = "WAIT";
	} else if (!(packet[1] & BIT(index == 2 ? 1 : 0))) {
		state = "N/A";
		if (index >= 3 && index < 6) {
			if (packet[13] == 1) {
				state = "WARM";
			} else if (packet[13] == 2) {
				state = "INIT";
			}
		}
	}
	if (state != NULL) {
		written = snprintf(text, size, "%s %s", labels[index], state);
	} else if (index < 3) {
		int64_t centi = index == 0   ? (int64_t)(int16_t)sys_get_le16(packet + 8)
				: index == 1 ? sys_get_le16(packet + 10)
					     : sys_get_le32(packet + 12);
		bool negative = centi < 0;
		uint32_t magnitude = negative ? -centi : centi;

		/* Pa is also hundredths of hPa. */
		written = snprintf(text, size, "%s %s%u.%02u %s", labels[index],
				   negative ? "-" : "", magnitude / 100, magnitude % 100,
				   index == 0   ? "C"
				   : index == 1 ? "%"
						: "hPa");
	} else if (index < 6) {
		unsigned int value = index == 3   ? sys_get_le16(packet + 8)
				     : index == 4 ? sys_get_le16(packet + 10)
						  : packet[12];

		written = snprintf(text, size, "%s %u%s", labels[index], value,
				   index == 3   ? " ppm"
				   : index == 4 ? " ppb"
						: "");
	} else {
		uint32_t milli = sys_get_le32(packet + 8 + (index - 6) * 4);

		written = snprintf(text, size, "%s %u.%03u ug/m3", labels[index], milli / 1000,
				   milli % 1000);
	}
	/* Never publish a truncated number/unit or require a larger ATT MTU. */
	if (written < 0 || written > WEATHER_TEXT_MAX) {
		snprintf(text, size, "%s RANGE", labels[index]);
	}
	return strlen(text);
}
