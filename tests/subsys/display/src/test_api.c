/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <display/display.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#define TEST_DISPLAY_NODE DT_NODELABEL(test_display)

struct test_display_data {
	enum display_pixel_format pixel_format;
	uint8_t brightness;
	uint32_t pixel_format_call_count;
	uint32_t pixel_format_fail_call;
	int pixel_format_fail_rc;
	int brightness_fail_rc;
};

static struct test_display_data test_display_data;

static int test_display_init(const struct device *dev)
{
	struct test_display_data *data = dev->data;

	data->pixel_format = PIXEL_FORMAT_MONO01;
	data->brightness = UINT8_MAX;
	return 0;
}

static int test_display_blanking(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int test_display_write(const struct device *dev, uint16_t x, uint16_t y,
			      const struct display_buffer_descriptor *desc, const void *buf)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(desc);
	ARG_UNUSED(buf);
	return 0;
}

static int test_display_set_contrast(const struct device *dev, uint8_t contrast)
{
	struct test_display_data *data = dev->data;

	if (data->brightness_fail_rc != 0) {
		return data->brightness_fail_rc;
	}

	data->brightness = contrast;
	return 0;
}

static void test_display_get_capabilities(const struct device *dev,
					  struct display_capabilities *capabilities)
{
	struct test_display_data *data = dev->data;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = 128;
	capabilities->y_resolution = 64;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_MONO01 | PIXEL_FORMAT_MONO10;
	capabilities->current_pixel_format = data->pixel_format;
}

static int test_display_set_pixel_format(const struct device *dev,
					 enum display_pixel_format pixel_format)
{
	struct test_display_data *data = dev->data;

	data->pixel_format_call_count++;
	if (data->pixel_format_fail_call == data->pixel_format_call_count) {
		return data->pixel_format_fail_rc;
	}

	data->pixel_format = pixel_format;
	return 0;
}

static DEVICE_API(display, test_display_api) = {
	.blanking_on = test_display_blanking,
	.blanking_off = test_display_blanking,
	.write = test_display_write,
	.set_contrast = test_display_set_contrast,
	.get_capabilities = test_display_get_capabilities,
	.set_pixel_format = test_display_set_pixel_format,
};

DEVICE_DT_DEFINE(TEST_DISPLAY_NODE, test_display_init, NULL, &test_display_data, NULL,
		 POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &test_display_api);

static mbs_display_config valid_display_config(void)
{
	mbs_display_config cfg = meshbus_DisplayConfig_init_zero;

	cfg.brightness = 25;
	cfg.sleep_timeout = 30;
	cfg.invert = false;
	return cfg;
}

static void *display_suite_setup(void)
{
	zassert_ok(mbs_display_config_reset());
	return NULL;
}

static void display_before(void *fixture)
{
	ARG_UNUSED(fixture);
	test_display_data.pixel_format_fail_call = 0U;
	test_display_data.pixel_format_fail_rc = 0;
	test_display_data.brightness_fail_rc = 0;
	zassert_ok(mbs_display_config_reset());
	mbs_display_active(false);
	test_display_data.pixel_format_call_count = 0U;
}

ZTEST(mbs_display_contract, test_config_defaults_and_validation)
{
	mbs_display_config cfg;

	zassert_equal(mbs_display_config_get(NULL), -EINVAL);
	zassert_ok(mbs_display_config_get(&cfg));
	zassert_equal(cfg.brightness, CONFIG_MBS_DISPLAY_DEFAULT_BRIGHTNESS);
	zassert_equal(cfg.sleep_timeout, CONFIG_MBS_DISPLAY_DEFAULT_SLEEP_TIMEOUT);
	zassert_false(cfg.invert);

	cfg = valid_display_config();
	cfg.brightness = 101;
	zassert_equal(mbs_display_config_set(&cfg), -EINVAL);

	cfg = valid_display_config();
	cfg.sleep_timeout = 301;
	zassert_equal(mbs_display_config_set(&cfg), -EINVAL);
}

ZTEST(mbs_display_contract, test_config_set_get_reset_with_display_device)
{
	mbs_display_config cfg = valid_display_config();
	mbs_display_config got;

	cfg.invert = true;
	zassert_ok(mbs_display_config_set(&cfg));
	zassert_ok(mbs_display_config_get(&got));
	zassert_equal(got.brightness, cfg.brightness);
	zassert_equal(got.sleep_timeout, cfg.sleep_timeout);
	zassert_equal(got.invert, cfg.invert);
	zassert_equal(test_display_data.brightness, 64U);
	zassert_equal(test_display_data.pixel_format, PIXEL_FORMAT_MONO10);

	zassert_ok(mbs_display_config_reset());
	zassert_ok(mbs_display_config_get(&got));
	zassert_equal(got.brightness, CONFIG_MBS_DISPLAY_DEFAULT_BRIGHTNESS);
	zassert_false(got.invert);
	zassert_equal(test_display_data.pixel_format, PIXEL_FORMAT_MONO01);
}

ZTEST(mbs_display_contract, test_active_state_and_public_channel_contract)
{
	struct mbs_display_state_event event = {
		.active = true,
	};
	struct mbs_display_state_event got;

	zassert_false(mbs_display_is_active());
	mbs_display_active(true);
	zassert_true(mbs_display_is_active());

	zassert_ok(zbus_chan_pub(&mbs_display_state_chan, &event, K_NO_WAIT));
	zassert_ok(zbus_chan_read(&mbs_display_state_chan, &got, K_NO_WAIT));
	zassert_true(got.active);
}

ZTEST(mbs_display_contract, test_brightness_failure_rolls_back_invert)
{
	mbs_display_config cfg = valid_display_config();
	mbs_display_config got;

	cfg.invert = true;
	test_display_data.brightness_fail_rc = -EAGAIN;

	zassert_equal(mbs_display_config_set(&cfg), -EAGAIN);
	zassert_equal(test_display_data.pixel_format, PIXEL_FORMAT_MONO01,
		      "invert was not restored after brightness failure");
	zassert_equal(test_display_data.pixel_format_call_count, 2U,
		      "invert rollback was not attempted");
	zassert_ok(mbs_display_config_get(&got));
	zassert_equal(got.brightness, CONFIG_MBS_DISPLAY_DEFAULT_BRIGHTNESS);
	zassert_false(got.invert, "stored config changed after failed hardware apply");
}

ZTEST(mbs_display_contract, test_invert_rollback_failure_reports_degraded_state)
{
	mbs_display_config cfg = valid_display_config();
	mbs_display_config got;

	cfg.invert = true;
	test_display_data.brightness_fail_rc = -EAGAIN;
	test_display_data.pixel_format_fail_call = 2U;
	test_display_data.pixel_format_fail_rc = -EBUSY;

	zassert_equal(mbs_display_config_set(&cfg), -EIO,
		      "rollback failure did not report unknown hardware state");
	zassert_equal(test_display_data.pixel_format, PIXEL_FORMAT_MONO10,
		      "fake did not preserve degraded hardware state");
	zassert_equal(test_display_data.pixel_format_call_count, 2U,
		      "invert rollback was not attempted");
	zassert_ok(mbs_display_config_get(&got));
	zassert_equal(got.brightness, CONFIG_MBS_DISPLAY_DEFAULT_BRIGHTNESS);
	zassert_false(got.invert, "stored config changed after rollback failure");
}

ZTEST(mbs_display_contract, test_dump_without_snapshot_backend)
{
	struct mbs_display_dump_chunk chunk;
	zassert_equal(mbs_display_dump_read(0, 0, 0, &chunk), -ENOTSUP);
}

ZTEST_SUITE(mbs_display_contract, NULL, display_suite_setup, display_before, NULL, NULL);
