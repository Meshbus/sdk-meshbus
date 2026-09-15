/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MATTER_AIR_QUALITY_H_
#define MATTER_AIR_QUALITY_H_
#include <zephyr/drivers/sensor.h>

struct air_reading {
	uint16_t eco2;
	uint16_t tvoc;
	uint8_t aqi;
	uint8_t validity;
};

int air_init(void);
int air_compensate(const struct sensor_value *temperature, const struct sensor_value *humidity);
/* -EAGAIN means no new sample. Validity 1/2 means conditioning, 3 invalid. */
int air_read(struct air_reading *reading);
#endif
