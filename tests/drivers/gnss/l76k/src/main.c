/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/ztest.h>

#define L76K_NODE DT_NODELABEL(l76k)
#define UART_NODE DT_NODELABEL(test_uart)
#define GPIO_NODE DT_NODELABEL(test_gpio)

static const struct device *const l76k_dev = DEVICE_DT_GET(L76K_NODE);
static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);
static const struct device *const gpio_dev = DEVICE_DT_GET(GPIO_NODE);

#define L76K_TEST_UART_INIT_PRIORITY 79

static int l76k_test_configure_uart(void)
{
	const struct uart_config cfg = {
		.baudrate = 9600,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(uart_dev, &cfg);
}

SYS_INIT(l76k_test_configure_uart, POST_KERNEL, L76K_TEST_UART_INIT_PRIORITY);

static uint8_t pcas_checksum(const char *cmd)
{
	uint8_t checksum = 0;

	for (const char *p = cmd; *p != '\0'; p++) {
		checksum ^= *p;
	}

	return checksum;
}

static void expect_tx_contains_pcas(const char *cmd)
{
	char expected[48];
	char tx[512];
	uint32_t len;

	snprintf(expected, sizeof(expected), "$%s*%02X\r\n", cmd, pcas_checksum(cmd));
	len = uart_emul_get_tx_data(uart_dev, (uint8_t *)tx, sizeof(tx) - 1U);
	tx[len] = '\0';

	zassert_not_null(strstr(tx, expected), "TX buffer did not contain %s; got %s",
			 expected, tx);
}

static void *l76k_setup(void)
{
	zassert_true(device_is_ready(l76k_dev));
	zassert_true(device_is_ready(uart_dev));
	zassert_true(device_is_ready(gpio_dev));
	return NULL;
}

static void l76k_before(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)uart_emul_flush_tx_data(uart_dev);
}

ZTEST(l76k, test_set_fix_rate_resumes_and_updates_cache_on_success)
{
	uint32_t fix_rate = 0;

	zassert_ok(gnss_set_fix_rate(l76k_dev, 500));
	expect_tx_contains_pcas("PCAS02,500");
	zassert_ok(gnss_get_fix_rate(l76k_dev, &fix_rate));
	zassert_equal(fix_rate, 500);
}

ZTEST(l76k, test_invalid_setter_does_not_transmit_or_update_cache)
{
	uint32_t before;
	uint32_t after;
	char tx[8];

	zassert_ok(gnss_get_fix_rate(l76k_dev, &before));
	zassert_equal(gnss_set_fix_rate(l76k_dev, 333), -EINVAL);
	zassert_equal(uart_emul_get_tx_data(uart_dev, (uint8_t *)tx, sizeof(tx)), 0);
	zassert_ok(gnss_get_fix_rate(l76k_dev, &after));
	zassert_equal(after, before);
}

ZTEST(l76k, test_navigation_mode_command_checksum)
{
	enum gnss_navigation_mode mode;

	zassert_ok(gnss_set_navigation_mode(l76k_dev, GNSS_NAVIGATION_MODE_HIGH_DYNAMICS));
	expect_tx_contains_pcas("PCAS11,3");
	zassert_ok(gnss_get_navigation_mode(l76k_dev, &mode));
	zassert_equal(mode, GNSS_NAVIGATION_MODE_HIGH_DYNAMICS);
}

ZTEST(l76k, test_enabled_systems_command_checksum)
{
	gnss_systems_t systems;

	zassert_ok(gnss_set_enabled_systems(l76k_dev, GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS));
	expect_tx_contains_pcas("PCAS04,5");
	zassert_ok(gnss_get_enabled_systems(l76k_dev, &systems));
	zassert_equal(systems, GNSS_SYSTEM_GPS | GNSS_SYSTEM_GLONASS | GNSS_SYSTEM_QZSS);
}

ZTEST(l76k, test_pps_timestamp_is_protected_and_reported)
{
	k_ticks_t timestamp = 0;

	zassert_equal(gnss_get_latest_timepulse(l76k_dev, &timestamp), -EAGAIN);
	zassert_ok(gpio_emul_input_set(gpio_dev, 0, 0));
	zassert_ok(gpio_emul_input_set(gpio_dev, 0, 1));
	k_sleep(K_MSEC(1));
	zassert_ok(gnss_get_latest_timepulse(l76k_dev, &timestamp));
	zassert_not_equal(timestamp, 0);
}

ZTEST(l76k, test_pm_resume_and_suspend_drive_wakeup_gpio)
{
	zassert_ok(pm_device_action_run(l76k_dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(gpio_emul_output_get(gpio_dev, 2), 1);

	zassert_ok(pm_device_action_run(l76k_dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(gpio_emul_output_get(gpio_dev, 2), 0);
}

ZTEST_SUITE(l76k, NULL, l76k_setup, l76k_before, NULL, NULL);
