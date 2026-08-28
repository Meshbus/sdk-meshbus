/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 * Copyright (c) 2020 Innoseis BV
 * Copyright (c) 2023 Cruise LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/util.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER 1
#include "adc_context.h"

#define DT_DRV_COMPAT ti_ads1110

LOG_MODULE_REGISTER(ADS1110, CONFIG_ADC_LOG_LEVEL);

#define ADS1110_CONFIG_GAIN(x) ((x)&BIT_MASK(2))
#define ADS1110_CONFIG_DR(x)   (((x)&BIT_MASK(2)) << 2)
#define ADS1110_CONFIG_CM(x)   (((x)&BIT_MASK(1)) << 4)

#define ADS1110_CONFIG_MASK_READY BIT(7)

#define ADS1110_DEFAULT_CONFIG 0x8C
#define ADS1110_REF_INTERNAL   2048
#define ADS1110_READY_POLL_US  100
#define ADS1110_READY_MARGIN_US 1000
#define ADS1110_TIMEOUT_MARGIN_US 50000

enum ads1110_reg {
	ADS1110_REG_OUTPUT = 0,
	ADS1110_REG_CONFIG = 1,
};

enum {
	ADS1110_CONFIG_DR_RATE_240_RES_12 = 0,
	ADS1110_CONFIG_DR_RATE_60_RES_14 = 1,
	ADS1110_CONFIG_DR_RATE_30_RES_15 = 2,
	ADS1110_CONFIG_DR_RATE_15_RES_16 = 3,
	ADS1110_CONFIG_DR_DEFAULT = ADS1110_CONFIG_DR_RATE_15_RES_16,
};

enum {
	ADS1110_CONFIG_GAIN_1 = 0,
	ADS1110_CONFIG_GAIN_2 = 1,
	ADS1110_CONFIG_GAIN_4 = 2,
	ADS1110_CONFIG_GAIN_8 = 3,
};

enum {
	ADS1110_CONFIG_CM_SINGLE = 0,
	ADS1110_CONFIG_CM_CONTINUOUS = 1,
};

struct ads1110_config {
	const struct i2c_dt_spec bus;
};

struct ads1110_data {
	struct adc_context ctx;
	k_timeout_t ready_time;
	k_timeout_t ready_timeout;
	struct k_sem acq_sem;
	K_KERNEL_STACK_MEMBER(acq_thread_stack, CONFIG_ADC_ADS1110_ACQUISITION_THREAD_STACK_SIZE);
	struct k_thread acq_thread;
	const struct device *dev;
	int16_t *buffer;
	int16_t *buffer_ptr;
	uint8_t channel_config;
	uint8_t active_config;
	uint8_t resolution;
	bool differential;
	bool channel_configured;
};

static int ads1110_read_async(const struct device *dev, const struct adc_sequence *sequence,
			      struct k_poll_signal *async);

static int ads1110_read_reg(const struct device *dev, enum ads1110_reg reg_addr, uint8_t *reg_val)
{
	const struct ads1110_config *config = dev->config;
	uint8_t buf[3] = {0};
	int rc = i2c_read_dt(&config->bus, buf, sizeof(buf));

	if (rc != 0) {
		return rc;
	}

	if (reg_addr == ADS1110_REG_OUTPUT) {
		reg_val[0] = buf[0];
		reg_val[1] = buf[1];
	} else {
		reg_val[0] = buf[2];
	}

	return rc;
}

static int ads1110_write_reg(const struct device *dev, uint8_t reg)
{
	uint8_t msg[1] = {reg};
	const struct ads1110_config *config = dev->config;

	/* It's only possible to write the config register, so the ADS1110
	 * assumes all writes are going to that register and omits the register
	 * parameter from write transactions
	 */
	return i2c_write_dt(&config->bus, msg, sizeof(msg));
}

static uint8_t ads1110_dr_to_resolution(uint8_t dr)
{
	switch (dr) {
	case ADS1110_CONFIG_DR_RATE_240_RES_12:
		return 12;
	case ADS1110_CONFIG_DR_RATE_60_RES_14:
		return 14;
	case ADS1110_CONFIG_DR_RATE_30_RES_15:
		return 15;
	case ADS1110_CONFIG_DR_RATE_15_RES_16:
	default:
		return 16;
	}
}

static int ads1110_resolution_to_dr(uint8_t resolution)
{
	switch (resolution) {
	case 12:
		return ADS1110_CONFIG_DR_RATE_240_RES_12;
	case 14:
		return ADS1110_CONFIG_DR_RATE_60_RES_14;
	case 15:
		return ADS1110_CONFIG_DR_RATE_30_RES_15;
	case 16:
		return ADS1110_CONFIG_DR_RATE_15_RES_16;
	default:
		return -EINVAL;
	}
}

static uint32_t ads1110_dr_ready_time_us(uint8_t dr)
{
	switch (dr) {
	case ADS1110_CONFIG_DR_RATE_15_RES_16:
		return USEC_PER_SEC / 15U;
	case ADS1110_CONFIG_DR_RATE_30_RES_15:
		return USEC_PER_SEC / 30U;
	case ADS1110_CONFIG_DR_RATE_60_RES_14:
		return USEC_PER_SEC / 60U;
	case ADS1110_CONFIG_DR_RATE_240_RES_12:
	default:
		return USEC_PER_SEC / 240U;
	}
}

static int ads1110_acq_time_to_dr(uint16_t acq_time)
{
	uint16_t acq_value = ADC_ACQ_TIME_VALUE(acq_time);

	if (acq_time == ADC_ACQ_TIME_DEFAULT) {
		return -ENOENT;
	}

	if (ADC_ACQ_TIME_UNIT(acq_time) != ADC_ACQ_TIME_TICKS) {
		return -EINVAL;
	}

	switch (acq_value) {
	case ADS1110_CONFIG_DR_RATE_240_RES_12:
	case ADS1110_CONFIG_DR_RATE_60_RES_14:
	case ADS1110_CONFIG_DR_RATE_30_RES_15:
	case ADS1110_CONFIG_DR_RATE_15_RES_16:
		return acq_value;
	default:
		return -EINVAL;
	}
}

static void ads1110_set_ready_timing(struct ads1110_data *data, uint8_t dr)
{
	uint32_t ready_time_us = ads1110_dr_ready_time_us(dr);

	data->ready_time = K_USEC(ready_time_us + ADS1110_READY_MARGIN_US);
	data->ready_timeout = K_USEC(ready_time_us + ADS1110_TIMEOUT_MARGIN_US);
}

static int ads1110_start_conversion(const struct device *dev)
{
	struct ads1110_data *data = dev->data;

	return ads1110_write_reg(dev, data->active_config | ADS1110_CONFIG_MASK_READY);
}

static int ads1110_wait_data_ready(const struct device *dev)
{
	int rc;
	struct ads1110_data *data = dev->data;
	k_timepoint_t deadline;
	uint8_t status = 0;

	k_sleep(data->ready_time);

	rc = ads1110_read_reg(dev, ADS1110_REG_CONFIG, &status);
	if (rc != 0) {
		return rc;
	}

	deadline = sys_timepoint_calc(data->ready_timeout);
	while ((status & ADS1110_CONFIG_MASK_READY) != 0) {
		if (sys_timepoint_expired(deadline)) {
			return -ETIMEDOUT;
		}

		k_sleep(K_USEC(ADS1110_READY_POLL_US));
		rc = ads1110_read_reg(dev, ADS1110_REG_CONFIG, &status);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static int ads1110_read_sample(const struct device *dev, int16_t *buff)
{
	int res;
	uint8_t sample[2] = {0};

	res = ads1110_read_reg(dev, ADS1110_REG_OUTPUT, sample);
	buff[0] = (int16_t)sys_get_be16(sample);
	return res;
}

static int ads1110_channel_setup(const struct device *dev,
				 const struct adc_channel_cfg *channel_cfg)
{
	struct ads1110_data *data = dev->data;
	uint8_t config = 0;
	int dr;

	if (channel_cfg->channel_id != 0) {
		LOG_ERR("unsupported channel id '%d'", channel_cfg->channel_id);
		return -ENOTSUP;
	}

	if (channel_cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("unsupported channel reference type '%d'", channel_cfg->reference);
		return -ENOTSUP;
	}

	if (channel_cfg->differential &&
	    ((channel_cfg->input_positive != 0U) || (channel_cfg->input_negative != 1U))) {
		LOG_ERR("unsupported differential inputs +%u -%u",
			channel_cfg->input_positive, channel_cfg->input_negative);
		return -EINVAL;
	}

	if (!channel_cfg->differential && (channel_cfg->input_positive != 0U)) {
		LOG_ERR("unsupported single-ended input %u", channel_cfg->input_positive);
		return -EINVAL;
	}

	data->differential = channel_cfg->differential;

	dr = ads1110_acq_time_to_dr(channel_cfg->acquisition_time);
	if (dr < 0 && dr != -ENOENT) {
		return dr;
	}

	if (dr >= 0) {
		data->resolution = ads1110_dr_to_resolution((uint8_t)dr);
		config |= ADS1110_CONFIG_DR((uint8_t)dr);
	} else {
		data->resolution = 0U;
	}

	switch (channel_cfg->gain) {
	case ADC_GAIN_1:
		config |= ADS1110_CONFIG_GAIN(ADS1110_CONFIG_GAIN_1);
		break;
	case ADC_GAIN_2:
		config |= ADS1110_CONFIG_GAIN(ADS1110_CONFIG_GAIN_2);
		break;
	case ADC_GAIN_4:
		config |= ADS1110_CONFIG_GAIN(ADS1110_CONFIG_GAIN_4);
		break;
	case ADC_GAIN_8:
		config |= ADS1110_CONFIG_GAIN(ADS1110_CONFIG_GAIN_8);
		break;
	default:
		return -EINVAL;
	}

	config |= ADS1110_CONFIG_CM(ADS1110_CONFIG_CM_SINGLE); /* Only single shot supported */
	data->channel_config = config;
	data->channel_configured = true;

	return ads1110_write_reg(dev, config);
}

static int ads1110_validate_buffer_size(const struct adc_sequence *sequence)
{
	size_t needed = sizeof(int16_t);

	if (sequence->options) {
		needed *= (1 + sequence->options->extra_samplings);
	}

	if (sequence->buffer_size < needed) {
		LOG_ERR("Insufficient buffer %i < %i", sequence->buffer_size, needed);
		return -ENOMEM;
	}

	return 0;
}

static int ads1110_validate_sequence(const struct device *dev, const struct adc_sequence *sequence)
{
	struct ads1110_data *data = dev->data;
	int dr;

	if (!data->channel_configured) {
		return -EINVAL;
	}

	if (sequence->channels != BIT(0)) {
		LOG_ERR("Invalid Channel 0x%x", sequence->channels);
		return -EINVAL;
	}

	if (sequence->oversampling) {
		LOG_ERR("Oversampling not supported");
		return -EINVAL;
	}

	dr = ads1110_resolution_to_dr(sequence->resolution);
	if (dr < 0) {
		LOG_ERR("unsupported resolution %d", sequence->resolution);
		return -EINVAL;
	}

	if ((data->resolution != 0U) && (data->resolution != sequence->resolution)) {
		LOG_ERR("resolution %d does not match channel acquisition-time resolution %d",
			sequence->resolution, data->resolution);
		return -EINVAL;
	}

	return ads1110_validate_buffer_size(sequence);
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, bool repeat_sampling)
{
	struct ads1110_data *data = CONTAINER_OF(ctx, struct ads1110_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->buffer_ptr;
	}
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct ads1110_data *data = CONTAINER_OF(ctx, struct ads1110_data, ctx);
	int rc;

	data->buffer_ptr = data->buffer;
	rc = ads1110_start_conversion(data->dev);
	if (rc != 0) {
		adc_context_complete(ctx, rc);
		return;
	}

	k_sem_give(&data->acq_sem);
}

static int ads1110_adc_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
	int rc;
	struct ads1110_data *data = dev->data;
	int dr;

	rc = ads1110_validate_sequence(dev, sequence);
	if (rc != 0) {
		return rc;
	}

	dr = ads1110_resolution_to_dr(sequence->resolution);
	data->active_config = data->channel_config;
	data->active_config &= (uint8_t)~ADS1110_CONFIG_DR(BIT_MASK(2));
	data->active_config |= ADS1110_CONFIG_DR((uint8_t)dr);
	ads1110_set_ready_timing(data, (uint8_t)dr);
	data->buffer = sequence->buffer;

	adc_context_start_read(&data->ctx, sequence);

	return adc_context_wait_for_completion(&data->ctx);
}

static int ads1110_adc_perform_read(const struct device *dev)
{
	int rc;
	struct ads1110_data *data = dev->data;

	k_sem_take(&data->acq_sem, K_FOREVER);

	rc = ads1110_wait_data_ready(dev);
	if (rc != 0) {
		adc_context_complete(&data->ctx, rc);
		return rc;
	}

	rc = ads1110_read_sample(dev, data->buffer);
	if (rc != 0) {
		adc_context_complete(&data->ctx, rc);
		return rc;
	}
	data->buffer++;

	adc_context_on_sampling_done(&data->ctx, dev);

	return rc;
}

static int ads1110_read(const struct device *dev, const struct adc_sequence *sequence)
{
	return ads1110_read_async(dev, sequence, NULL);
}

static int ads1110_read_async(const struct device *dev, const struct adc_sequence *sequence,
			      struct k_poll_signal *async)
{
	int rc;
	struct ads1110_data *data = dev->data;

	adc_context_lock(&data->ctx, async ? true : false, async);
	rc = ads1110_adc_start_read(dev, sequence);
	adc_context_release(&data->ctx, rc);
	return rc;
}

static void ads1110_acquisition_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = p1;

	while (true) {
		(void)ads1110_adc_perform_read(dev);
	}
}

static int ads1110_init(const struct device *dev)
{
	int rc = 0;
	const struct ads1110_config *config = dev->config;
	struct ads1110_data *data = dev->data;

	adc_context_init(&data->ctx);
	data->dev = dev;
	data->resolution = 0U;
	data->channel_config = ADS1110_CONFIG_CM(ADS1110_CONFIG_CM_SINGLE) |
			       ADS1110_CONFIG_DR(ADS1110_CONFIG_DR_DEFAULT) |
			       ADS1110_CONFIG_GAIN(ADS1110_CONFIG_GAIN_1);
	data->active_config = data->channel_config;
	ads1110_set_ready_timing(data, ADS1110_CONFIG_DR_DEFAULT);

	k_sem_init(&data->acq_sem, 0, 1);

	if (!device_is_ready(config->bus.bus)) {
		return -ENODEV;
	}

	rc = ads1110_write_reg(dev, ADS1110_DEFAULT_CONFIG);
	if (rc) {
		LOG_ERR("Could not set default config 0x%x", ADS1110_DEFAULT_CONFIG);
		return rc;
	}

	adc_context_unlock_unconditionally(&data->ctx);

	k_thread_create(&data->acq_thread, data->acq_thread_stack,
			CONFIG_ADC_ADS1110_ACQUISITION_THREAD_STACK_SIZE,
			ads1110_acquisition_thread, (void *)dev, NULL, NULL,
			CONFIG_ADC_ADS1110_ACQUISITION_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&data->acq_thread, "adc_ads1110");

	return rc;
}

static DEVICE_API(adc, api) = {
	.channel_setup = ads1110_channel_setup,
	.read = ads1110_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = ads1110_read_async,
#endif
	.ref_internal = ADS1110_REF_INTERNAL,
};
#define ADC_ADS1110_INST_DEFINE(n)                                                                 \
	static const struct ads1110_config config_##n = {.bus = I2C_DT_SPEC_INST_GET(n)};  \
	static struct ads1110_data data_##n;                                                       \
	DEVICE_DT_INST_DEFINE(n, ads1110_init, NULL, &data_##n, &config_##n, POST_KERNEL,          \
			      CONFIG_ADC_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(ADC_ADS1110_INST_DEFINE);
