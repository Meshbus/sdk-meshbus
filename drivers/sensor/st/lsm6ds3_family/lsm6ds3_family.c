/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fobe_lsm6ds3_family

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(lsm6ds3_family, CONFIG_SENSOR_LOG_LEVEL);

#define LSM6DS3_REG_WHO_AM_I 0x0f
#define LSM6DS3_REG_CTRL1_XL 0x10
#define LSM6DS3_REG_CTRL2_G  0x11
#define LSM6DS3_REG_CTRL3_C  0x12
#define LSM6DS3_REG_OUT_TEMP 0x20
#define LSM6DS3_REG_OUT_G    0x22
#define LSM6DS3_REG_OUT_XL   0x28

#define LSM6DS3_ID_TR   0x69
#define LSM6DS3_ID_TR_C 0x6a

#define LSM6DS3_CTRL_ODR_MASK   GENMASK(7, 4)
#define LSM6DS3_CTRL_XL_FS_MASK GENMASK(3, 2)
#define LSM6DS3_CTRL_G_FS_MASK  (GENMASK(3, 2) | BIT(1))
#define LSM6DS3_CTRL3_BOOT      BIT(7)
#define LSM6DS3_CTRL3_BDU       BIT(6)
#define LSM6DS3_CTRL3_IF_INC    BIT(2)
#define LSM6DS3_CTRL3_SW_RESET  BIT(0)

#define LSM6DS3_AXIS_COUNT          3U
#define LSM6DS3_ALL_SAMPLE_BYTES    14U
#define LSM6DS3_VECTOR_SAMPLE_BYTES 6U
#define LSM6DS3_BOOT_WAIT_MS        35U
#define LSM6DS3_RESET_TIMEOUT_MS    50U

BUILD_ASSERT(CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_FS == 2 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_FS == 4 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_FS == 8 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_FS == 16,
	     "LSM6DS3 accelerometer full scale must be 2, 4, 8, or 16 g");
BUILD_ASSERT(CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS == 125 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS == 250 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS == 500 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS == 1000 ||
	     CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS == 2000,
	     "LSM6DS3 gyroscope full scale must be 125, 250, 500, 1000, or 2000 dps");

enum lsm6ds3_variant {
	LSM6DS3_VARIANT_TR,
	LSM6DS3_VARIANT_TR_C,
};

struct lsm6ds3_family_config {
	struct i2c_dt_spec i2c;
	uint8_t accel_odr;
	uint8_t gyro_odr;
	uint32_t accel_fs;
	uint32_t gyro_fs;
};

struct lsm6ds3_family_data {
	struct k_mutex lock;
	uint8_t accel_odr;
	uint8_t gyro_odr;
	uint8_t accel_fs_reg;
	uint8_t gyro_fs_reg;
	uint16_t accel_sensitivity_ug;
	uint32_t gyro_sensitivity_udps;
	int16_t accel[LSM6DS3_AXIS_COUNT];
	int16_t gyro[LSM6DS3_AXIS_COUNT];
	int16_t temperature;
	enum lsm6ds3_variant variant;
	bool active;
};

static const char *lsm6ds3_variant_name(enum lsm6ds3_variant variant)
{
	return variant == LSM6DS3_VARIANT_TR ? "LSM6DS3/LSM6DS3TR" : "LSM6DS3TR-C/LSM6DSL";
}

static int lsm6ds3_accel_fs_get(uint32_t fs, uint8_t *reg, uint16_t *sensitivity_ug)
{
	switch (fs) {
	case 2:
		*reg = 0U;
		*sensitivity_ug = 61U;
		return 0;
	case 4:
		*reg = 2U;
		*sensitivity_ug = 122U;
		return 0;
	case 8:
		*reg = 3U;
		*sensitivity_ug = 244U;
		return 0;
	case 16:
		*reg = 1U;
		*sensitivity_ug = 488U;
		return 0;
	default:
		return -EINVAL;
	}
}

static int lsm6ds3_gyro_fs_get(uint32_t fs, uint8_t *reg, uint32_t *sensitivity_udps)
{
	switch (fs) {
	case 125:
		*reg = BIT(1);
		*sensitivity_udps = 4375U;
		return 0;
	case 250:
		*reg = 0U;
		*sensitivity_udps = 8750U;
		return 0;
	case 500:
		*reg = 1U << 2;
		*sensitivity_udps = 17500U;
		return 0;
	case 1000:
		*reg = 2U << 2;
		*sensitivity_udps = 35000U;
		return 0;
	case 2000:
		*reg = 3U << 2;
		*sensitivity_udps = 70000U;
		return 0;
	default:
		return -EINVAL;
	}
}

static int lsm6ds3_odr_from_value(const struct sensor_value *value, uint8_t *odr)
{
	int64_t micro_hz;

	if (value == NULL || odr == NULL || value->val1 < 0 || value->val2 < 0 ||
	    value->val2 >= 1000000) {
		return -EINVAL;
	}

	micro_hz = ((int64_t)value->val1 * 1000000LL) + value->val2;
	switch (micro_hz) {
	case 12500000LL:
		*odr = 1U;
		break;
	case 26000000LL:
		*odr = 2U;
		break;
	case 52000000LL:
		*odr = 3U;
		break;
	case 104000000LL:
		*odr = 4U;
		break;
	case 208000000LL:
		*odr = 5U;
		break;
	case 416000000LL:
		*odr = 6U;
		break;
	case 833000000LL:
		*odr = 7U;
		break;
	case 1660000000LL:
		*odr = 8U;
		break;
	case 3330000000LL:
		*odr = 9U;
		break;
	case 6660000000LL:
		*odr = 10U;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lsm6ds3_odr_to_value(uint8_t odr, struct sensor_value *value)
{
	if (value == NULL) {
		return -EINVAL;
	}

	value->val2 = 0;
	switch (odr) {
	case 1U:
		value->val1 = 12;
		value->val2 = 500000;
		break;
	case 2U:
		value->val1 = 26;
		break;
	case 3U:
		value->val1 = 52;
		break;
	case 4U:
		value->val1 = 104;
		break;
	case 5U:
		value->val1 = 208;
		break;
	case 6U:
		value->val1 = 416;
		break;
	case 7U:
		value->val1 = 833;
		break;
	case 8U:
		value->val1 = 1660;
		break;
	case 9U:
		value->val1 = 3330;
		break;
	case 10U:
		value->val1 = 6660;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lsm6ds3_check_id(const struct device *dev)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	struct lsm6ds3_family_data *data = dev->data;
	uint8_t chip_id;
	int rc;

	rc = i2c_reg_read_byte_dt(&cfg->i2c, LSM6DS3_REG_WHO_AM_I, &chip_id);
	if (rc != 0) {
		LOG_ERR("%s: failed to read WHO_AM_I (%d)", dev->name, rc);
		return rc;
	}
	if (chip_id == LSM6DS3_ID_TR) {
		data->variant = LSM6DS3_VARIANT_TR;
	} else if (chip_id == LSM6DS3_ID_TR_C) {
		data->variant = LSM6DS3_VARIANT_TR_C;
	} else {
		LOG_ERR("%s: unsupported WHO_AM_I 0x%02x", dev->name, chip_id);
		return -ENODEV;
	}

	return 0;
}

static int lsm6ds3_reset(const struct device *dev)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	uint8_t ctrl3;
	int rc;

	rc = i2c_reg_update_byte_dt(&cfg->i2c, LSM6DS3_REG_CTRL3_C, LSM6DS3_CTRL3_SW_RESET,
				    LSM6DS3_CTRL3_SW_RESET);
	if (rc != 0) {
		return rc;
	}
	for (size_t attempt = 0U; attempt < LSM6DS3_RESET_TIMEOUT_MS; attempt++) {
		k_msleep(1);
		rc = i2c_reg_read_byte_dt(&cfg->i2c, LSM6DS3_REG_CTRL3_C, &ctrl3);
		if (rc != 0) {
			return rc;
		}
		if ((ctrl3 & LSM6DS3_CTRL3_SW_RESET) == 0U) {
			return 0;
		}
	}

	LOG_ERR("%s: software reset timed out", dev->name);
	return -ETIMEDOUT;
}

static int lsm6ds3_configure(const struct device *dev, bool active)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	struct lsm6ds3_family_data *data = dev->data;
	int rc;

	rc = i2c_reg_update_byte_dt(&cfg->i2c, LSM6DS3_REG_CTRL3_C,
				    LSM6DS3_CTRL3_BOOT | LSM6DS3_CTRL3_BDU | LSM6DS3_CTRL3_IF_INC,
				    LSM6DS3_CTRL3_BDU | LSM6DS3_CTRL3_IF_INC);
	if (rc != 0) {
		return rc;
	}
	rc = i2c_reg_update_byte_dt(
		&cfg->i2c, LSM6DS3_REG_CTRL1_XL, LSM6DS3_CTRL_ODR_MASK | LSM6DS3_CTRL_XL_FS_MASK,
		((active ? data->accel_odr : 0U) << 4) | (data->accel_fs_reg << 2));
	if (rc != 0) {
		return rc;
	}
	rc = i2c_reg_update_byte_dt(&cfg->i2c, LSM6DS3_REG_CTRL2_G,
				    LSM6DS3_CTRL_ODR_MASK | LSM6DS3_CTRL_G_FS_MASK,
				    ((active ? data->gyro_odr : 0U) << 4) | data->gyro_fs_reg);
	if (rc == 0) {
		data->active = active;
	}
	return rc;
}

static void lsm6ds3_vector_parse(int16_t out[LSM6DS3_AXIS_COUNT], const uint8_t *buf)
{
	for (size_t i = 0U; i < LSM6DS3_AXIS_COUNT; i++) {
		out[i] = (int16_t)sys_get_le16(&buf[i * 2U]);
	}
}

static int lsm6ds3_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	struct lsm6ds3_family_data *data = dev->data;
	uint8_t buf[LSM6DS3_ALL_SAMPLE_BYTES];
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (chan) {
	case SENSOR_CHAN_ALL:
		rc = i2c_burst_read_dt(&cfg->i2c, LSM6DS3_REG_OUT_TEMP, buf, sizeof(buf));
		if (rc == 0) {
			data->temperature = (int16_t)sys_get_le16(buf);
			lsm6ds3_vector_parse(data->gyro, &buf[2]);
			lsm6ds3_vector_parse(data->accel, &buf[8]);
		}
		break;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		rc = i2c_burst_read_dt(&cfg->i2c, LSM6DS3_REG_OUT_XL, buf,
				       LSM6DS3_VECTOR_SAMPLE_BYTES);
		if (rc == 0) {
			lsm6ds3_vector_parse(data->accel, buf);
		}
		break;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
	case SENSOR_CHAN_GYRO_XYZ:
		rc = i2c_burst_read_dt(&cfg->i2c, LSM6DS3_REG_OUT_G, buf,
				       LSM6DS3_VECTOR_SAMPLE_BYTES);
		if (rc == 0) {
			lsm6ds3_vector_parse(data->gyro, buf);
		}
		break;
	case SENSOR_CHAN_DIE_TEMP:
		rc = i2c_burst_read_dt(&cfg->i2c, LSM6DS3_REG_OUT_TEMP, buf, 2U);
		if (rc == 0) {
			data->temperature = (int16_t)sys_get_le16(buf);
		}
		break;
	default:
		rc = -ENOTSUP;
		break;
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static void lsm6ds3_accel_convert(int16_t raw, uint16_t sensitivity_ug, struct sensor_value *value)
{
	sensor_ug_to_ms2((int32_t)raw * sensitivity_ug, value);
}

static void lsm6ds3_gyro_convert(int16_t raw, uint32_t sensitivity_udps, struct sensor_value *value)
{
	sensor_10udegrees_to_rad((int32_t)(((int64_t)raw * sensitivity_udps) / 10LL), value);
}

static int lsm6ds3_temperature_convert(enum lsm6ds3_variant variant, int16_t raw,
				       struct sensor_value *value)
{
	/* 0x69 devices use 16 LSB/deg C; 0x6a devices use 256 LSB/deg C. */
	const int32_t sensitivity = variant == LSM6DS3_VARIANT_TR ? 16 : 256;
	const int64_t temperature_micro_c =
		25000000LL + ((int64_t)raw * 1000000LL) / sensitivity;

	return sensor_value_from_micro(value, temperature_micro_c);
}

static int lsm6ds3_axis_select(enum sensor_channel chan, enum sensor_channel x,
			       enum sensor_channel y, enum sensor_channel z,
			       enum sensor_channel xyz, size_t *first, size_t *count)
{
	if (chan == x) {
		*first = 0U;
		*count = 1U;
	} else if (chan == y) {
		*first = 1U;
		*count = 1U;
	} else if (chan == z) {
		*first = 2U;
		*count = 1U;
	} else if (chan == xyz) {
		*first = 0U;
		*count = LSM6DS3_AXIS_COUNT;
	} else {
		return -ENOTSUP;
	}
	return 0;
}

static int lsm6ds3_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *value)
{
	struct lsm6ds3_family_data *data = dev->data;
	size_t first;
	size_t count;
	int rc;

	if (value == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (chan >= SENSOR_CHAN_ACCEL_X && chan <= SENSOR_CHAN_ACCEL_XYZ) {
		rc = lsm6ds3_axis_select(chan, SENSOR_CHAN_ACCEL_X, SENSOR_CHAN_ACCEL_Y,
					 SENSOR_CHAN_ACCEL_Z, SENSOR_CHAN_ACCEL_XYZ, &first,
					 &count);
		for (size_t i = 0U; rc == 0 && i < count; i++) {
			lsm6ds3_accel_convert(data->accel[first + i], data->accel_sensitivity_ug,
					      &value[i]);
		}
	} else if (chan >= SENSOR_CHAN_GYRO_X && chan <= SENSOR_CHAN_GYRO_XYZ) {
		rc = lsm6ds3_axis_select(chan, SENSOR_CHAN_GYRO_X, SENSOR_CHAN_GYRO_Y,
					 SENSOR_CHAN_GYRO_Z, SENSOR_CHAN_GYRO_XYZ, &first, &count);
		for (size_t i = 0U; rc == 0 && i < count; i++) {
			lsm6ds3_gyro_convert(data->gyro[first + i], data->gyro_sensitivity_udps,
					     &value[i]);
		}
	} else if (chan == SENSOR_CHAN_DIE_TEMP) {
		rc = lsm6ds3_temperature_convert(data->variant, data->temperature, value);
	} else {
		rc = -ENOTSUP;
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static bool lsm6ds3_accel_channel(enum sensor_channel chan)
{
	return chan >= SENSOR_CHAN_ACCEL_X && chan <= SENSOR_CHAN_ACCEL_XYZ;
}

static bool lsm6ds3_gyro_channel(enum sensor_channel chan)
{
	return chan >= SENSOR_CHAN_GYRO_X && chan <= SENSOR_CHAN_GYRO_XYZ;
}

static int lsm6ds3_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, const struct sensor_value *value)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	struct lsm6ds3_family_data *data = dev->data;
	uint8_t odr;
	uint8_t reg;
	int rc;

	if (attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return -ENOTSUP;
	}
	rc = lsm6ds3_odr_from_value(value, &odr);
	if (rc != 0) {
		return rc;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (lsm6ds3_accel_channel(chan)) {
		reg = LSM6DS3_REG_CTRL1_XL;
	} else if (lsm6ds3_gyro_channel(chan)) {
		reg = LSM6DS3_REG_CTRL2_G;
	} else {
		rc = -ENOTSUP;
		goto out;
	}

	rc = data->active ? i2c_reg_update_byte_dt(&cfg->i2c, reg, LSM6DS3_CTRL_ODR_MASK,
						     odr << 4)
			  : 0;
	if (rc == 0) {
		if (lsm6ds3_accel_channel(chan)) {
			data->accel_odr = odr;
		} else {
			data->gyro_odr = odr;
		}
	}

out:
	k_mutex_unlock(&data->lock);
	return rc;
}

static int lsm6ds3_attr_get(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, struct sensor_value *value)
{
	struct lsm6ds3_family_data *data = dev->data;
	int rc;

	if (value == NULL) {
		return -EINVAL;
	}
	if (attr != SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (lsm6ds3_accel_channel(chan)) {
		rc = lsm6ds3_odr_to_value(data->accel_odr, value);
	} else if (lsm6ds3_gyro_channel(chan)) {
		rc = lsm6ds3_odr_to_value(data->gyro_odr, value);
	} else {
		rc = -ENOTSUP;
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static DEVICE_API(sensor, lsm6ds3_api) = {
	.sample_fetch = lsm6ds3_sample_fetch,
	.channel_get = lsm6ds3_channel_get,
	.attr_set = lsm6ds3_attr_set,
	.attr_get = lsm6ds3_attr_get,
};

static int lsm6ds3_turn_on(const struct device *dev)
{
	int rc;

	/* Wait for register access after the shared peripheral rail turns on. */
	k_msleep(LSM6DS3_BOOT_WAIT_MS);
	rc = lsm6ds3_check_id(dev);

	return rc != 0 ? rc : lsm6ds3_reset(dev);
}

#ifdef CONFIG_PM_DEVICE
static int lsm6ds3_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct lsm6ds3_family_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		rc = lsm6ds3_turn_on(dev);
		break;
	case PM_DEVICE_ACTION_RESUME:
		rc = lsm6ds3_check_id(dev);
		if (rc == 0) {
			rc = lsm6ds3_configure(dev, true);
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		rc = lsm6ds3_configure(dev, false);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		rc = 0;
		break;
	default:
		rc = -ENOTSUP;
		break;
	}
	k_mutex_unlock(&data->lock);
	return rc;
}
#endif

static int lsm6ds3_init(const struct device *dev)
{
	const struct lsm6ds3_family_config *cfg = dev->config;
	struct lsm6ds3_family_data *data = dev->data;
	int rc;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->i2c.bus);
		return -ENODEV;
	}
	k_mutex_init(&data->lock);
	data->accel_odr = cfg->accel_odr;
	data->gyro_odr = cfg->gyro_odr;
	data->active = false;
	rc = lsm6ds3_accel_fs_get(cfg->accel_fs, &data->accel_fs_reg, &data->accel_sensitivity_ug);
	if (rc == 0) {
		rc = lsm6ds3_gyro_fs_get(cfg->gyro_fs, &data->gyro_fs_reg,
					 &data->gyro_sensitivity_udps);
	}
	if (rc != 0) {
		LOG_ERR("%s: invalid full-scale configuration", dev->name);
		return rc;
	}

#ifdef CONFIG_PM_DEVICE
	rc = pm_device_driver_init(dev, lsm6ds3_pm_action);
#else
	rc = lsm6ds3_turn_on(dev);
	if (rc == 0) {
		rc = lsm6ds3_configure(dev, true);
	}
#endif
	if (rc != 0) {
		LOG_ERR("%s: initialization failed (%d)", dev->name, rc);
		return rc;
	}

	LOG_INF("%s ready: variant=%s accel_odr=%u gyro_odr=%u", dev->name,
		lsm6ds3_variant_name(data->variant), data->accel_odr, data->gyro_odr);
	return 0;
}

#define LSM6DS3_DEFINE(inst)                                                                       \
	static struct lsm6ds3_family_data lsm6ds3_data_##inst;                                     \
	static const struct lsm6ds3_family_config lsm6ds3_config_##inst = {                        \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.accel_odr = CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_ODR,                                 \
		.gyro_odr = CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_ODR,                                   \
		.accel_fs = CONFIG_FOBE_LSM6DS3_FAMILY_ACCEL_FS,                                   \
		.gyro_fs = CONFIG_FOBE_LSM6DS3_FAMILY_GYRO_FS,                                     \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, lsm6ds3_pm_action);                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, lsm6ds3_init, PM_DEVICE_DT_INST_GET(inst),              \
				     &lsm6ds3_data_##inst, &lsm6ds3_config_##inst, POST_KERNEL,    \
				     CONFIG_SENSOR_INIT_PRIORITY, &lsm6ds3_api)

DT_INST_FOREACH_STATUS_OKAY(LSM6DS3_DEFINE)
