/*
 * Copyright (c) 2025 FoBE Projects
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tca8418_kbd

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <drivers/mfd/mfd_tca8418.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_tca8418, CONFIG_INPUT_LOG_LEVEL);

struct tca8418_kbd_config {
	const struct device *mfd;
	uint8_t row_size;
	uint8_t col_size;
	uint16_t poll_interval_ms;
};

struct tca8418_kbd_data {
	const struct device *dev;
	struct k_work work;
	struct tca8418_mfd_callback mfd_callback;
	struct tca8418_mfd_power_callback mfd_power_callback;
	struct k_timer poll_timer;
	bool use_polling;
};

static int tca8418_clear_interrupt(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	const struct i2c_dt_spec *i2c = mfd_tca8418_get_i2c_spec(config->mfd);
	uint8_t int_stat;
	int ret;

	/* Read interrupt status to see what caused the interrupt */
	ret = i2c_reg_read_byte_dt(i2c, TCA8418_REG_INT_STAT, &int_stat);
	if (ret < 0) {
		LOG_ERR("Failed to read INT_STAT: %d", ret);
		return ret;
	}

	/* Clear any pending interrupts by writing 1 to the bits */
	if (int_stat != 0) {
		ret = i2c_reg_write_byte_dt(i2c, TCA8418_REG_INT_STAT, int_stat);
		if (ret < 0) {
			LOG_ERR("Failed to clear INT_STAT: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int tca8418_process_events(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	const struct i2c_dt_spec *i2c = mfd_tca8418_get_i2c_spec(config->mfd);
	uint8_t key_lck_ec;
	uint8_t event_count;
	int ret;

	/* Read key lock and event counter register */
	ret = i2c_reg_read_byte_dt(i2c, TCA8418_REG_KEY_LCK_EC, &key_lck_ec);
	if (ret < 0) {
		LOG_ERR("Failed to read KEY_LCK_EC: %d", ret);
		return ret;
	}

	event_count = key_lck_ec & TCA8418_KEY_LCK_EC_KEC_MASK;

	LOG_DBG("Event count: %d", event_count);

	/* Process all events in the FIFO */
	for (uint8_t i = 0; i < event_count; i++) {
		uint8_t event;
		uint8_t key_code;
		bool pressed;
		uint8_t row;
		uint8_t col;

		/* Read key event from FIFO (always read KEY_EVENT_A, FIFO auto-shifts) */
		ret = i2c_reg_read_byte_dt(i2c, TCA8418_REG_KEY_EVENT_A, &event);
		if (ret < 0) {
			LOG_ERR("Failed to read KEY_EVENT_A: %d", ret);
			return ret;
		}

		/* Decode event */
		pressed = (event & TCA8418_KEY_EVENT_PRESS) != 0;
		key_code = event & TCA8418_KEY_EVENT_CODE_MASK;

		/* Key codes 1-80 are matrix keys, 97-114 are GPI events */
		if (key_code == 0) {
			/* Empty event, skip */
			continue;
		}

		if (key_code >= 1 && key_code <= 80) {
			/* Matrix key: code = row * 10 + col + 1 */
			row = (key_code - 1) / 10;
			col = (key_code - 1) % 10;

			if (row >= config->row_size || col >= config->col_size) {
				LOG_DBG("Ignoring out-of-range key: code=%d row=%d col=%d",
					key_code, row, col);
				continue;
			}

			LOG_DBG("Key event: code=%d row=%d col=%d pressed=%d",
				key_code, row, col, pressed);

			/* Report row/column as INPUT_ABS_X (col) and INPUT_ABS_Y (row) */
			input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
			input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
			input_report_key(dev, INPUT_BTN_TOUCH, pressed ? 1 : 0, true, K_FOREVER);
		} else if (key_code >= 97 && key_code <= 114) {
			/* GPI event (not currently supported, log only) */
			LOG_DBG("GPI event: code=%d pressed=%d", key_code, pressed);
		} else {
			LOG_WRN("Unknown key code: %d", key_code);
		}
	}

	/* Clear key event interrupt */
	ret = tca8418_clear_interrupt(dev);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void tca8418_kbd_work_handler(struct k_work *work)
{
	struct tca8418_kbd_data *data = CONTAINER_OF(work, struct tca8418_kbd_data, work);

	tca8418_process_events(data->dev);
}

static void tca8418_kbd_timer_handler(struct k_timer *timer)
{
	struct tca8418_kbd_data *data = CONTAINER_OF(timer, struct tca8418_kbd_data, poll_timer);

	k_work_submit(&data->work);
}

static void tca8418_kbd_mfd_callback(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	const struct i2c_dt_spec *i2c = mfd_tca8418_get_i2c_spec(config->mfd);
	struct tca8418_kbd_data *data = dev->data;
	uint8_t int_stat;
	int ret;

	/* Check if this interrupt is for keyboard events */
	ret = i2c_reg_read_byte_dt(i2c, TCA8418_REG_INT_STAT, &int_stat);
	if (ret < 0) {
		LOG_ERR("Failed to read INT_STAT: %d", ret);
		return;
	}

	if (int_stat & TCA8418_INT_K_INT) {
		k_work_submit(&data->work);
	}
}

static int tca8418_configure_matrix(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	const struct i2c_dt_spec *i2c = mfd_tca8418_get_i2c_spec(config->mfd);
	uint8_t kp_gpio1 = 0;  /* ROW0-7 */
	uint8_t kp_gpio2 = 0;  /* COL0-7 */
	uint8_t kp_gpio3 = 0;  /* COL8-9 */
	int ret;

	/* Configure rows: set bits for rows used in keypad matrix */
	for (uint8_t i = 0; i < config->row_size && i < TCA8418_MAX_ROWS; i++) {
		kp_gpio1 |= BIT(i);
	}

	/* Configure columns: set bits for columns used in keypad matrix */
	for (uint8_t i = 0; i < config->col_size && i < TCA8418_MAX_COLS; i++) {
		if (i < 8) {
			kp_gpio2 |= BIT(i);
		} else {
			kp_gpio3 |= BIT(i - 8);
		}
	}

	LOG_DBG("Configuring matrix: KP_GPIO1=0x%02x KP_GPIO2=0x%02x KP_GPIO3=0x%02x",
		kp_gpio1, kp_gpio2, kp_gpio3);

	/* Write KP_GPIO registers to configure keypad/GPIO selection */
	ret = i2c_reg_write_byte_dt(i2c, TCA8418_REG_KP_GPIO1, kp_gpio1);
	if (ret < 0) {
		LOG_ERR("Failed to write KP_GPIO1: %d", ret);
		return ret;
	}

	ret = i2c_reg_write_byte_dt(i2c, TCA8418_REG_KP_GPIO2, kp_gpio2);
	if (ret < 0) {
		LOG_ERR("Failed to write KP_GPIO2: %d", ret);
		return ret;
	}

	ret = i2c_reg_write_byte_dt(i2c, TCA8418_REG_KP_GPIO3, kp_gpio3);
	if (ret < 0) {
		LOG_ERR("Failed to write KP_GPIO3: %d", ret);
		return ret;
	}

	return 0;
}

static int tca8418_configure_runtime(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	const struct i2c_dt_spec *i2c = mfd_tca8418_get_i2c_spec(config->mfd);
	int ret;

	ret = tca8418_configure_matrix(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure matrix: %d", ret);
		return ret;
	}

	ret = tca8418_clear_interrupt(dev);
	if (ret < 0) {
		LOG_ERR("Failed to clear interrupts: %d", ret);
		return ret;
	}

	ret = i2c_reg_write_byte_dt(i2c, TCA8418_REG_CFG,
				    TCA8418_CFG_INT_CFG | TCA8418_CFG_KE_IEN);
	if (ret < 0) {
		LOG_ERR("Failed to write CFG: %d", ret);
		return ret;
	}

	return 0;
}

static void tca8418_kbd_mfd_power_callback(const struct device *dev,
					   enum tca8418_mfd_power_event event)
{
	const struct tca8418_kbd_config *config = dev->config;
	struct tca8418_kbd_data *data = dev->data;

	if (event == TCA8418_MFD_POWER_EVENT_TURN_OFF) {
		if (data->use_polling) {
			k_timer_stop(&data->poll_timer);
		}
		return;
	}

	if (event != TCA8418_MFD_POWER_EVENT_TURN_ON) {
		return;
	}

	/* Re-apply matrix/interrupt config after power-domain OFF->ON cycle. */
	if (tca8418_configure_runtime(dev) < 0) {
		LOG_ERR("Failed to restore keypad configuration after power-on");
	}

	if (data->use_polling) {
		k_timer_start(&data->poll_timer, K_MSEC(config->poll_interval_ms),
			      K_MSEC(config->poll_interval_ms));
	}
}

static int tca8418_kbd_init(const struct device *dev)
{
	const struct tca8418_kbd_config *config = dev->config;
	struct tca8418_kbd_data *data = dev->data;
	int ret;

	data->dev = dev;

	/* Check if MFD parent is ready */
	if (!device_is_ready(config->mfd)) {
		LOG_ERR("MFD parent device not ready");
		return -ENODEV;
	}

	/* Initialize work item */
	k_work_init(&data->work, tca8418_kbd_work_handler);

	/* Configure keypad matrix */
	ret = tca8418_configure_runtime(dev);
	if (ret < 0) {
		return ret;
	}

	/* Check if MFD has interrupt configured */
	/* Register callback with MFD for interrupt notification */
	data->mfd_callback.cb = tca8418_kbd_mfd_callback;
	data->mfd_callback.dev = dev;
	mfd_tca8418_register_interrupt_callback(config->mfd, &data->mfd_callback);

	data->mfd_power_callback.cb = tca8418_kbd_mfd_power_callback;
	data->mfd_power_callback.dev = dev;
	mfd_tca8418_register_power_callback(config->mfd, &data->mfd_power_callback);

	/* Also setup polling as fallback if interrupt doesn't work */
	if (config->poll_interval_ms > 0) {
		k_timer_init(&data->poll_timer, tca8418_kbd_timer_handler, NULL);
		data->use_polling = !mfd_tca8418_has_interrupt(config->mfd);
		if (data->use_polling) {
			k_timer_start(&data->poll_timer, K_MSEC(config->poll_interval_ms),
				      K_MSEC(config->poll_interval_ms));
		}
	}

	LOG_INF("TCA8418 keyboard initialized (%dx%d matrix)",
		config->row_size, config->col_size);

	return 0;
}

#define TCA8418_KBD_INIT(inst)                                                                     \
	static struct tca8418_kbd_data tca8418_kbd_data_##inst;                                    \
                                                                                                   \
	static const struct tca8418_kbd_config tca8418_kbd_config_##inst = {                       \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.row_size = DT_INST_PROP(inst, row_size),                                          \
		.col_size = DT_INST_PROP(inst, col_size),                                          \
		.poll_interval_ms = DT_INST_PROP_OR(inst, poll_interval_ms, 10),                   \
	};                                                                                         \
                                                                                                   \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, row_size), 1, 8), "row-size must be 1-8");        \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP(inst, col_size), 1, 10), "col-size must be 1-10");      \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, tca8418_kbd_init, NULL, &tca8418_kbd_data_##inst,              \
			      &tca8418_kbd_config_##inst, POST_KERNEL,                             \
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TCA8418_KBD_INIT)
