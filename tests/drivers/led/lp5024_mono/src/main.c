/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#include <string.h>

#define LP_NODE DT_NODELABEL(lp5024)
#define LP_DUP_NODE DT_NODELABEL(lp5024_dup)
#define GPIO_NODE DT_NODELABEL(test_gpio)

#define LP5024_OUTPUT_COLOR_BASE 0x0fU

struct lp5024_emul_data {
	uint8_t regs[0x28];
	uint16_t write_count;
};

static struct lp5024_emul_data lp_emul_data;
static struct lp5024_emul_data lp_dup_emul_data;

static const struct device *const lp_dev = DEVICE_DT_GET(LP_NODE);
static const struct device *const lp_dup_dev = DEVICE_DT_GET(LP_DUP_NODE);
static const struct device *const gpio_dev = DEVICE_DT_GET(GPIO_NODE);

static int lp5024_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				int addr)
{
	struct lp5024_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);

	if (num_msgs != 1 || (msgs[0].flags & I2C_MSG_READ) != 0 || msgs[0].len != 2U) {
		return -EIO;
	}

	reg = msgs[0].buf[0];
	if (reg >= ARRAY_SIZE(data->regs)) {
		return -EIO;
	}

	data->regs[reg] = msgs[0].buf[1];
	data->write_count++;
	return 0;
}

static int lp5024_emul_init(const struct emul *target, const struct device *parent)
{
	struct lp5024_emul_data *data = target->data;

	ARG_UNUSED(parent);

	memset(data, 0, sizeof(*data));
	return 0;
}

static const struct i2c_emul_api lp5024_emul_api = {
	.transfer = lp5024_emul_transfer,
};

EMUL_DT_DEFINE(LP_NODE, lp5024_emul_init, &lp_emul_data, NULL, &lp5024_emul_api, NULL);
EMUL_DT_DEFINE(LP_DUP_NODE, lp5024_emul_init, &lp_dup_emul_data, NULL, &lp5024_emul_api, NULL);

static void *lp5024_setup(void)
{
	zassert_true(device_is_ready(lp_dev));
	zassert_false(device_is_ready(lp_dup_dev), "duplicate output device should fail init");
	zassert_true(device_is_ready(gpio_dev));

	return NULL;
}

ZTEST(lp5024_mono, test_init_sequence_and_brightness_mapping)
{
	zassert_true(lp_emul_data.write_count > 0);
	zassert_ok(led_set_brightness(lp_dev, 0, 50));
	zassert_equal(lp_emul_data.regs[LP5024_OUTPUT_COLOR_BASE + 2], 127);
	zassert_ok(led_set_brightness(lp_dev, 1, 100));
	zassert_equal(lp_emul_data.regs[LP5024_OUTPUT_COLOR_BASE + 5], 255);
	zassert_equal(led_set_brightness(lp_dev, 2, 1), -EINVAL);
}

ZTEST(lp5024_mono, test_pm_resume_restores_cached_brightness)
{
	uint16_t writes;

	zassert_ok(led_set_brightness(lp_dev, 0, 25));
	zassert_ok(pm_device_action_run(lp_dev, PM_DEVICE_ACTION_SUSPEND));
	writes = lp_emul_data.write_count;
	zassert_ok(led_set_brightness(lp_dev, 0, 75));
	zassert_equal(lp_emul_data.write_count, writes);
	zassert_ok(pm_device_action_run(lp_dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(lp_emul_data.regs[LP5024_OUTPUT_COLOR_BASE + 2], 191);
}

ZTEST(lp5024_mono, test_enable_gpio_tracks_power_state)
{
	zassert_ok(pm_device_action_run(lp_dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(gpio_emul_output_get(gpio_dev, 0), 0);
	zassert_ok(pm_device_action_run(lp_dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(gpio_emul_output_get(gpio_dev, 0), 1);
}

ZTEST_SUITE(lp5024_mono, NULL, lp5024_setup, NULL, NULL, NULL);
