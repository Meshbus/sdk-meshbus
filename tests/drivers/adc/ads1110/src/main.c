/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define ADS_NODE DT_NODELABEL(ads1110)

#define ADS1110_ST_DRDY BIT(7)
#define ADS1110_GAIN_MASK BIT_MASK(2)
#define ADS1110_DR_MASK (BIT_MASK(2) << 2)
#define ADS1110_DR_SHIFT 2

struct ads1110_emul_data {
	uint8_t config;
	uint8_t writes[16];
	uint8_t write_count;
	uint8_t busy_reads;
	uint8_t config_reads;
	int16_t sample;
};

static struct ads1110_emul_data ads_emul_data;
static const struct device *const ads_dev = DEVICE_DT_GET(ADS_NODE);

static void ads1110_emul_reset(void)
{
	ads_emul_data.config = 0x8c;
	ads_emul_data.write_count = 0;
	ads_emul_data.busy_reads = 0;
	ads_emul_data.config_reads = 0;
	ads_emul_data.sample = 0;
}

static int ads1110_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				 int addr)
{
	struct ads1110_emul_data *data = target->data;

	ARG_UNUSED(addr);

	if (num_msgs != 1) {
		return -EIO;
	}

	if ((msgs[0].flags & I2C_MSG_READ) != 0) {
		uint8_t status = data->config;

		if (msgs[0].len != 3U) {
			return -EIO;
		}

		if (data->busy_reads > 0U) {
			status |= ADS1110_ST_DRDY;
			data->busy_reads--;
		} else {
			status &= (uint8_t)~ADS1110_ST_DRDY;
		}

		sys_put_be16((uint16_t)data->sample, msgs[0].buf);
		msgs[0].buf[2] = status;
		data->config_reads++;
		return 0;
	}

	if (msgs[0].len != 1U) {
		return -EIO;
	}

	data->config = msgs[0].buf[0];
	if (data->write_count < ARRAY_SIZE(data->writes)) {
		data->writes[data->write_count++] = data->config;
	}

	return 0;
}

static int ads1110_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(target);
	ARG_UNUSED(parent);

	ads1110_emul_reset();
	return 0;
}

static const struct i2c_emul_api ads1110_emul_api = {
	.transfer = ads1110_emul_transfer,
};

EMUL_DT_DEFINE(ADS_NODE, ads1110_emul_init, &ads_emul_data, NULL, &ads1110_emul_api, NULL);

static struct adc_channel_cfg ads1110_channel_cfg(enum adc_gain gain, uint16_t acq_time)
{
	return (struct adc_channel_cfg){
		.gain = gain,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = acq_time,
		.channel_id = 0,
		.differential = false,
		.input_positive = 0,
	};
}

static struct adc_channel_cfg ads1110_diff_channel_cfg(void)
{
	return (struct adc_channel_cfg){
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 0,
		.differential = true,
		.input_positive = 0,
		.input_negative = 1,
	};
}

static int ads1110_read_resolution(uint8_t resolution, int16_t *sample)
{
	struct adc_sequence sequence = {
		.channels = BIT(0),
		.buffer = sample,
		.buffer_size = sizeof(*sample),
		.resolution = resolution,
	};

	return adc_read(ads_dev, &sequence);
}

static void *ads1110_setup(void)
{
	zassert_true(device_is_ready(ads_dev));

	return NULL;
}

static void ads1110_before(void *fixture)
{
	struct adc_channel_cfg cfg = ads1110_channel_cfg(ADC_GAIN_1, ADC_ACQ_TIME_DEFAULT);

	ARG_UNUSED(fixture);

	ads1110_emul_reset();
	zassert_ok(adc_channel_setup(ads_dev, &cfg));
	ads_emul_data.write_count = 0;
}

ZTEST(ads1110, test_gain_mapping)
{
	const struct {
		enum adc_gain gain;
		uint8_t reg_gain;
	} cases[] = {
		{ADC_GAIN_1, 0},
		{ADC_GAIN_2, 1},
		{ADC_GAIN_4, 2},
		{ADC_GAIN_8, 3},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct adc_channel_cfg cfg =
			ads1110_channel_cfg(cases[i].gain, ADC_ACQ_TIME_DEFAULT);

		ads1110_emul_reset();
		zassert_ok(adc_channel_setup(ads_dev, &cfg));
		zassert_equal(ads_emul_data.config & ADS1110_GAIN_MASK, cases[i].reg_gain);
	}
}

ZTEST(ads1110, test_unsupported_fractional_gain)
{
	struct adc_channel_cfg cfg = ads1110_channel_cfg(ADC_GAIN_1_2, ADC_ACQ_TIME_DEFAULT);

	zassert_equal(adc_channel_setup(ads_dev, &cfg), -EINVAL);
}

ZTEST(ads1110, test_differential_input_0_1_supported)
{
	struct adc_channel_cfg cfg = ads1110_diff_channel_cfg();

	zassert_ok(adc_channel_setup(ads_dev, &cfg));
}

ZTEST(ads1110, test_unsupported_input_selection)
{
	struct adc_channel_cfg cfg = ads1110_channel_cfg(ADC_GAIN_1, ADC_ACQ_TIME_DEFAULT);

	cfg.input_positive = 1;
	zassert_equal(adc_channel_setup(ads_dev, &cfg), -EINVAL);

	cfg = ads1110_diff_channel_cfg();
	cfg.input_negative = 0;
	zassert_equal(adc_channel_setup(ads_dev, &cfg), -EINVAL);
}

ZTEST(ads1110, test_resolution_selects_data_rate_and_starts_conversion)
{
	const struct {
		uint8_t resolution;
		uint8_t dr;
	} cases[] = {
		{12, 0},
		{14, 1},
		{15, 2},
		{16, 3},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		int16_t sample = 0;
		uint8_t start_config;

		ads_emul_data.write_count = 0;
		ads_emul_data.sample = (int16_t)(0x1200 + i);
		zassert_ok(ads1110_read_resolution(cases[i].resolution, &sample));
		zassert_equal(sample, ads_emul_data.sample);
		zassert_true(ads_emul_data.write_count >= 1U);
		start_config = ads_emul_data.writes[ads_emul_data.write_count - 1U];
		zassert_true((start_config & ADS1110_ST_DRDY) != 0);
		zassert_equal((start_config & ADS1110_DR_MASK) >> ADS1110_DR_SHIFT,
			      cases[i].dr);
	}
}

ZTEST(ads1110, test_explicit_acquisition_time_must_match_resolution)
{
	struct adc_channel_cfg cfg =
		ads1110_channel_cfg(ADC_GAIN_1, ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 0));
	int16_t sample = 0;

	zassert_ok(adc_channel_setup(ads_dev, &cfg));
	zassert_equal(ads1110_read_resolution(16, &sample), -EINVAL);
	zassert_ok(ads1110_read_resolution(12, &sample));
}

ZTEST(ads1110, test_ready_busy_then_ready)
{
	int16_t sample = 0;

	ads_emul_data.sample = 0x3456;
	ads_emul_data.busy_reads = 2;
	zassert_ok(ads1110_read_resolution(12, &sample));
	zassert_equal(sample, 0x3456);
	zassert_true(ads_emul_data.config_reads >= 3U);
}

ZTEST(ads1110, test_ready_timeout)
{
	int16_t sample = 0;

	ads_emul_data.busy_reads = UINT8_MAX;
	zassert_equal(ads1110_read_resolution(12, &sample), -ETIMEDOUT);
}

ZTEST(ads1110, test_async_read)
{
	struct k_poll_signal signal;
	struct k_poll_event event;
	struct adc_sequence sequence;
	int16_t sample = 0;
	int signaled;
	int result;

	k_poll_signal_init(&signal);
	event = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
							     K_POLL_MODE_NOTIFY_ONLY,
							     &signal);
	sequence = (struct adc_sequence){
		.channels = BIT(0),
		.buffer = &sample,
		.buffer_size = sizeof(sample),
		.resolution = 12,
	};

	ads_emul_data.sample = 0x1234;
	zassert_ok(adc_read_async(ads_dev, &sequence, &signal));
	zassert_ok(k_poll(&event, 1, K_SECONDS(1)));
	k_poll_signal_check(&signal, &signaled, &result);
	zassert_true(signaled);
	zassert_ok(result);
	zassert_equal(sample, 0x1234);
}

ZTEST_SUITE(ads1110, NULL, ads1110_setup, ads1110_before, NULL, NULL);
