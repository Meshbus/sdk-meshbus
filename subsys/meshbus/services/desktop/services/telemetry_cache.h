/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/meshbus/telemetry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESKTOP_TELEMETRY_CACHE_COUNT 8U

struct desktop_telemetry_cache_entry {
	bool valid;
	uint32_t timestamp;
	enum sensor_channel chan;
	uint8_t value_count;
	struct sensor_value values[MESHBUS_TELEMETRY_MAX_VALUES];
};

uint32_t desktop_telemetry_cache_update_seq(void);
bool desktop_telemetry_cache_latest(struct desktop_telemetry_cache_entry *out);
bool desktop_telemetry_cache_get(enum sensor_channel chan,
				 struct desktop_telemetry_cache_entry *out);

#ifdef __cplusplus
}
#endif
