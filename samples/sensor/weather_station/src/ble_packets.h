/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WEATHER_BLE_PACKETS_H_
#define WEATHER_BLE_PACKETS_H_
#include "readings.h"
#define WEATHER_PACKET_COUNT 3
#define WEATHER_PACKET_MAX   20
extern const uint8_t weather_packet_sizes[WEATHER_PACKET_COUNT];
void weather_encode(const struct readings *value, uint16_t sequence,
		    uint8_t packets[WEATHER_PACKET_COUNT][WEATHER_PACKET_MAX]);
#endif
