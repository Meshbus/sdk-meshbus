/*
 * Copyright (c) 2025 FoBE Projects
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tca8418

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/mfd_tca8418.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(mfd_tca8418, CONFIG_MFD_LOG_LEVEL);

struct mfd_tca8418_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
	struct gpio_dt_spec reset_gpio;
};

struct mfd_tca8418_data {
	struct k_work work;
	struct gpio_callback int_gpio_cb;
	struct k_mutex callback_lock;
	sys_slist_t callback_list;
	sys_slist_t power_callback_list;
};

static int mfd_tca8418_irq_set_enabled(const struct device *dev, bool enabled)
{
	const struct mfd_tca8418_config *config = dev->config;

	if (config->int_gpio.port == NULL) {
		return 0;
	}

	int ret = gpio_pin_interrupt_configure_dt(&config->int_gpio,
						  enabled ? GPIO_INT_EDGE_TO_ACTIVE :
							    GPIO_INT_DISABLE);
	if (ret < 0) {
		LOG_ERR("Failed to %s interrupt: %d", enabled ? "enable" : "disable", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int mfd_tca8418_configure_powered_pins(const struct device *dev, bool powered)
{
	const struct mfd_tca8418_config *config = dev->config;
	int ret;

	if (config->reset_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->reset_gpio,
					    powered ? GPIO_OUTPUT_INACTIVE : GPIO_DISCONNECTED);
		if (ret < 0) {
			LOG_ERR("Failed to configure reset GPIO: %d", ret);
			return ret;
		}
	}

	if (config->int_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->int_gpio,
					    powered ? GPIO_INPUT : GPIO_DISCONNECTED);
		if (ret < 0) {
			LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int mfd_tca8418_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct mfd_tca8418_config *config = dev->config;
	struct mfd_tca8418_data *data = dev->data;
	struct tca8418_mfd_power_callback *cb_entry;
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_OFF:
		k_mutex_lock(&data->callback_lock, K_FOREVER);
		SYS_SLIST_FOR_EACH_CONTAINER(&data->power_callback_list, cb_entry, node) {
			cb_entry->cb(cb_entry->dev, TCA8418_MFD_POWER_EVENT_TURN_OFF);
		}
		k_mutex_unlock(&data->callback_lock);

		ret = mfd_tca8418_irq_set_enabled(dev, false);
		if (ret < 0) {
			return ret;
		}

		ret = mfd_tca8418_configure_powered_pins(dev, false);
		if (ret < 0) {
			return ret;
		}

		break;

	case PM_DEVICE_ACTION_TURN_ON:
		ret = mfd_tca8418_configure_powered_pins(dev, true);
		if (ret < 0) {
			return ret;
		}

		/* TCA8418 reset recovery time is >=120us. */
		if (config->reset_gpio.port != NULL) {
			k_busy_wait(150);
		}

		ret = mfd_tca8418_irq_set_enabled(dev, true);
		if (ret < 0) {
			return ret;
		}

		k_mutex_lock(&data->callback_lock, K_FOREVER);
		SYS_SLIST_FOR_EACH_CONTAINER(&data->power_callback_list, cb_entry, node) {
			cb_entry->cb(cb_entry->dev, TCA8418_MFD_POWER_EVENT_TURN_ON);
		}
		k_mutex_unlock(&data->callback_lock);

		break;

	case PM_DEVICE_ACTION_SUSPEND:
	case PM_DEVICE_ACTION_RESUME:
		/*
		 * Keep keypad IRQ path intact during runtime PM transitions.
		 * Actual pin isolation is only needed when the power-domain
		 * physically turns OFF/ON.
		 */
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif /* CONFIG_PM_DEVICE */

static void mfd_tca8418_int_handler(const struct device *gpio_dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct mfd_tca8418_data *data = CONTAINER_OF(cb, struct mfd_tca8418_data, int_gpio_cb);

	k_work_submit(&data->work);
}

static void mfd_tca8418_work_handler(struct k_work *work)
{
	struct mfd_tca8418_data *data = CONTAINER_OF(work, struct mfd_tca8418_data, work);
	struct tca8418_mfd_callback *cb_entry;

	k_mutex_lock(&data->callback_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER(&data->callback_list, cb_entry, node) {
		cb_entry->cb(cb_entry->dev);
	}
	k_mutex_unlock(&data->callback_lock);
}

void mfd_tca8418_register_interrupt_callback(const struct device *mfd,
					     struct tca8418_mfd_callback *callback)
{
	struct mfd_tca8418_data *data = mfd->data;

	k_mutex_lock(&data->callback_lock, K_FOREVER);
	sys_slist_append(&data->callback_list, &callback->node);
	k_mutex_unlock(&data->callback_lock);
}

void mfd_tca8418_register_power_callback(const struct device *mfd,
					 struct tca8418_mfd_power_callback *callback)
{
	struct mfd_tca8418_data *data = mfd->data;

	k_mutex_lock(&data->callback_lock, K_FOREVER);
	sys_slist_append(&data->power_callback_list, &callback->node);
	k_mutex_unlock(&data->callback_lock);
}

const struct i2c_dt_spec *mfd_tca8418_get_i2c_spec(const struct device *mfd)
{
	const struct mfd_tca8418_config *config = mfd->config;

	return &config->i2c;
}

bool mfd_tca8418_has_interrupt(const struct device *mfd)
{
	const struct mfd_tca8418_config *config = mfd->config;

	return config->int_gpio.port != NULL;
}

static int mfd_tca8418_init(const struct device *dev)
{
	const struct mfd_tca8418_config *config = dev->config;
	struct mfd_tca8418_data *data = dev->data;
	int ret;

	/* Check if I2C bus is ready */
	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus %s is not ready", config->i2c.bus->name);
		return -ENODEV;
	}

	/* Initialize callback list */
	k_mutex_init(&data->callback_lock);
	sys_slist_init(&data->callback_list);
	sys_slist_init(&data->power_callback_list);

	/* Initialize work item */
	k_work_init(&data->work, mfd_tca8418_work_handler);

	/* Optional: perform hardware reset if reset GPIO is configured */
	if (config->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure reset GPIO: %d", ret);
			return ret;
		}

		/* Hold reset low for at least 120us (per datasheet) */
		k_busy_wait(150);

		/* Release reset */
		ret = gpio_pin_set_dt(&config->reset_gpio, 0);
		if (ret < 0) {
			LOG_ERR("Failed to release reset: %d", ret);
			return ret;
		}

		/* Wait for device to be ready after reset (120us recovery) */
		k_busy_wait(150);

		LOG_DBG("Hardware reset completed");
	}

	/* Setup interrupt GPIO if configured */
	if (config->int_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			LOG_ERR("Interrupt GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
			return ret;
		}

		gpio_init_callback(&data->int_gpio_cb, mfd_tca8418_int_handler,
				   BIT(config->int_gpio.pin));

		ret = gpio_add_callback_dt(&config->int_gpio, &data->int_gpio_cb);
		if (ret < 0) {
			LOG_ERR("Failed to add GPIO callback: %d", ret);
			return ret;
		}

		ret = mfd_tca8418_irq_set_enabled(dev, true);
		if (ret < 0) {
			return ret;
		}

		LOG_DBG("Interrupt mode configured");
	}

	LOG_INF("TCA8418 MFD initialized");

	return 0;
}

#define MFD_TCA8418_DEFINE(inst)                                                                   \
	static struct mfd_tca8418_data mfd_tca8418_data_##inst;                                    \
	PM_DEVICE_DT_INST_DEFINE(inst, mfd_tca8418_pm_action);                                     \
                                                                                                   \
	static const struct mfd_tca8418_config mfd_tca8418_config_##inst = {                       \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),                        \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, mfd_tca8418_init, PM_DEVICE_DT_INST_GET(inst),               \
			      &mfd_tca8418_data_##inst, &mfd_tca8418_config_##inst, POST_KERNEL,   \
			      CONFIG_MFD_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_TCA8418_DEFINE)
