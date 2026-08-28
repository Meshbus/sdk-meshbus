/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "indicator_light.h"

#if IS_ENABLED(CONFIG_MESHBUS_INDICATOR_LIGHT) && IS_ENABLED(MESHBUS_INDICATOR_LIGHT_DEVICE_EXIST)

LOG_MODULE_REGISTER(meshbus_indicator_light, CONFIG_MESHBUS_INDICATOR_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Devices                                                                    */
/* -------------------------------------------------------------------------- */
#define LED_NODE DT_CHOSEN(meshbus_indicator_light_active)
#if DT_NODE_HAS_PROP(LED_NODE, pwms)
#define LED_TYPE_PWM 1
#include <zephyr/drivers/led.h>
#define LED_PARENT_NODE DT_PARENT(LED_NODE)
static const struct device *const led_dev = DEVICE_DT_GET(LED_PARENT_NODE);
#elif DT_NODE_HAS_PROP(LED_NODE, gpios)
/* GPIO LED */
#define LED_TYPE_GPIO 1
#include <zephyr/drivers/gpio.h>
static const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(LED_NODE, gpios);
#elif DT_NODE_HAS_PROP(LED_NODE, chain_length)
/* LED strip (WS2812, etc.) */
#define LED_TYPE_STRIP 1
#include <zephyr/drivers/led_strip.h>
static const struct device *const led_strip_dev = DEVICE_DT_GET(LED_NODE);
static struct led_rgb led_pixel;
#else
/* Generic LED driver - assume the node itself is a LED device */
#define LED_TYPE_GENERIC 1
#include <zephyr/drivers/led.h>
static const struct device *const led_dev = DEVICE_DT_GET(LED_NODE);
#endif

/* -------------------------------------------------------------------------- */
/* Defaults And State                                                         */
/* -------------------------------------------------------------------------- */
struct light_play_state {
	uint32_t on_duration_ms;  /**< On duration */
	uint32_t off_duration_ms; /**< Off duration */
	uint8_t r, g, b;          /**< Color */
	uint8_t remaining_cycles; /**< Remaining cycles (0 = infinite) */
	uint8_t current_cycle;    /**< Current cycle index */
	bool on_phase;            /**< True if in on phase */
	bool active;              /**< Playback is active */
};
static struct {
	/* Playback state */
	struct light_play_state play;

	/* Idle heartbeat state */
	uint32_t idle_on_duration_ms;
	uint32_t idle_off_duration_ms;
	uint8_t idle_r, idle_g, idle_b;
	uint32_t last_idle_time;
	bool idle_enabled;

	/* Master enable/disable */
	bool enabled;

	/* Hardware state */
	bool ready;
} light_state = {
	.play =
		{
			.r = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_RED,
			.g = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_GREEN,
			.b = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_BLUE,
		},
	.idle_on_duration_ms = CONFIG_MESHBUS_INDICATOR_LIGHT_IDLE_ON_DURATION,
	.idle_off_duration_ms = CONFIG_MESHBUS_INDICATOR_LIGHT_IDLE_OFF_DURATION,
	.idle_r = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_RED,
	.idle_g = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_GREEN,
	.idle_b = CONFIG_MESHBUS_INDICATOR_LIGHT_DEFAULT_BLUE,
	.idle_enabled = true,
	.enabled = true,
};
static K_MUTEX_DEFINE(light_mutex);
static struct k_work_delayable light_work;

/* -------------------------------------------------------------------------- */
/* Validation And Helpers                                                     */
/* -------------------------------------------------------------------------- */
static void led_hw_set(bool on, uint8_t r, uint8_t g, uint8_t b)
{
#if defined(LED_TYPE_GPIO)
	/* GPIO LED - simple on/off, ignore color */
	gpio_pin_set_dt(&led_gpio, on ? 1 : 0);

#elif defined(LED_TYPE_PWM)
	/* PWM LED - use brightness based on max color component */
	if (on) {
		uint8_t brightness = MAX(MAX(r, g), b);
		/* If no color specified, use full brightness */
		if (brightness == 0) {
			brightness = 255;
		}
		/* Scale to 0-100% */
		uint8_t level = (brightness * 100U) / 255U;
		led_set_brightness(led_dev, 0, level);
	} else {
		led_set_brightness(led_dev, 0, 0);
	}

#elif defined(LED_TYPE_STRIP)
	/* LED strip - set RGB color */
	if (on) {
		led_pixel.r = r;
		led_pixel.g = g;
		led_pixel.b = b;
	} else {
		led_pixel.r = 0;
		led_pixel.g = 0;
		led_pixel.b = 0;
	}
	led_strip_update_rgb(led_strip_dev, &led_pixel, 1);

#elif defined(LED_TYPE_GENERIC)
	/* Generic LED driver */
	if (on) {
		led_on(led_dev, 0);
	} else {
		led_off(led_dev, 0);
	}
#endif
}

static int led_hw_init(void)
{
	int rc = 0;

#if defined(LED_TYPE_GPIO)
	if (!gpio_is_ready_dt(&led_gpio)) {
		LOG_ERR("LED GPIO device not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("Failed to configure LED GPIO: %d", rc);
		return rc;
	}

	LOG_DBG("LED initialized (GPIO)");

#elif defined(LED_TYPE_PWM)
	if (!device_is_ready(led_dev)) {
		LOG_ERR("LED PWM device not ready");
		return -ENODEV;
	}

	LOG_DBG("LED initialized (PWM)");

#elif defined(LED_TYPE_STRIP)
	if (!device_is_ready(led_strip_dev)) {
		LOG_ERR("LED strip device not ready");
		return -ENODEV;
	}

	/* Initialize pixel to off */
	led_pixel.r = 0;
	led_pixel.g = 0;
	led_pixel.b = 0;
	led_strip_update_rgb(led_strip_dev, &led_pixel, 1);

	LOG_DBG("LED initialized (LED strip)");

#elif defined(LED_TYPE_GENERIC)
	if (!device_is_ready(led_dev)) {
		LOG_ERR("LED device not ready");
		return -ENODEV;
	}

	LOG_DBG("LED initialized (Generic)");
#endif

	return rc;
}

static bool led_hw_supports_rgb(void)
{
#if defined(LED_TYPE_STRIP)
	return true;
#else
	return false;
#endif
}

static void led_update_output(void)
{
	if (!light_state.play.active) {
		led_hw_set(false, 0, 0, 0);
		return;
	}

	if (light_state.play.on_phase) {
		led_hw_set(true, light_state.play.r, light_state.play.g, light_state.play.b);
	} else {
		led_hw_set(false, 0, 0, 0);
	}
}

static void light_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&light_mutex, K_FOREVER);

	if (!light_state.play.active) {
		/* Check for idle heartbeat */
		uint32_t idle_total =
			light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
		if (light_state.idle_enabled && idle_total > 0) {
			uint32_t now = k_uptime_get_32();
			uint32_t elapsed = now - light_state.last_idle_time;

			if (elapsed >= idle_total) {
				/* Start new idle cycle */
				light_state.last_idle_time = now;
				led_hw_set(true, light_state.idle_r, light_state.idle_g,
					   light_state.idle_b);
				k_mutex_unlock(&light_mutex);
				k_work_reschedule(&light_work,
						  K_MSEC(light_state.idle_on_duration_ms));
				return;
			} else if (elapsed < light_state.idle_on_duration_ms) {
				/* Still in on phase */
				led_hw_set(true, light_state.idle_r, light_state.idle_g,
					   light_state.idle_b);
				k_mutex_unlock(&light_mutex);
				k_work_reschedule(
					&light_work,
					K_MSEC(light_state.idle_on_duration_ms - elapsed));
				return;
			} else {
				/* In off phase */
				led_hw_set(false, 0, 0, 0);
				k_mutex_unlock(&light_mutex);
				k_work_reschedule(&light_work, K_MSEC(idle_total - elapsed));
				return;
			}
		}
		k_mutex_unlock(&light_mutex);
		return;
	}

	struct light_play_state *play = &light_state.play;

	if (play->on_phase) {
		/* Transition to off phase */
		play->on_phase = false;
		led_update_output();

		if (play->off_duration_ms > 0) {
			k_mutex_unlock(&light_mutex);
			k_work_reschedule(&light_work, K_MSEC(play->off_duration_ms));
			return;
		}
		/* No off phase, go directly to next cycle */
	}

	/* End of off phase or no off phase */
	play->current_cycle++;

	if (play->remaining_cycles > 0 && play->current_cycle >= play->remaining_cycles) {
		/* All cycles done, deactivate */
		play->active = false;
		led_update_output();

		/* Start idle timer if enabled */
		uint32_t idle_total =
			light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
		if (light_state.idle_enabled && idle_total > 0) {
			light_state.last_idle_time = k_uptime_get_32();
			k_mutex_unlock(&light_mutex);
			k_work_reschedule(&light_work, K_MSEC(idle_total));
			return;
		}
		k_mutex_unlock(&light_mutex);
		return;
	}

	/* Start next on phase */
	play->on_phase = true;
	led_update_output();

	k_mutex_unlock(&light_mutex);

	if (play->on_duration_ms > 0) {
		k_work_reschedule(&light_work, K_MSEC(play->on_duration_ms));
	}
}

static void led_start_play(void)
{
	struct light_play_state *play = &light_state.play;

	play->current_cycle = 0;
	play->on_phase = true;

	led_update_output();

	if (play->on_duration_ms > 0) {
		k_work_reschedule(&light_work, K_MSEC(play->on_duration_ms));
	}
}

bool indicator_light_is_ready(void)
{
	return light_state.ready;
}

bool indicator_light_supports_rgb(void)
{
	return light_state.ready && led_hw_supports_rgb();
}

void indicator_light_set_idle_color(uint8_t r, uint8_t g, uint8_t b)
{
	k_mutex_lock(&light_mutex, K_FOREVER);
	light_state.idle_r = r;
	light_state.idle_g = g;
	light_state.idle_b = b;
	k_mutex_unlock(&light_mutex);

	LOG_DBG("Idle color set: r=%u g=%u b=%u", r, g, b);
}

void indicator_light_set_idle(uint32_t on_duration_ms, uint32_t off_duration_ms)
{
	k_mutex_lock(&light_mutex, K_FOREVER);
	light_state.idle_on_duration_ms = on_duration_ms;
	light_state.idle_off_duration_ms = off_duration_ms;
	light_state.last_idle_time = k_uptime_get_32();
	k_mutex_unlock(&light_mutex);

	LOG_DBG("Idle timing set: on=%u off=%u", on_duration_ms, off_duration_ms);
}

void indicator_light_set_idle_enabled(bool enable)
{
	k_mutex_lock(&light_mutex, K_FOREVER);

	bool was_enabled = light_state.idle_enabled;
	light_state.idle_enabled = enable;

	if (enable && !was_enabled && light_state.enabled && !light_state.play.active) {
		/* Start idle timer */
		uint32_t idle_total =
			light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
		if (idle_total > 0) {
			light_state.last_idle_time = k_uptime_get_32();
			k_work_reschedule(&light_work, K_MSEC(idle_total));
		}
	} else if (!enable && was_enabled) {
		/* Cancel idle work if no active playback */
		if (!light_state.play.active) {
			k_work_cancel_delayable(&light_work);
			led_hw_set(false, 0, 0, 0);
		}
	}

	k_mutex_unlock(&light_mutex);

	LOG_DBG("Idle enabled: %d", enable);
}

void indicator_light_set_enabled(bool enable)
{
	k_mutex_lock(&light_mutex, K_FOREVER);

	bool was_enabled = light_state.enabled;
	light_state.enabled = enable;

	if (!enable && was_enabled) {
		/* Turning off - stop everything */
		light_state.play.active = false;
		k_work_cancel_delayable(&light_work);
		led_hw_set(false, 0, 0, 0);
	} else if (enable && !was_enabled) {
		/* Turning on - restart idle if appropriate */
		if (light_state.idle_enabled && !light_state.play.active) {
			uint32_t idle_total =
				light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
			if (idle_total > 0) {
				light_state.last_idle_time = k_uptime_get_32();
				k_work_reschedule(&light_work, K_MSEC(idle_total));
			}
		}
	}

	k_mutex_unlock(&light_mutex);

	LOG_DBG("Light enabled: %d", enable);
}

void indicator_light_set_color(uint8_t r, uint8_t g, uint8_t b)
{
	k_mutex_lock(&light_mutex, K_FOREVER);
	light_state.play.r = r;
	light_state.play.g = g;
	light_state.play.b = b;
	k_mutex_unlock(&light_mutex);

	LOG_DBG("Play color set: r=%u g=%u b=%u", r, g, b);
}

int indicator_light_play(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t count)
{
	if (!light_state.ready) {
		return -ENODEV;
	}

	if (!light_state.enabled) {
		return -ENOTSUP;
	}

	k_mutex_lock(&light_mutex, K_FOREVER);

	/* Setup state - replaces any current request */
	light_state.play.on_duration_ms = on_duration_ms;
	light_state.play.off_duration_ms = off_duration_ms;
	light_state.play.remaining_cycles = count;
	light_state.play.active = true;

	led_start_play();

	k_mutex_unlock(&light_mutex);

	LOG_DBG("Play started: on=%u off=%u count=%u", on_duration_ms, off_duration_ms, count);

	return 0;
}

void indicator_light_stop(void)
{
	k_mutex_lock(&light_mutex, K_FOREVER);

	light_state.play.active = false;
	led_update_output();

	/* Start idle timer if enabled */
	uint32_t idle_total = light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
	if (light_state.idle_enabled && idle_total > 0) {
		light_state.last_idle_time = k_uptime_get_32();
		k_work_reschedule(&light_work, K_MSEC(idle_total));
	}

	k_mutex_unlock(&light_mutex);

	LOG_DBG("Play stopped");
}

/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */
int meshbus_indicator_light_init(void)
{
	k_work_init_delayable(&light_work, light_work_handler);

	int rc = led_hw_init();
	if (rc == 0) {
		light_state.ready = true;

		/* Start idle heartbeat if enabled */
		uint32_t idle_total =
			light_state.idle_on_duration_ms + light_state.idle_off_duration_ms;
		if (light_state.idle_enabled && idle_total > 0) {
			light_state.last_idle_time = k_uptime_get_32();
			k_work_reschedule(&light_work, K_MSEC(idle_total));
		}

		LOG_INF("Indicator light is ready");
	} else if (rc == -ENODEV) {
		LOG_WRN("Indicator no light hardware");
	} else {
		LOG_ERR("Indicator light initialize failed: %d", rc);
	}

	return 0; /* Don't fail system init */
}

#endif
