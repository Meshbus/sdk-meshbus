// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <string.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/input.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_INPUT_NODE DT_NODELABEL(meshbus_test_input)

static int meshbus_input_test_device_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

DEVICE_DT_DEFINE(TEST_INPUT_NODE, meshbus_input_test_device_init, NULL, NULL, NULL,
		 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

static struct meshbus_input_act_event last_action;
K_SEM_DEFINE(action_sem, 0, 16);

static void input_action_listener_cb(const struct zbus_channel *chan)
{
	const struct meshbus_input_act_event *event = zbus_chan_const_msg(chan);

	if (chan != &meshbus_input_action_chan || event == NULL) {
		return;
	}

	last_action = *event;
	k_sem_give(&action_sem);
}

ZBUS_LISTENER_DEFINE(input_action_listener, input_action_listener_cb);
ZBUS_CHAN_ADD_OBS(meshbus_input_action_chan, input_action_listener, 0);

static void action_drain(void)
{
	while (k_sem_take(&action_sem, K_NO_WAIT) == 0) {
	}
	memset(&last_action, 0, sizeof(last_action));
}

static void report_test_key(uint16_t code, int32_t value)
{
	zassert_ok(input_report(DEVICE_DT_GET(TEST_INPUT_NODE), INPUT_EV_KEY, code, value, true,
				K_FOREVER));
}

static void assert_action(uint16_t code, uint8_t action)
{
	zassert_ok(k_sem_take(&action_sem, K_MSEC(200)));
	zassert_equal(last_action.type, INPUT_EV_KEY);
	zassert_equal(last_action.code, code);
	zassert_equal(last_action.action, action);
}

ZTEST(meshbus_input_contract, test_public_raw_channel_accepts_and_retains_event)
{
	struct meshbus_input_event event = {
		.type = INPUT_EV_KEY,
		.code = INPUT_KEY_ENTER,
		.value = 1,
	};
	struct meshbus_input_event got;

	zassert_ok(zbus_chan_pub(&meshbus_input_raw_event_chan, &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&meshbus_input_raw_event_chan, &got, K_NO_WAIT));
	zassert_equal(got.type, event.type);
	zassert_equal(got.code, event.code);
	zassert_equal(got.value, event.value);
}

ZTEST(meshbus_input_contract, test_public_action_channel_accepts_key_and_scroll_actions)
{
	struct meshbus_input_act_event event = {
		.type = INPUT_EV_KEY,
		.code = INPUT_KEY_ENTER,
		.action = INPUT_ACT_KEY_SHORT,
	};
	struct meshbus_input_act_event got;

	zassert_ok(zbus_chan_pub(&meshbus_input_action_chan, &event, K_NO_WAIT));
	(void)k_sem_take(&action_sem, K_MSEC(50));
	zassert_ok(zbus_chan_read(&meshbus_input_action_chan, &got, K_NO_WAIT));
	zassert_equal(got.type, event.type);
	zassert_equal(got.code, event.code);
	zassert_equal(got.action, event.action);

	event.type = INPUT_EV_REL;
	event.code = INPUT_REL_WHEEL;
	event.action = INPUT_ACT_SCROLL_CCW;
	zassert_ok(zbus_chan_pub(&meshbus_input_action_chan, &event, K_NO_WAIT));
	(void)k_sem_take(&action_sem, K_MSEC(50));
	zassert_ok(zbus_chan_read(&meshbus_input_action_chan, &got, K_NO_WAIT));
	zassert_equal(got.action, INPUT_ACT_SCROLL_CCW);
}

ZTEST(meshbus_input_contract, test_public_channels_have_no_validator_for_boundary_values)
{
	struct meshbus_input_event raw = {
		.type = UINT8_MAX,
		.code = UINT16_MAX,
		.value = INT32_MIN,
	};
	struct meshbus_input_act_event act = {
		.type = UINT8_MAX,
		.code = UINT16_MAX,
		.action = UINT8_MAX,
	};

	zassert_ok(zbus_chan_pub(&meshbus_input_raw_event_chan, &raw, K_NO_WAIT));
	zassert_ok(zbus_chan_pub(&meshbus_input_action_chan, &act, K_NO_WAIT));
}

ZTEST(meshbus_input_contract, test_ok_key_keeps_short_and_long_press_semantics)
{
	action_drain();
	report_test_key(INPUT_KEY_ENTER, 1);
	k_sleep(K_MSEC(20));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
	report_test_key(INPUT_KEY_ENTER, 0);
	assert_action(INPUT_KEY_ENTER, INPUT_ACT_KEY_SHORT);

	action_drain();
	report_test_key(INPUT_KEY_ENTER, 1);
	assert_action(INPUT_KEY_ENTER, INPUT_ACT_KEY_LONG);
	report_test_key(INPUT_KEY_ENTER, 0);
	k_sleep(K_MSEC(30));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
}

ZTEST(meshbus_input_contract, test_keypad_dot_keeps_short_and_long_press_semantics)
{
	action_drain();
	report_test_key(INPUT_KEY_KPDOT, 1);
	k_sleep(K_MSEC(20));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
	report_test_key(INPUT_KEY_KPDOT, 0);
	assert_action(INPUT_KEY_KPDOT, INPUT_ACT_KEY_SHORT);

	action_drain();
	report_test_key(INPUT_KEY_KPDOT, 1);
	assert_action(INPUT_KEY_KPDOT, INPUT_ACT_KEY_LONG);
	report_test_key(INPUT_KEY_KPDOT, 0);
	k_sleep(K_MSEC(30));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
}

ZTEST(meshbus_input_contract, test_keypad_5_stays_digit_only)
{
	action_drain();
	report_test_key(INPUT_KEY_5, 1);
	k_sleep(K_MSEC(CONFIG_MESHBUS_INPUT_LONG_PRESS_MS + 20));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
	report_test_key(INPUT_KEY_5, 0);
	assert_action(INPUT_KEY_5, INPUT_ACT_KEY_SHORT);
}

ZTEST(meshbus_input_contract, test_t9_control_keys_keep_long_press_semantics)
{
	static const uint16_t long_press_codes[] = {
		INPUT_KEY_4,
		INPUT_KEY_KPDOT,
		INPUT_KEY_KPASTERISK,
	};

	for (size_t i = 0; i < ARRAY_SIZE(long_press_codes); i++) {
		action_drain();
		report_test_key(long_press_codes[i], 1);
		assert_action(long_press_codes[i], INPUT_ACT_KEY_LONG);
		report_test_key(long_press_codes[i], 0);
		k_sleep(K_MSEC(30));
		zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
	}
}

ZTEST(meshbus_input_contract, test_direction_key_repeats_short_press_while_held)
{
	action_drain();
	report_test_key(INPUT_KEY_LEFT, 1);
	assert_action(INPUT_KEY_LEFT, INPUT_ACT_KEY_SHORT);
	assert_action(INPUT_KEY_LEFT, INPUT_ACT_KEY_SHORT);
	report_test_key(INPUT_KEY_LEFT, 0);
	k_sleep(K_MSEC(CONFIG_MESHBUS_INPUT_REPEAT_INTERVAL_MS + 20));
	zassert_equal(k_sem_take(&action_sem, K_NO_WAIT), -EBUSY);
}

ZTEST_SUITE(meshbus_input_contract, NULL, NULL, NULL, NULL, NULL);
