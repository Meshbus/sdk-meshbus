/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <bootutil/mcuboot_status.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define STATUS_LED_NODE DT_ALIAS(mcuboot_led0)

#if DT_NODE_EXISTS(STATUS_LED_NODE) && DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay) && \
	DT_NODE_HAS_PROP(STATUS_LED_NODE, gpios)
#define STATUS_LED_AVAILABLE 1
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
#else
#define STATUS_LED_AVAILABLE 0
#endif

#define BREATH_PERIOD_US 20000U
#define BREATH_LEVELS 32U
#define BREATH_STEP_PERIODS 1U
#define UART_FLASH_MS 180

static atomic_t indicator_running;
static atomic_t uart_flash_until_ms;

#if STATUS_LED_AVAILABLE
static void status_led_set(bool active)
{
	(void)gpio_pin_set_dt(&status_led, active ? 1 : 0);
}

static bool uart_flash_active(void)
{
	return (int32_t)(atomic_get(&uart_flash_until_ms) - k_uptime_get_32()) > 0;
}

static void indicator_update(bool force)
{
	static uint32_t last_update_ms;
	uint32_t now;
	bool active;

	if (atomic_get(&indicator_running) == 0) {
		return;
	}

	now = k_uptime_get_32();
	if (!force && (now == last_update_ms)) {
		return;
	}

	last_update_ms = now;

	if (uart_flash_active()) {
		active = ((now / 35U) & 1U) == 0U;
	} else {
		const uint32_t frame_ms = BREATH_PERIOD_US / 1000U;
		const uint32_t step_ms = frame_ms * BREATH_STEP_PERIODS;
		const uint32_t cycle_steps = BREATH_LEVELS * 2U;
		uint32_t phase = (now / step_ms) % cycle_steps;
		uint32_t level;

		if (phase >= BREATH_LEVELS) {
			phase = (cycle_steps - 1U) - phase;
		}

		level = phase;
		active = (now % frame_ms) < ((level * frame_ms) / BREATH_LEVELS);
	}

	status_led_set(active);
}

static void indicator_start(void)
{
	if (!device_is_ready(status_led.port)) {
		return;
	}

	if (gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE) != 0) {
		return;
	}

	atomic_set(&indicator_running, 1);
	status_led_set(true);
}

static void indicator_stop(void)
{
	atomic_set(&indicator_running, 0);
	status_led_set(false);
}
#else
static void indicator_start(void)
{
}

static void indicator_stop(void)
{
}

static void indicator_update(bool force)
{
	ARG_UNUSED(force);
}
#endif

static void indicator_uart_activity(void)
{
	atomic_set(&uart_flash_until_ms, (atomic_val_t)(k_uptime_get_32() + UART_FLASH_MS));
	indicator_update(true);
}

void mcuboot_status_change(mcuboot_status_type_t status)
{
	switch (status) {
	case MCUBOOT_STATUS_SERIAL_DFU_ENTERED:
	case MCUBOOT_STATUS_NO_BOOTABLE_IMAGE_FOUND:
	case MCUBOOT_STATUS_BOOT_FAILED:
		indicator_start();
		break;
	case MCUBOOT_STATUS_BOOTABLE_IMAGE_FOUND:
		indicator_stop();
		break;
	default:
		break;
	}
}

extern int __real_console_read(char *str, int str_cnt, int *newline);
extern void __real_console_write(const char *str, int cnt);

int __wrap_console_read(char *str, int str_cnt, int *newline)
{
	int rc;

	indicator_update(false);
	rc = __real_console_read(str, str_cnt, newline);
	indicator_update(false);

	if ((rc > 0) || ((newline != NULL) && (*newline != 0))) {
		indicator_uart_activity();
	}

	return rc;
}

void __wrap_console_write(const char *str, int cnt)
{
	if (cnt > 0) {
		indicator_uart_activity();
	}

	__real_console_write(str, cnt);
	indicator_update(false);
}
