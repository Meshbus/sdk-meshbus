/*
 * SPDX-FileCopyrightText: FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT qst_qma6100p

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(qma6100p, CONFIG_SENSOR_LOG_LEVEL);

#define QMA6100P_REG_CHIP_ID 0x00
#define QMA6100P_REG_DX_L    0x01
#define QMA6100P_REG_FSR     0x0f
#define QMA6100P_REG_PM      0x11
#define QMA6100P_REG_SR      0x36

#define QMA6100P_CHIP_ID 0x90
#define QMA6100P_PM_ACTIVE BIT(7)
#define QMA6100P_SR_RESET  0xb6
#define QMA6100P_FSR_MASK  GENMASK(3, 0)

#define QMA6100P_SAMPLE_BYTES 6U
#define QMA6100P_AXIS_COUNT   3U

struct qma6100p_config {
	struct i2c_dt_spec i2c;
	uint32_t range_g;
};

struct qma6100p_data {
	struct k_mutex lock;
	int16_t raw[QMA6100P_AXIS_COUNT];
	uint8_t range_reg;
	uint16_t sensitivity_ug;
};

static int qma6100p_range_to_reg(uint32_t range_g, uint8_t *range_reg, uint16_t *sensitivity_ug)
{
	switch (range_g) {
	case 2:
		*range_reg = 0x01;
		*sensitivity_ug = 244;
		return 0;
	case 4:
		*range_reg = 0x02;
		*sensitivity_ug = 488;
		return 0;
	case 8:
		*range_reg = 0x04;
		*sensitivity_ug = 977;
		return 0;
	case 16:
		*range_reg = 0x08;
		*sensitivity_ug = 1950;
		return 0;
	case 32:
		*range_reg = 0x0f;
		*sensitivity_ug = 3910;
		return 0;
	default:
		return -EINVAL;
	}
}

static int qma6100p_check_id(const struct device *dev)
{
	const struct qma6100p_config *cfg = dev->config;
	uint8_t chip_id;
	int ret;

	ret = i2c_reg_read_byte_dt(&cfg->i2c, QMA6100P_REG_CHIP_ID, &chip_id);
	if (ret != 0) {
		LOG_ERR("%s: failed to read chip id (%d)", dev->name, ret);
		return ret;
	}
	if (chip_id != QMA6100P_CHIP_ID) {
		LOG_ERR("%s: invalid chip id 0x%02x", dev->name, chip_id);
		return -ENODEV;
	}

	return 0;
}

static int qma6100p_set_active(const struct device *dev, bool active)
{
	const struct qma6100p_config *cfg = dev->config;

	return i2c_reg_update_byte_dt(&cfg->i2c, QMA6100P_REG_PM, QMA6100P_PM_ACTIVE,
				      active ? QMA6100P_PM_ACTIVE : 0);
}

static int qma6100p_configure(const struct device *dev, bool reset)
{
	const struct qma6100p_config *cfg = dev->config;
	struct qma6100p_data *data = dev->data;
	int ret;

	ret = qma6100p_check_id(dev);
	if (ret != 0) {
		return ret;
	}

	if (reset) {
		ret = i2c_reg_write_byte_dt(&cfg->i2c, QMA6100P_REG_SR, QMA6100P_SR_RESET);
		if (ret != 0) {
			return ret;
		}
		k_msleep(2);
		ret = i2c_reg_write_byte_dt(&cfg->i2c, QMA6100P_REG_SR, 0x00);
		if (ret != 0) {
			return ret;
		}
		k_msleep(20);
	}

	ret = i2c_reg_update_byte_dt(&cfg->i2c, QMA6100P_REG_FSR, QMA6100P_FSR_MASK,
				     data->range_reg);
	if (ret != 0) {
		return ret;
	}

	ret = qma6100p_set_active(dev, true);
	if (ret != 0) {
		return ret;
	}

	k_msleep(2);
	return 0;
}

static int qma6100p_pm_get(const struct device *dev, bool *pm_acquired)
{
	int ret;

	*pm_acquired = false;

	if (!IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		return 0;
	}

	ret = pm_device_runtime_get(dev);
	if (ret == -ENOTSUP) {
		return 0;
	}
	if (ret == 0) {
		*pm_acquired = true;
	}

	return ret;
}

static int qma6100p_pm_put(const struct device *dev, bool pm_acquired)
{
	if (!pm_acquired || !IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)) {
		return 0;
	}

	return pm_device_runtime_put(dev);
}

static int qma6100p_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct qma6100p_config *cfg = dev->config;
	struct qma6100p_data *data = dev->data;
	uint8_t buf[QMA6100P_SAMPLE_BYTES];
	bool pm_acquired;
	int ret;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_ACCEL_X) &&
	    (chan != SENSOR_CHAN_ACCEL_Y) && (chan != SENSOR_CHAN_ACCEL_Z) &&
	    (chan != SENSOR_CHAN_ACCEL_XYZ)) {
		return -ENOTSUP;
	}

	ret = qma6100p_pm_get(dev, &pm_acquired);
	if (ret != 0) {
		return ret;
	}

	ret = i2c_burst_read_dt(&cfg->i2c, QMA6100P_REG_DX_L, buf, sizeof(buf));
	if (ret != 0) {
		goto out;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	for (uint8_t i = 0U; i < QMA6100P_AXIS_COUNT; i++) {
		int16_t sample = (int16_t)sys_get_le16(&buf[i * 2U]);

		data->raw[i] = sample >> 2;
	}
	k_mutex_unlock(&data->lock);

out:
	(void)qma6100p_pm_put(dev, pm_acquired);
	return ret;
}

static void qma6100p_convert(const struct qma6100p_config *cfg, int16_t raw,
			     const struct qma6100p_data *data, struct sensor_value *val)
{
	ARG_UNUSED(cfg);

	int64_t micro_ms2 = (int64_t)raw * data->sensitivity_ug * SENSOR_G;

	micro_ms2 /= 1000000LL;
	(void)sensor_value_from_micro(val, micro_ms2);
}

static int qma6100p_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct qma6100p_config *cfg = dev->config;
	struct qma6100p_data *data = dev->data;
	uint8_t first;
	uint8_t count;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		first = 0U;
		count = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		first = 1U;
		count = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		first = 2U;
		count = 1U;
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		first = 0U;
		count = QMA6100P_AXIS_COUNT;
		break;
	default:
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	for (uint8_t i = 0U; i < count; i++) {
		qma6100p_convert(cfg, data->raw[first + i], data, &val[i]);
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static DEVICE_API(sensor, qma6100p_api) = {
	.sample_fetch = qma6100p_sample_fetch,
	.channel_get = qma6100p_channel_get,
};

#ifdef CONFIG_PM_DEVICE
static int qma6100p_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct qma6100p_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		ret = qma6100p_configure(dev, false);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = qma6100p_set_active(dev, false);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
	case PM_DEVICE_ACTION_TURN_OFF:
		ret = 0;
		break;
	default:
		ret = -ENOTSUP;
		break;
	}
	k_mutex_unlock(&data->lock);

	return ret;
}
#endif

static int qma6100p_init(const struct device *dev)
{
	const struct qma6100p_config *cfg = dev->config;
	struct qma6100p_data *data = dev->data;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->i2c.bus);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	ret = qma6100p_range_to_reg(cfg->range_g, &data->range_reg, &data->sensitivity_ug);
	if (ret != 0) {
		return ret;
	}

	ret = qma6100p_configure(dev, true);
	if (ret != 0) {
		return ret;
	}

	LOG_INF("%s ready: range_g=%u range_reg=0x%02x sensitivity=%u ug/LSB", dev->name,
		cfg->range_g, data->range_reg, data->sensitivity_ug);

#ifdef CONFIG_PM_DEVICE
	return pm_device_driver_init(dev, qma6100p_pm_action);
#else
	return 0;
#endif
}

#define QMA6100P_DEFINE(inst)                                                                     \
	static struct qma6100p_data qma6100p_data_##inst;                                        \
	static const struct qma6100p_config qma6100p_config_##inst = {                           \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                               \
		.range_g = DT_INST_PROP(inst, range_g),                                          \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, qma6100p_pm_action);                                      \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, qma6100p_init, PM_DEVICE_DT_INST_GET(inst),            \
				     &qma6100p_data_##inst, &qma6100p_config_##inst,            \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &qma6100p_api);

DT_INST_FOREACH_STATUS_OKAY(QMA6100P_DEFINE)
