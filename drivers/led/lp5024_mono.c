/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/led.h>
#include <zephyr/dt-bindings/led/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(lp5024_mono, CONFIG_LED_LOG_LEVEL);

#define DT_DRV_COMPAT fobe_lp5024_mono

#define LP5024_MONO_OUTPUT_COUNT 24U
#define LP5024_MONO_BRIGHTNESS_CH_COUNT 8U
#define LP5024_MONO_COLOR_MAX 0xFFU

#define LP5024_MONO_DEVICE_CONFIG0 0x00U
#define LP5024_MONO_DEVICE_CONFIG1 0x01U
#define LP5024_MONO_BANK_BRIGHTNESS 0x03U
#define LP5024_MONO_LED_BRIGHTNESS_BASE 0x07U
#define LP5024_MONO_OUTPUT_COLOR_BASE 0x0FU
#define LP5024_MONO_RESET_REG 0x27U

#define LP5024_MONO_RESET_SW 0xFFU
#define LP5024_MONO_CHIP_EN BIT(6)
#define LP5024_MONO_CFG1_BASE (BIT(2) | BIT(3) | BIT(4))
#define LP5024_MONO_CFG1_MAX_CURRENT_OPT BIT(1)
#define LP5024_MONO_CFG1_LOG_SCALE_EN BIT(5)

#define LP5024_MONO_DISABLE_DELAY_US 3
#define LP5024_MONO_ENABLE_DELAY_US 500

struct lp5024_mono_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec enable_gpio;
	const struct led_info *leds_info;
	const uint8_t *outputs;
	uint8_t num_leds;
	bool log_scale_en;
	bool max_curr_opt;
};

struct lp5024_mono_data {
	struct k_mutex lock;
	uint8_t cached_levels[LP5024_MONO_OUTPUT_COUNT];
	bool powered;
};

static const uint8_t lp5024_mono_color_mapping[] = { LED_COLOR_ID_WHITE };

static int lp5024_mono_write_reg(const struct i2c_dt_spec *bus,
				 uint8_t reg,
				 uint8_t value)
{
	uint8_t buf[2] = { reg, value };

	return i2c_write_dt(bus, buf, sizeof(buf));
}

static int lp5024_mono_hw_enable(const struct device *dev, bool enable)
{
	const struct lp5024_mono_config *config = dev->config;
	int rc;

	if (config->enable_gpio.port == NULL) {
		return 0;
	}

	rc = gpio_pin_set_dt(&config->enable_gpio, enable);
	if (rc < 0) {
		LOG_ERR("%s: failed to set enable gpio", dev->name);
		return rc;
	}

	k_usleep(enable ? LP5024_MONO_ENABLE_DELAY_US : LP5024_MONO_DISABLE_DELAY_US);
	return 0;
}

static int lp5024_mono_prepare(const struct device *dev)
{
	const struct lp5024_mono_config *config = dev->config;
	uint8_t cfg1 = LP5024_MONO_CFG1_BASE;
	int rc;

	if (config->max_curr_opt) {
		cfg1 |= LP5024_MONO_CFG1_MAX_CURRENT_OPT;
	}

	if (config->log_scale_en) {
		cfg1 |= LP5024_MONO_CFG1_LOG_SCALE_EN;
	}

	rc = lp5024_mono_write_reg(&config->bus,
				   LP5024_MONO_RESET_REG,
				   LP5024_MONO_RESET_SW);
	if (rc < 0) {
		return rc;
	}

	rc = lp5024_mono_write_reg(&config->bus,
				   LP5024_MONO_DEVICE_CONFIG1,
				   cfg1);
	if (rc < 0) {
		return rc;
	}

	rc = lp5024_mono_write_reg(&config->bus,
				   LP5024_MONO_DEVICE_CONFIG0,
				   LP5024_MONO_CHIP_EN);
	if (rc < 0) {
		return rc;
	}

	rc = lp5024_mono_write_reg(&config->bus,
				   LP5024_MONO_BANK_BRIGHTNESS,
				   0x00U);
	if (rc < 0) {
		return rc;
	}

	for (uint8_t chan = 0; chan < LP5024_MONO_BRIGHTNESS_CH_COUNT; chan++) {
		rc = lp5024_mono_write_reg(&config->bus,
					   LP5024_MONO_LED_BRIGHTNESS_BASE + chan,
					   LP5024_MONO_COLOR_MAX);
		if (rc < 0) {
			return rc;
		}
	}

	for (uint8_t output = 0; output < LP5024_MONO_OUTPUT_COUNT; output++) {
		rc = lp5024_mono_write_reg(&config->bus,
					   LP5024_MONO_OUTPUT_COLOR_BASE + output,
					   0x00U);
		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}

static int lp5024_mono_apply_output_level(const struct device *dev,
					  uint32_t led,
					  uint8_t level)
{
	const struct lp5024_mono_config *config = dev->config;

	if (led >= config->num_leds) {
		return -EINVAL;
	}

	return lp5024_mono_write_reg(&config->bus,
				     LP5024_MONO_OUTPUT_COLOR_BASE + config->outputs[led],
				     level);
}

static int lp5024_mono_get_info(const struct device *dev,
				uint32_t led,
				const struct led_info **info)
{
	const struct lp5024_mono_config *config = dev->config;

	if (led >= config->num_leds) {
		return -EINVAL;
	}

	*info = &config->leds_info[led];
	return 0;
}

static int lp5024_mono_set_brightness(const struct device *dev,
				      uint32_t led,
				      uint8_t value)
{
	struct lp5024_mono_data *data = dev->data;
	const struct lp5024_mono_config *config = dev->config;
	uint8_t raw_value = (value * LP5024_MONO_COLOR_MAX) / LED_BRIGHTNESS_MAX;
	int rc = 0;

	if (led >= config->num_leds) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->cached_levels[led] = raw_value;
	if (data->powered) {
		rc = lp5024_mono_apply_output_level(dev, led, raw_value);
	}
	k_mutex_unlock(&data->lock);

	return rc;
}

static int lp5024_mono_restore_outputs(const struct device *dev)
{
	const struct lp5024_mono_config *config = dev->config;
	struct lp5024_mono_data *data = dev->data;
	int rc;

	for (uint8_t led = 0; led < config->num_leds; led++) {
		rc = lp5024_mono_apply_output_level(dev, led, data->cached_levels[led]);
		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}

static int lp5024_mono_power_on_locked(const struct device *dev)
{
	struct lp5024_mono_data *data = dev->data;
	int rc;

	rc = lp5024_mono_hw_enable(dev, true);
	if (rc < 0) {
		return rc;
	}

	rc = lp5024_mono_prepare(dev);
	if (rc < 0) {
		(void)lp5024_mono_hw_enable(dev, false);
		return rc;
	}

	rc = lp5024_mono_restore_outputs(dev);
	if (rc < 0) {
		(void)lp5024_mono_hw_enable(dev, false);
		return rc;
	}

	data->powered = true;
	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int lp5024_mono_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct lp5024_mono_data *data = dev->data;
	int rc = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
	case PM_DEVICE_ACTION_TURN_ON:
		if (!data->powered) {
			rc = lp5024_mono_power_on_locked(dev);
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_TURN_OFF:
		if (data->powered) {
			rc = lp5024_mono_hw_enable(dev, false);
			if (rc == 0) {
				data->powered = false;
			}
		}
		break;
	default:
		rc = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return rc;
}
#endif /* CONFIG_PM_DEVICE */

static int lp5024_mono_init(const struct device *dev)
{
	const struct lp5024_mono_config *config = dev->config;
	struct lp5024_mono_data *data = dev->data;
	int rc;

	k_mutex_init(&data->lock);
	memset(data->cached_levels, 0, sizeof(data->cached_levels));
	data->powered = false;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("%s: I2C device not ready", dev->name);
		return -ENODEV;
	}

	if (config->num_leds > LP5024_MONO_OUTPUT_COUNT) {
		LOG_ERR("%s: invalid LED count %u", dev->name, config->num_leds);
		return -EINVAL;
	}

	if (config->enable_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->enable_gpio)) {
			LOG_ERR("%s: enable gpio is not ready", dev->name);
			return -ENODEV;
		}

		rc = gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (rc < 0) {
			LOG_ERR("%s: failed to configure enable gpio: %d", dev->name, rc);
			return rc;
		}
	}

	for (uint8_t led = 0; led < config->num_leds; led++) {
		if (config->outputs[led] >= LP5024_MONO_OUTPUT_COUNT) {
			LOG_ERR("%s: led[%u] output index %u out of range",
				dev->name, led, config->outputs[led]);
			return -EINVAL;
		}

		for (uint8_t other = led + 1; other < config->num_leds; other++) {
			if (config->outputs[led] == config->outputs[other]) {
				LOG_ERR("%s: duplicate output index %u",
					dev->name, config->outputs[led]);
				return -EINVAL;
			}
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = lp5024_mono_power_on_locked(dev);
	k_mutex_unlock(&data->lock);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static DEVICE_API(led, lp5024_mono_api) = {
	.get_info = lp5024_mono_get_info,
	.set_brightness = lp5024_mono_set_brightness,
};

#define LP5024_MONO_OUTPUT_ASSERT(node_id) \
	BUILD_ASSERT((DT_PROP(node_id, index) >= 0) && \
		     (DT_PROP(node_id, index) < LP5024_MONO_OUTPUT_COUNT), \
		     "LP5024 mono child index must be in range 0..23");

#define LP5024_MONO_LED_INFO(node_id) \
	{ \
		.label = DT_PROP(node_id, label), \
		.index = DT_PROP(node_id, index), \
		.num_colors = 1U, \
		.color_mapping = lp5024_mono_color_mapping, \
	},

#define LP5024_MONO_OUTPUT(node_id) DT_PROP(node_id, index),

#define LP5024_MONO_DEVICE(inst) \
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, LP5024_MONO_OUTPUT_ASSERT) \
	static const struct led_info lp5024_mono_leds_info_##inst[] = { \
		DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, LP5024_MONO_LED_INFO) \
	}; \
	static const uint8_t lp5024_mono_outputs_##inst[] = { \
		DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, LP5024_MONO_OUTPUT) \
	}; \
	BUILD_ASSERT(ARRAY_SIZE(lp5024_mono_leds_info_##inst) == \
		     ARRAY_SIZE(lp5024_mono_outputs_##inst), \
		     "LP5024 mono LED info/output tables must stay aligned"); \
	static const struct lp5024_mono_config lp5024_mono_config_##inst = { \
		.bus = I2C_DT_SPEC_INST_GET(inst), \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}), \
		.leds_info = lp5024_mono_leds_info_##inst, \
		.outputs = lp5024_mono_outputs_##inst, \
		.num_leds = ARRAY_SIZE(lp5024_mono_outputs_##inst), \
		.log_scale_en = DT_INST_PROP(inst, log_scale_en), \
		.max_curr_opt = DT_INST_PROP(inst, max_curr_opt), \
	}; \
	static struct lp5024_mono_data lp5024_mono_data_##inst; \
	PM_DEVICE_DT_INST_DEFINE(inst, lp5024_mono_pm_action); \
	DEVICE_DT_INST_DEFINE(inst, \
			      lp5024_mono_init, \
			      PM_DEVICE_DT_INST_GET(inst), \
			      &lp5024_mono_data_##inst, \
			      &lp5024_mono_config_##inst, \
			      POST_KERNEL, \
			      CONFIG_LED_INIT_PRIORITY, \
			      &lp5024_mono_api);

DT_INST_FOREACH_STATUS_OKAY(LP5024_MONO_DEVICE)
