/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include "air_quality.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

/* Sample owns this device: CONFIG_ENS160 is disabled. Unlike the workspace
 * driver, preserve conditioning states instead of failing initialization.
 * Register contract: ScioSense ENS160 datasheet v1.3, sections 16.2.1-16.2.10.
 */
static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(DT_NODELABEL(weather_air));

int air_init(void)
{
	uint8_t id[2];
	int ret;

	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	ret = i2c_burst_read_dt(&bus, 0x00, id, sizeof(id));
	if (ret < 0) {
		return ret;
	}
	if (sys_get_le16(id) != 0x0160) {
		return -ENODEV;
	}
	ret = i2c_reg_write_byte_dt(&bus, 0x10, 0x01); /* Idle */
	if (ret < 0) {
		return ret;
	}
	k_msleep(10);
	ret = i2c_reg_write_byte_dt(&bus, 0x10, 0x02); /* Standard */
	k_msleep(10);
	return ret;
}

int air_compensate(const struct sensor_value *temperature, const struct sensor_value *humidity)
{
	int64_t temp = sensor_value_to_micro(temperature);
	int64_t rh = sensor_value_to_micro(humidity);
	uint8_t values[4];

	if (temp < -5000000 || temp > 60000000 || rh < 20000000 || rh > 80000000) {
		return -ERANGE;
	}
	sys_put_le16((temp + 273150000) * 64 / 1000000, values);
	sys_put_le16(rh * 512 / 1000000, values + 2);
	return i2c_burst_write_dt(&bus, 0x13, values, sizeof(values));
}

int air_read(struct air_reading *reading)
{
	uint8_t status;
	uint8_t values[5];
	int ret = i2c_reg_read_byte_dt(&bus, 0x20, &status);

	if (ret < 0) {
		return ret;
	}
	reading->validity = (status >> 2) & 3;
	if ((status & BIT(6)) || !(status & BIT(7))) {
		return -EIO;
	}
	if (reading->validity != 0) {
		return 0;
	}
	if (!(status & BIT(1))) {
		return -EAGAIN;
	}
	ret = i2c_burst_read_dt(&bus, 0x21, values, sizeof(values));
	if (ret < 0) {
		return ret;
	}
	reading->aqi = values[0] & 7;
	reading->tvoc = sys_get_le16(values + 1);
	reading->eco2 = sys_get_le16(values + 3);
	return reading->aqi >= 1 && reading->aqi <= 5 ? 0 : -EBADMSG;
}
