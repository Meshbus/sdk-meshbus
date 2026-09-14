/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WEATHER_BLE_TEXT_H_
#define WEATHER_BLE_TEXT_H_
#include "ble_packets.h"
#define WEATHER_TEXT_COUNT 9
#define WEATHER_TEXT_MAX   20
/* ASCII, also valid UTF-8. Returns bytes excluding the terminating NUL. */
size_t weather_text(size_t index, const uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX],
		    char text[WEATHER_TEXT_MAX + 1]);
#endif
