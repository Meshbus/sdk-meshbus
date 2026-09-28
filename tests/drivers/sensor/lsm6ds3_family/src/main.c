/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define LSM6DS3_TR_NODE   DT_NODELABEL(lsm6ds3_tr)
#define LSM6DS3_TR_C_NODE DT_NODELABEL(lsm6ds3_tr_c)

#define LSM6DS3_REG_WHO_AM_I 0x0f
#define LSM6DS3_REG_CTRL3_C  0x12
#define LSM6DS3_REG_OUT_TEMP 0x20
#define LSM6DS3_CTRL3_SW_RESET BIT(0)
#define LSM6DS3_ID_TR          0x69
#define LSM6DS3_ID_TR_C        0x6a

struct lsm6ds3_emul_config {
	uint8_t chip_id;
};

struct lsm6ds3_emul_data {
	uint8_t regs[UINT8_MAX + 1U];
};

static const struct device *const tr_dev = DEVICE_DT_GET(LSM6DS3_TR_NODE);
static const struct device *const tr_c_dev = DEVICE_DT_GET(LSM6DS3_TR_C_NODE);

static int lsm6ds3_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
				 int num_msgs, int addr)
{
	struct lsm6ds3_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);

	if (num_msgs == 2 && msgs[0].len == 1U &&
	    (msgs[0].flags & I2C_MSG_READ) == 0U &&
	    (msgs[1].flags & I2C_MSG_READ) != 0U) {
		reg = msgs[0].buf[0];
		for (size_t i = 0U; i < msgs[1].len; i++) {
			msgs[1].buf[i] = data->regs[(uint8_t)(reg + i)];
		}
		return 0;
	}

	if (num_msgs == 1 && (msgs[0].flags & I2C_MSG_READ) == 0U &&
	    msgs[0].len >= 2U) {
		reg = msgs[0].buf[0];
		for (size_t i = 1U; i < msgs[0].len; i++) {
			data->regs[(uint8_t)(reg + i - 1U)] = msgs[0].buf[i];
		}
		/* Hardware clears SW_RESET when the reset has completed. */
		data->regs[LSM6DS3_REG_CTRL3_C] &= (uint8_t)~LSM6DS3_CTRL3_SW_RESET;
		return 0;
	}

	return -EIO;
}

static int lsm6ds3_emul_init(const struct emul *target, const struct device *parent)
{
	const struct lsm6ds3_emul_config *cfg = target->cfg;
	struct lsm6ds3_emul_data *data = target->data;

	ARG_UNUSED(parent);
	memset(data, 0, sizeof(*data));
	data->regs[LSM6DS3_REG_WHO_AM_I] = cfg->chip_id;
	return 0;
}

static const struct i2c_emul_api lsm6ds3_emul_api = {
	.transfer = lsm6ds3_emul_transfer,
};

#define LSM6DS3_EMUL_DEFINE(node_id, id)                                            \
	static struct lsm6ds3_emul_data node_id##_emul_data;                         \
	static const struct lsm6ds3_emul_config node_id##_emul_config = {             \
		.chip_id = (id),                                                        \
	};                                                                              \
	EMUL_DT_DEFINE(node_id, lsm6ds3_emul_init, &node_id##_emul_data,              \
		       &node_id##_emul_config, &lsm6ds3_emul_api, NULL)

LSM6DS3_EMUL_DEFINE(LSM6DS3_TR_NODE, LSM6DS3_ID_TR);
LSM6DS3_EMUL_DEFINE(LSM6DS3_TR_C_NODE, LSM6DS3_ID_TR_C);

static void lsm6ds3_temperature_raw_set(const struct emul *target, int16_t raw)
{
	struct lsm6ds3_emul_data *data = target->data;

	sys_put_le16((uint16_t)raw, &data->regs[LSM6DS3_REG_OUT_TEMP]);
}

static void *lsm6ds3_setup(void)
{
	zassert_true(device_is_ready(tr_dev));
	zassert_true(device_is_ready(tr_c_dev));
	zassert_ok(pm_device_runtime_enable(tr_dev));
	zassert_ok(pm_device_runtime_enable(tr_c_dev));
	zassert_ok(pm_device_runtime_get(tr_dev));
	zassert_ok(pm_device_runtime_get(tr_c_dev));
	return NULL;
}

static void lsm6ds3_after(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(pm_device_runtime_put(tr_dev));
	zassert_ok(pm_device_runtime_put(tr_c_dev));
}

ZTEST(lsm6ds3_family, test_temperature_sensitivity_follows_detected_variant)
{
	const struct emul *tr_emul = EMUL_DT_GET(LSM6DS3_TR_NODE);
	const struct emul *tr_c_emul = EMUL_DT_GET(LSM6DS3_TR_C_NODE);
	struct sensor_value value;

	/* The same raw value is 1 deg C on 0x69 and 1/16 deg C on 0x6a. */
	lsm6ds3_temperature_raw_set(tr_emul, 16);
	lsm6ds3_temperature_raw_set(tr_c_emul, 16);

	zassert_ok(sensor_sample_fetch_chan(tr_dev, SENSOR_CHAN_DIE_TEMP));
	zassert_ok(sensor_channel_get(tr_dev, SENSOR_CHAN_DIE_TEMP, &value));
	zassert_equal(sensor_value_to_micro(&value), 26000000LL);

	zassert_ok(sensor_sample_fetch_chan(tr_c_dev, SENSOR_CHAN_DIE_TEMP));
	zassert_ok(sensor_channel_get(tr_c_dev, SENSOR_CHAN_DIE_TEMP, &value));
	zassert_equal(sensor_value_to_micro(&value), 25062500LL);
}

ZTEST_SUITE(lsm6ds3_family, NULL, lsm6ds3_setup, NULL, NULL, lsm6ds3_after);
