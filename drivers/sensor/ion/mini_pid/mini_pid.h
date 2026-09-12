#ifndef ZEPHYR_DRIVERS_SENSOR_ION_MINI_PID_COMPAT_H_
#define ZEPHYR_DRIVERS_SENSOR_ION_MINI_PID_COMPAT_H_

#include <stdbool.h>
#include <stdint.h>

#include <drivers/sensor/mini_pid.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

struct adc_dt_spec;

struct mini_pid_data {
	struct k_mutex lock;
	int32_t voltage;
	int64_t voc_micro_ppm;
	int32_t baseline;
	int32_t sensitivity;
	bool sample_valid;
};

struct mini_pid_config {
	const struct adc_dt_spec adc;
	int32_t baseline_default;
	int32_t sensitivity_default;
	uint16_t averaging_samples;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_ION_MINI_PID_COMPAT_H_ */
