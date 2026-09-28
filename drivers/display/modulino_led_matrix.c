/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT arduino_modulino_led_matrix

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define MATRIX_WIDTH      12U
#define MATRIX_HEIGHT     8U
#define MATRIX_PIXELS     (MATRIX_WIDTH * MATRIX_HEIGHT)
#define MATRIX_MONO_BYTES 12U
#define MATRIX_GRAY_BYTES 48U
#define MATRIX_ID         0x72

struct modulino_config {
	struct i2c_dt_spec i2c;
	uint32_t command_delay_ms;
};

struct modulino_data {
	struct k_mutex lock;
	/* Canonical row-major luminance, 0..15, independent of input format. */
	uint8_t pixels[MATRIX_PIXELS];
	enum display_pixel_format format;
	bool blanked;
};

static size_t modulino_frame_size(enum display_pixel_format format)
{
	return format == PIXEL_FORMAT_L_4 ? MATRIX_GRAY_BYTES : MATRIX_MONO_BYTES;
}

static int modulino_pack(const uint8_t *pixels, enum display_pixel_format format, uint8_t *frame)
{
	memset(frame, 0, MATRIX_GRAY_BYTES);
	for (size_t i = 0; i < MATRIX_PIXELS; i++) {
		if (format == PIXEL_FORMAT_L_4) {
			frame[i / 2] |= pixels[i] << ((i % 2 == 0) ? 4 : 0);
		} else if (pixels[i] != 0) {
			frame[i % MATRIX_WIDTH] |= BIT(i / MATRIX_WIDTH);
		}
	}

	/* Firmware decodes these prefixes before pixels, without any escaping. */
	if (memcmp(frame, "CF", 2) == 0 || memcmp(frame, "DIE", 3) == 0 ||
	    memcmp(frame, "MON", 3) == 0 || memcmp(frame, "GS4", 3) == 0) {
		return -EINVAL;
	}

	return 0;
}

static int modulino_get_mode(const struct device *dev, enum display_pixel_format *format)
{
	const struct modulino_config *config = dev->config;
	uint8_t status[4];
	int ret = i2c_read_dt(&config->i2c, status, sizeof(status));

	if (ret < 0) {
		return ret;
	}
	/* The identity is the pinstrap ID, even at a user-configured address. */
	if (status[0] != MATRIX_ID) {
		return -ENODEV;
	}
	if (memcmp(&status[1], "MON", 3) == 0) {
		*format = PIXEL_FORMAT_MONO01;
	} else if (memcmp(&status[1], "GS4", 3) == 0) {
		*format = PIXEL_FORMAT_L_4;
	} else {
		return -ENOTSUP;
	}
	return 0;
}

static int modulino_send(const struct device *dev, const uint8_t *frame,
			 enum display_pixel_format format)
{
	const struct modulino_config *config = dev->config;
	enum display_pixel_format current;
	int ret = modulino_get_mode(dev, &current);

	if (ret < 0) {
		return ret;
	}
	if (current != format) {
		uint8_t command[MATRIX_GRAY_BYTES] = {0};

		memcpy(command, format == PIXEL_FORMAT_L_4 ? "GS4" : "MON", 3);
		/* The receiver requires the OLD mode's exact packet length. */
		ret = i2c_write_dt(&config->i2c, command, modulino_frame_size(current));
		k_msleep(config->command_delay_ms);
		if (ret < 0) {
			return ret;
		}
		ret = modulino_get_mode(dev, &current);
		if (ret < 0) {
			return ret;
		}
		if (current != format) {
			return -EIO;
		}
	}

	ret = i2c_write_dt(&config->i2c, frame, modulino_frame_size(format));
	/* A following read also overwrites the firmware's shared I2C buffer. */
	k_msleep(config->command_delay_ms);
	return ret;
}

static int modulino_write(const struct device *dev, uint16_t x, uint16_t y,
			  const struct display_buffer_descriptor *desc, const void *buf)
{
	struct modulino_data *data = dev->data;
	const uint8_t *src = buf;
	uint8_t pixels[MATRIX_PIXELS];
	uint8_t frame[MATRIX_GRAY_BYTES];
	size_t required;
	int ret;

	if (desc == NULL || buf == NULL || desc->width == 0 || desc->height == 0 ||
	    desc->pitch < desc->width || x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT ||
	    desc->width > MATRIX_WIDTH - x || desc->height > MATRIX_HEIGHT - y) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->format == PIXEL_FORMAT_L_4) {
		/* Each row starts on a byte boundary; an odd pitch has a pad nibble. */
		required = (desc->height - 1U) * DIV_ROUND_UP(desc->pitch, 2U) +
			   DIV_ROUND_UP(desc->width, 2U);
	} else {
		/* At most eight rows, so the input is one vertical tile per column. */
		required = desc->width;
	}
	if (desc->buf_size < required) {
		ret = -EINVAL;
		goto out;
	}

	memcpy(pixels, data->pixels, sizeof(pixels));
	for (size_t row = 0; row < desc->height; row++) {
		for (size_t col = 0; col < desc->width; col++) {
			uint8_t value;

			if (data->format == PIXEL_FORMAT_L_4) {
				size_t offset = row * DIV_ROUND_UP(desc->pitch, 2U) + col / 2;

				value = (src[offset] >> ((col % 2 == 0) ? 4 : 0)) & 0x0f;
			} else {
				value = (src[col] & BIT(row)) ? 15 : 0;
			}
			pixels[(y + row) * MATRIX_WIDTH + x + col] = value;
		}
	}

	ret = modulino_pack(pixels, data->format, frame);
	if (ret == 0 && !data->blanked) {
		ret = modulino_send(dev, frame, data->format);
	}
	if (ret == 0) {
		memcpy(data->pixels, pixels, sizeof(pixels));
	}
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int modulino_blanking(const struct device *dev, bool blanked)
{
	struct modulino_data *data = dev->data;
	uint8_t frame[MATRIX_GRAY_BYTES] = {0};
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!blanked) {
		ret = modulino_pack(data->pixels, data->format, frame);
	}
	if (ret == 0) {
		ret = modulino_send(dev, frame, data->format);
	}
	if (ret == 0) {
		data->blanked = blanked;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int modulino_blanking_on(const struct device *dev)
{
	return modulino_blanking(dev, true);
}

static int modulino_blanking_off(const struct device *dev)
{
	return modulino_blanking(dev, false);
}

static int modulino_set_pixel_format(const struct device *dev, enum display_pixel_format format)
{
	struct modulino_data *data = dev->data;
	uint8_t frame[MATRIX_GRAY_BYTES];
	int ret;

	if (format != PIXEL_FORMAT_MONO01 && format != PIXEL_FORMAT_L_4) {
		return -ENOTSUP;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	ret = modulino_pack(data->pixels, format, frame);
	if (ret == 0 && !data->blanked) {
		ret = modulino_send(dev, frame, format);
	}
	if (ret == 0) {
		data->format = format;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static void modulino_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	struct modulino_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = MATRIX_WIDTH;
	caps->y_resolution = MATRIX_HEIGHT;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_L_4;
	caps->current_pixel_format = data->format;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
	if (data->format == PIXEL_FORMAT_MONO01) {
		caps->screen_info = SCREEN_INFO_MONO_VTILED;
	}
	k_mutex_unlock(&data->lock);
}

static int modulino_init(const struct device *dev)
{
	const struct modulino_config *config = dev->config;
	struct modulino_data *data = dev->data;
	uint8_t frame[MATRIX_GRAY_BYTES] = {0};

	if (!i2c_is_ready_dt(&config->i2c)) {
		return -ENODEV;
	}
	k_mutex_init(&data->lock);
	data->format = PIXEL_FORMAT_MONO01;
	data->blanked = true;
	return modulino_send(dev, frame, data->format);
}

static DEVICE_API(display, modulino_api) = {
	.blanking_on = modulino_blanking_on,
	.blanking_off = modulino_blanking_off,
	.write = modulino_write,
	.set_pixel_format = modulino_set_pixel_format,
	.get_capabilities = modulino_get_capabilities,
};

#define MODULINO_DEFINE(inst)                                                                      \
	BUILD_ASSERT(DT_INST_PROP(inst, command_delay_ms) > 0,                                     \
		     "command-delay-ms must be positive");                                         \
	static const struct modulino_config modulino_config_##inst = {                             \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.command_delay_ms = DT_INST_PROP(inst, command_delay_ms),                          \
	};                                                                                         \
	static struct modulino_data modulino_data_##inst;                                          \
	DEVICE_DT_INST_DEFINE(inst, modulino_init, NULL, &modulino_data_##inst,                    \
			      &modulino_config_##inst, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,  \
			      &modulino_api);

DT_INST_FOREACH_STATUS_OKAY(MODULINO_DEFINE)
