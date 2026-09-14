/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT sensirion_sps30

#include <math.h>
#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

struct sps30_config {
	struct i2c_dt_spec bus;
};
struct sps30_data {
	struct k_mutex lock;
	struct sensor_value mass[3];
	bool valid;
};

static int command(const struct device *dev, uint16_t code)
{
	const struct sps30_config *cfg = dev->config;
	uint8_t tx[2];

	sys_put_be16(code, tx);
	return i2c_write_dt(&cfg->bus, tx, sizeof(tx));
}

/* Separate STOP-delimited write/read, with CRC after each 16-bit word. */
static int read_words(const struct device *dev, uint16_t code, uint8_t *out, size_t size)
{
	const struct sps30_config *cfg = dev->config;
	uint8_t wire[60];
	int ret = command(dev, code);

	if (ret < 0) {
		return ret;
	}
	ret = i2c_read_dt(&cfg->bus, wire, size / 2 * 3);
	if (ret < 0) {
		return ret;
	}
	for (size_t i = 0; i < size / 2; i++) {
		if (crc8(&wire[i * 3], 2, 0x31, 0xff, false) != wire[i * 3 + 2]) {
			return -EBADMSG;
		}
		memcpy(&out[i * 2], &wire[i * 3], 2);
	}
	return 0;
}

static int channel_index(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_PM_1_0:
		return 0;
	case SENSOR_CHAN_PM_2_5:
		return 1;
	case SENSOR_CHAN_PM_10:
		return 2;
	default:
		return -ENOTSUP;
	}
}

static int sps30_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct sps30_data *data = dev->data;
	struct sensor_value mass[3];
	uint8_t ready[2];
	uint8_t frame[40];
	int ret;

	if (chan != SENSOR_CHAN_ALL && channel_index(chan) < 0) {
		return -ENOTSUP;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	ret = read_words(dev, 0x0202, ready, sizeof(ready));
	if (ret < 0) {
		goto out;
	}
	if (sys_get_be16(ready) != 1) {
		ret = -EAGAIN;
		goto out;
	}
	ret = read_words(dev, 0x0300, frame, sizeof(frame));
	if (ret < 0) {
		goto out;
	}
	/* Float outputs: PM1, PM2.5, PM4, PM10, number counts, typical size.
	 * Zephyr's standard mass channels cover indices 0, 1 and 3.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(mass); i++) {
		uint32_t bits = sys_get_be32(&frame[(i == 2 ? 3 : i) * 4]);
		float value;

		memcpy(&value, &bits, sizeof(value));
		/* Bound conversion well below int32 overflow, beyond sensor range. */
		if (!isfinite(value) || value < 0 || value > 1000000) {
			ret = -EBADMSG;
			goto out;
		}
		ret = sensor_value_from_float(&mass[i], value);
		if (ret < 0) {
			goto out;
		}
	}
	memcpy(data->mass, mass, sizeof(mass));
	data->valid = true;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int sps30_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
	struct sps30_data *data = dev->data;
	int index = channel_index(chan);
	int ret = 0;

	if (index < 0) {
		return index;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->valid) {
		ret = -ENODATA;
	} else {
		*val = data->mass[index];
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int sps30_init(const struct device *dev)
{
	const struct sps30_config *cfg = dev->config;
	struct sps30_data *data = dev->data;
	uint8_t start[] = {0x00, 0x10, 0x03, 0x00, 0};
	uint8_t version[2];
	int ret;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		return -ENODEV;
	}
	k_mutex_init(&data->lock);
	/* Reset SPS30 only: also handles a host reboot during measurement. */
	ret = command(dev, 0xd304);
	k_msleep(100);
	if (ret < 0) {
		return ret;
	}
	ret = read_words(dev, 0xd100, version, sizeof(version));
	if (ret < 0) {
		return ret;
	}
	start[4] = crc8(&start[2], 2, 0x31, 0xff, false);
	ret = i2c_write_dt(&cfg->bus, start, sizeof(start));
	k_msleep(20);
	return ret;
}

static DEVICE_API(sensor, sps30_api) = {
	.sample_fetch = sps30_fetch,
	.channel_get = sps30_get,
};
#define SPS30_DEFINE(inst)                                                                         \
	static const struct sps30_config config_##inst = {.bus = I2C_DT_SPEC_INST_GET(inst)};      \
	static struct sps30_data data_##inst;                                                      \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, sps30_init, NULL, &data_##inst, &config_##inst,         \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &sps30_api);
DT_INST_FOREACH_STATUS_OKAY(SPS30_DEFINE)
