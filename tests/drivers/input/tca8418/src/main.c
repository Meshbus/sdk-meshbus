/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c_emul.h>
#include <drivers/mfd/mfd_tca8418.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TCA_INT_NODE DT_NODELABEL(tca_int)
#define TCA_POLL_NODE DT_NODELABEL(tca_poll)
#define KBD_INT_NODE DT_NODELABEL(kbd_int)
#define KBD_POLL_NODE DT_NODELABEL(kbd_poll)
#define GPIO_NODE DT_NODELABEL(test_gpio)

struct tca8418_emul_data {
	uint8_t regs[0x2f];
	uint8_t fifo[TCA8418_FIFO_SIZE];
	uint8_t fifo_len;
	uint8_t fifo_pos;
};

struct captured_input {
	const struct device *dev;
	uint8_t type;
	uint16_t code;
	int32_t value;
	bool sync;
};

static struct tca8418_emul_data tca_int_data;
static struct tca8418_emul_data tca_poll_data;
static struct captured_input captured[16];
static uint8_t captured_count;

static const struct device *const tca_int_dev = DEVICE_DT_GET(TCA_INT_NODE);
static const struct device *const tca_poll_dev = DEVICE_DT_GET(TCA_POLL_NODE);
static const struct device *const kbd_int_dev = DEVICE_DT_GET(KBD_INT_NODE);
static const struct device *const kbd_poll_dev = DEVICE_DT_GET(KBD_POLL_NODE);
static const struct device *const gpio_dev = DEVICE_DT_GET(GPIO_NODE);

static uint8_t matrix_code(uint8_t row, uint8_t col)
{
	return (row * 10U) + col + 1U;
}

static void capture_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (captured_count >= ARRAY_SIZE(captured)) {
		return;
	}

	captured[captured_count++] = (struct captured_input){
		.dev = evt->dev,
		.type = evt->type,
		.code = evt->code,
		.value = evt->value,
		.sync = evt->sync,
	};
}

INPUT_CALLBACK_DEFINE(NULL, capture_cb, NULL);

static void tca8418_emul_reset(struct tca8418_emul_data *data)
{
	memset(data, 0, sizeof(*data));
}

static void tca8418_emul_set_event(struct tca8418_emul_data *data, uint8_t event)
{
	data->fifo[0] = event;
	data->fifo_len = 1;
	data->fifo_pos = 0;
	data->regs[TCA8418_REG_KEY_LCK_EC] = 1;
	data->regs[TCA8418_REG_INT_STAT] = TCA8418_INT_K_INT;
}

static int tca8418_emul_read_reg(struct tca8418_emul_data *data, uint8_t reg, uint8_t *value)
{
	if (reg == TCA8418_REG_KEY_LCK_EC) {
		*value = data->fifo_len - data->fifo_pos;
		return 0;
	}

	if (reg == TCA8418_REG_KEY_EVENT_A) {
		if (data->fifo_pos >= data->fifo_len) {
			*value = 0;
			return 0;
		}
		*value = data->fifo[data->fifo_pos++];
		return 0;
	}

	if (reg >= ARRAY_SIZE(data->regs)) {
		return -EIO;
	}

	*value = data->regs[reg];
	return 0;
}

static int tca8418_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
				 int num_msgs, int addr)
{
	struct tca8418_emul_data *data = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);

	if (num_msgs == 2 && (msgs[0].flags & I2C_MSG_READ) == 0 &&
	    (msgs[1].flags & I2C_MSG_READ) != 0 && msgs[0].len == 1U && msgs[1].len == 1U) {
		reg = msgs[0].buf[0];
		return tca8418_emul_read_reg(data, reg, &msgs[1].buf[0]);
	}

	if (num_msgs == 1 && (msgs[0].flags & I2C_MSG_READ) == 0 && msgs[0].len == 2U) {
		reg = msgs[0].buf[0];
		if (reg >= ARRAY_SIZE(data->regs)) {
			return -EIO;
		}

		if (reg == TCA8418_REG_INT_STAT) {
			data->regs[reg] &= (uint8_t)~msgs[0].buf[1];
		} else {
			data->regs[reg] = msgs[0].buf[1];
		}
		return 0;
	}

	return -EIO;
}

static int tca8418_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);

	tca8418_emul_reset(target->data);
	return 0;
}

static const struct i2c_emul_api tca8418_emul_api = {
	.transfer = tca8418_emul_transfer,
};

EMUL_DT_DEFINE(TCA_INT_NODE, tca8418_emul_init, &tca_int_data, NULL, &tca8418_emul_api, NULL);
EMUL_DT_DEFINE(TCA_POLL_NODE, tca8418_emul_init, &tca_poll_data, NULL, &tca8418_emul_api, NULL);

static void clear_capture(void)
{
	memset(captured, 0, sizeof(captured));
	captured_count = 0;
}

static bool captured_has(const struct device *dev, uint8_t type, uint16_t code, int32_t value)
{
	for (uint8_t i = 0; i < captured_count; i++) {
		if (captured[i].dev == dev && captured[i].type == type &&
		    captured[i].code == code && captured[i].value == value) {
			return true;
		}
	}

	return false;
}

static void trigger_interrupt_event(uint8_t event)
{
	tca8418_emul_set_event(&tca_int_data, event);
	zassert_ok(gpio_emul_input_set(gpio_dev, 0, 1));
	zassert_ok(gpio_emul_input_set(gpio_dev, 0, 0));
	k_sleep(K_MSEC(10));
	zassert_ok(gpio_emul_input_set(gpio_dev, 0, 1));
}

static void *tca8418_setup(void)
{
	zassert_true(device_is_ready(tca_int_dev));
	zassert_true(device_is_ready(tca_poll_dev));
	zassert_true(device_is_ready(kbd_int_dev));
	zassert_true(device_is_ready(kbd_poll_dev));
	zassert_true(device_is_ready(gpio_dev));
	zassert_true(mfd_tca8418_has_interrupt(tca_int_dev));
	zassert_false(mfd_tca8418_has_interrupt(tca_poll_dev));
	return NULL;
}

static void tca8418_before(void *fixture)
{
	ARG_UNUSED(fixture);

	tca8418_emul_reset(&tca_int_data);
	tca8418_emul_reset(&tca_poll_data);
	clear_capture();
}

ZTEST(tca8418, test_interrupt_matrix_event_and_input_keymap)
{
	trigger_interrupt_event(TCA8418_KEY_EVENT_PRESS | matrix_code(1, 2));

	zassert_true(captured_has(kbd_int_dev, INPUT_EV_ABS, INPUT_ABS_X, 2));
	zassert_true(captured_has(kbd_int_dev, INPUT_EV_ABS, INPUT_ABS_Y, 1));
	zassert_true(captured_has(kbd_int_dev, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1));
	zassert_true(captured_has(DEVICE_DT_GET(DT_CHILD(KBD_INT_NODE, keymap)),
				  INPUT_EV_KEY, INPUT_KEY_6, 1));

	trigger_interrupt_event(TCA8418_KEY_EVENT_PRESS | matrix_code(0, 3));
	zassert_true(captured_has(DEVICE_DT_GET(DT_CHILD(KBD_INT_NODE, keymap)),
				  INPUT_EV_KEY, INPUT_KEY_KPDOT, 1));

	trigger_interrupt_event(TCA8418_KEY_EVENT_PRESS | matrix_code(2, 3));
	zassert_true(captured_has(DEVICE_DT_GET(DT_CHILD(KBD_INT_NODE, keymap)),
				  INPUT_EV_KEY, INPUT_KEY_KPASTERISK, 1));
}

ZTEST(tca8418, test_out_of_range_matrix_event_is_filtered)
{
	trigger_interrupt_event(TCA8418_KEY_EVENT_PRESS | matrix_code(7, 9));

	zassert_false(captured_has(kbd_int_dev, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1));
	zassert_false(captured_has(DEVICE_DT_GET(DT_CHILD(KBD_INT_NODE, keymap)),
				   INPUT_EV_KEY, INPUT_KEY_6, 1));
}

ZTEST(tca8418, test_polling_fallback_without_interrupt_gpio)
{
	tca8418_emul_set_event(&tca_poll_data, TCA8418_KEY_EVENT_PRESS | matrix_code(0, 1));
	k_sleep(K_MSEC(30));

	zassert_true(captured_has(kbd_poll_dev, INPUT_EV_ABS, INPUT_ABS_X, 1));
	zassert_true(captured_has(kbd_poll_dev, INPUT_EV_ABS, INPUT_ABS_Y, 0));
	zassert_true(captured_has(kbd_poll_dev, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1));
}

ZTEST_SUITE(tca8418, NULL, tca8418_setup, tca8418_before, NULL, NULL);
