#ifndef ZEPHYR_DRIVERS_SENSOR_ECSENSE_ES4_AG1_H_
#define ZEPHYR_DRIVERS_SENSOR_ECSENSE_ES4_AG1_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/sensor/es4_ag1.h>

struct adc_dt_spec;

struct es4_ag1_data {
	struct k_mutex lock;
	int32_t avg_uv;
	int64_t voc_micro_ppm;
	int32_t zero_offset_uv;
	int32_t transimpedance_ohms;
	int32_t sensitivity_nanoamp_per_ppm;
	bool sample_valid;
};

struct es4_ag1_config {
	struct adc_dt_spec adc;
	int32_t zero_offset_uv_default;
	int32_t transimpedance_ohms_default;
	int32_t sensitivity_nanoamp_per_ppm_default;
	bool inverted_polarity;
	uint16_t averaging_samples;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_ECSENSE_ES4_AG1_H_ */
