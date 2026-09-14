/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WEATHER_READINGS_H_
#define WEATHER_READINGS_H_
#include "air_quality.h"
struct readings {
	struct sensor_value temperature;
	struct sensor_value humidity;
	struct sensor_value pressure;
	struct air_reading air;
	struct sensor_value pm[3];
	int pm_error;
	int th_error;
	int pressure_error;
	int air_error;
	int64_t sampled_at;
};

#endif
