/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SAMPLES_DRIVERS_SENSOR_COMPOSITE_COMPASS_SHELL_H_
#define FOBE_SAMPLES_DRIVERS_SENSOR_COMPOSITE_COMPASS_SHELL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

void compass_sample_shell_bind_device(const struct device *dev);
bool compass_sample_stream_enabled_get(void);
uint32_t compass_sample_stream_interval_ms_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FOBE_SAMPLES_DRIVERS_SENSOR_COMPOSITE_COMPASS_SHELL_H_ */
