/* Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "readings.h"
#include "weather_ble.h"

#define PAGE_COUNT  9
#define SCROLL_MS   80
#define DEBOUNCE_MS 25
#define STALE_MS    6000

static const struct device *const matrix = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const humidity = DEVICE_DT_GET(DT_NODELABEL(weather_humidity));
static const struct device *const pressure = DEVICE_DT_GET(DT_NODELABEL(weather_pressure));
static const struct device *const particles = DEVICE_DT_GET(DT_NODELABEL(weather_particles));
static const enum sensor_channel pm_channels[] = {SENSOR_CHAN_PM_1_0, SENSOR_CHAN_PM_2_5,
						  SENSOR_CHAN_PM_10};
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct readings latest = {
	.th_error = -EAGAIN, .pressure_error = -EAGAIN, .air_error = -EAGAIN, .pm_error = -EAGAIN};
K_MUTEX_DEFINE(reading_lock);
K_SEM_DEFINE(start_sampling, 0, 1);

static void sample_sensors(void *a, void *b, void *c)
{
	struct readings next = {0};
	bool air_ready = false;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_sem_take(&start_sampling, K_FOREVER);
	while (true) {
		next.th_error = device_is_ready(humidity) ? sensor_sample_fetch(humidity) : -ENODEV;
		if (next.th_error == 0) {
			next.th_error = sensor_channel_get(humidity, SENSOR_CHAN_AMBIENT_TEMP,
							   &next.temperature);
		}
		if (next.th_error == 0) {
			next.th_error =
				sensor_channel_get(humidity, SENSOR_CHAN_HUMIDITY, &next.humidity);
		}
		next.pressure_error =
			device_is_ready(pressure) ? sensor_sample_fetch(pressure) : -ENODEV;
		if (next.pressure_error == 0) {
			next.pressure_error =
				sensor_channel_get(pressure, SENSOR_CHAN_PRESS, &next.pressure);
		}
		next.air_error = air_ready ? 0 : air_init();
		air_ready = next.air_error == 0;
		if (air_ready && next.th_error == 0) {
			int ret = air_compensate(&next.temperature, &next.humidity);

			if (ret != 0) {
				printk("WEATHER compensation rc=%d\n", ret);
			}
		}
		if (air_ready) {
			next.air_error = air_read(&next.air);
			if (next.air_error != 0 && next.air_error != -EAGAIN) {
				air_ready = false;
			}
		}
		next.pm_error =
			device_is_ready(particles) ? sensor_sample_fetch(particles) : -ENODEV;
		for (size_t i = 0; i < ARRAY_SIZE(pm_channels) && next.pm_error == 0; i++) {
			next.pm_error = sensor_channel_get(particles, pm_channels[i], &next.pm[i]);
		}
		next.sampled_at = k_uptime_get();
		k_mutex_lock(&reading_lock, K_FOREVER);
		latest = next;
		k_mutex_unlock(&reading_lock);
		weather_ble_publish(&next);
		printk("WEATHER sample th_rc=%d pressure_rc=%d air_rc=%d validity=%u pm_rc=%d\n",
		       next.th_error, next.pressure_error, next.air_error, next.air.validity,
		       next.pm_error);
		k_msleep(2000);
	}
}
K_THREAD_DEFINE(sampler, 3072, sample_sensors, NULL, NULL, NULL, 7, 0, 0);

/* Five-column glyphs, bit zero at top. Shift down one row on output so every
 * transmitted byte is even: none can form the Modulino's reserved prefixes.
 */
static const uint8_t digits[][5] = {
	{0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},
	{0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
	{0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
	{0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
	{0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
};
static const uint8_t letters[][5] = {
	{0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
	{0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
	{0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
	{0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
	{0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01},
	{0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
	{0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
	{0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
	{0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
	{0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
	{0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
	{0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
	{0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

static uint8_t glyph(char ch, int col)
{
	if (col == 5) {
		return 0;
	}
	if (ch >= '0' && ch <= '9') {
		return digits[ch - '0'][col];
	}
	if (ch >= 'A' && ch <= 'Z') {
		return letters[ch - 'A'][col];
	}
	if (ch == '.') {
		return col == 2 ? 0x40 : 0;
	}
	if (ch == '-') {
		return 0x08;
	}
	if (ch == '/') {
		static const uint8_t slash[] = {0x60, 0x10, 0x08, 0x04, 0x03};
		return slash[col];
	}
	if (ch == '%') {
		static const uint8_t percent[] = {0x63, 0x13, 0x08, 0x64, 0x63};

		return percent[col];
	}
	return 0;
}

static void decimal_text(char *text, size_t size, const char *label, int64_t tenths,
			 const char *unit)
{
	bool negative = tenths < 0;
	int64_t magnitude = negative ? -tenths : tenths;

	snprintf(text, size, "%s %s%lld.%lld %s", label, negative ? "-" : "",
		 (long long)(magnitude / 10), (long long)(magnitude % 10), unit);
}

static void page_text(unsigned int page, char *text, size_t size)
{
	static const char *const labels[] = {"TEMP", "RH",    "PRESS", "ECO2", "TVOC",
					     "AQI",  "PM1.0", "PM2.5", "PM10"};
	struct readings value;
	int error;

	k_mutex_lock(&reading_lock, K_FOREVER);
	value = latest;
	k_mutex_unlock(&reading_lock);
	error = page < 2    ? value.th_error
		: page == 2 ? value.pressure_error
		: page < 6  ? value.air_error
			    : value.pm_error;
	if (value.sampled_at == 0 || error == -EAGAIN) {
		snprintf(text, size, "%s WAIT", labels[page]);
	} else if (error != 0 || k_uptime_get() - value.sampled_at > STALE_MS) {
		snprintf(text, size, "%s ERR", labels[page]);
	} else if (page >= 3 && page < 6 && value.air.validity != 0) {
		const char *state = value.air.validity == 1   ? "WARM"
				    : value.air.validity == 2 ? "INIT"
							      : "ERR";

		snprintf(text, size, "%s %s", labels[page], state);
	} else if (page == 0) {
		decimal_text(text, size, labels[page],
			     sensor_value_to_micro(&value.temperature) / 100000, "C");
	} else if (page == 1) {
		decimal_text(text, size, labels[page],
			     sensor_value_to_micro(&value.humidity) / 100000, "%");
	} else if (page == 2) {
		/* Sensor API pressure is kPa; display hPa with one decimal place. */
		decimal_text(text, size, labels[page],
			     sensor_value_to_micro(&value.pressure) / 10000, "HPA");
	} else if (page >= 6) {
		decimal_text(text, size, labels[page],
			     sensor_value_to_micro(&value.pm[page - 6]) / 100000, "UG/M3");
	} else {
		unsigned int number = page == 3   ? value.air.eco2
				      : page == 4 ? value.air.tvoc
						  : value.air.aqi;

		snprintf(text, size, "%s %u %s", labels[page], number,
			 page == 3   ? "PPM"
			 : page == 4 ? "PPB"
				     : "");
	}
	printk("WEATHER page=%u text=%s\n", page, text);
}

static int draw_text(const char *text, int offset)
{
	uint8_t frame[12];
	const int width = strlen(text) * 6;
	const struct display_buffer_descriptor desc = {
		.width = 12,
		.height = 8,
		.pitch = 12,
		.buf_size = sizeof(frame),
	};

	for (int x = 0; x < 12; x++) {
		int column = offset + x;

		frame[x] = column >= 0 && column < width ? glyph(text[column / 6], column % 6) << 1
							 : 0;
	}
	return display_write(matrix, 0, 0, &desc, frame);
}

int main(void)
{
	char text[64];
	unsigned int page = 0;
	int offset = 0;
	int raw;
	int stable;
	int ret;
	int64_t edge_at;
	int64_t next_frame;

	if (!device_is_ready(matrix) || !gpio_is_ready_dt(&button)) {
		printk("WEATHER_FATAL display or button not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret == 0) {
		ret = display_set_pixel_format(matrix, PIXEL_FORMAT_MONO01);
	}
	if (ret == 0) {
		ret = draw_text("", 0);
	}
	if (ret == 0) {
		ret = display_blanking_off(matrix);
	}
	if (ret < 0) {
		printk("WEATHER_FATAL setup rc=%d\n", ret);
		return ret;
	}
	raw = gpio_pin_get_dt(&button);
	if (raw < 0) {
		return raw;
	}
	stable = raw;
	edge_at = k_uptime_get();
	next_frame = edge_at;
	ret = weather_ble_start();
	if (ret < 0) {
		printk("WEATHER BLE unavailable rc=%d\n", ret);
	}
	k_sem_give(&start_sampling);
	page_text(page, text, sizeof(text));
	printk("WEATHER_READY pages=9 button=GPIO9 active_low=1\n");
	while (true) {
		int64_t now = k_uptime_get();
		int level = gpio_pin_get_dt(&button);

		if (level < 0) {
			printk("WEATHER_FATAL button rc=%d\n", level);
			return level;
		}
		if (level != raw) {
			raw = level;
			edge_at = now;
		}
		if (raw != stable && now - edge_at >= DEBOUNCE_MS) {
			stable = raw;
			if (stable) {
				page = (page + 1) % PAGE_COUNT;
				offset = 0;
				page_text(page, text, sizeof(text));
				next_frame = now;
				printk("WEATHER_BUTTON page=%u\n", page);
			}
		}
		if (now >= next_frame) {
			ret = draw_text(text, offset++);
			if (ret < 0) {
				printk("WEATHER_FATAL display rc=%d\n", ret);
				return ret;
			}
			next_frame = now + SCROLL_MS;
			if (offset >= strlen(text) * 6 + 12) {
				page = (page + 1) % PAGE_COUNT;
				offset = 0;
				page_text(page, text, sizeof(text));
			}
		}
		k_msleep(5);
	}
}
