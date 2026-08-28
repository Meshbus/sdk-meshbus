/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_analog_light

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(analog_light, CONFIG_SENSOR_LOG_LEVEL);

struct analog_light_config {
	struct adc_dt_spec adc;
	uint16_t low_threshold_mv;
	uint16_t high_threshold_mv;
	uint16_t averaging_samples;
};

struct analog_light_data {
	struct k_mutex lock;
	int32_t voltage_mv;
	int32_t light_level;
};

static int analog_light_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct analog_light_config *cfg = dev->config;
	struct analog_light_data *data = dev->data;
	int64_t acc_mv = 0;
	uint16_t samples = MAX(cfg->averaging_samples, 1U);
	int ret;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_LIGHT) &&
	    (chan != SENSOR_CHAN_VOLTAGE)) {
		return -ENOTSUP;
	}

	if (!adc_is_ready_dt(&cfg->adc)) {
		LOG_ERR("ADC device %s not ready", cfg->adc.dev->name);
		return -ENODEV;
	}

	for (uint16_t i = 0U; i < samples; i++) {
		int16_t raw = 0;
		int32_t mv;
		struct adc_sequence sequence = {
			.buffer = &raw,
			.buffer_size = sizeof(raw),
		};

		ret = adc_sequence_init_dt(&cfg->adc, &sequence);
		if (ret != 0) {
			LOG_ERR("Failed to init ADC sequence (%d)", ret);
			return ret;
		}

		ret = adc_read_dt(&cfg->adc, &sequence);
		if (ret != 0) {
			LOG_ERR("ADC read failed (%d)", ret);
			return ret;
		}

		mv = raw;
		ret = adc_raw_to_millivolts_dt(&cfg->adc, &mv);
		if (ret != 0) {
			LOG_ERR("ADC conversion failed (%d)", ret);
			return ret;
		}

		acc_mv += mv;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->voltage_mv = (int32_t)(acc_mv / samples);

	if (data->voltage_mv <= cfg->low_threshold_mv) {
		data->light_level = 0;
	} else if (data->voltage_mv >= cfg->high_threshold_mv) {
		data->light_level = 100;
	} else {
		data->light_level = 100 * (data->voltage_mv - cfg->low_threshold_mv) /
				    (cfg->high_threshold_mv - cfg->low_threshold_mv);
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int analog_light_channel_get(const struct device *dev, enum sensor_channel chan,
				    struct sensor_value *val)
{
	struct analog_light_data *data = dev->data;
	int32_t value;

	if (val == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (chan) {
	case SENSOR_CHAN_LIGHT:
		value = data->light_level;
		break;
	case SENSOR_CHAN_VOLTAGE:
		value = data->voltage_mv;
		break;
	default:
		k_mutex_unlock(&data->lock);
		return -ENOTSUP;
	}
	k_mutex_unlock(&data->lock);

	val->val1 = value;
	val->val2 = 0;
	return 0;
}

static DEVICE_API(sensor, analog_light_api) = {
	.sample_fetch = analog_light_sample_fetch,
	.channel_get = analog_light_channel_get,
};

static int analog_light_init(const struct device *dev)
{
	const struct analog_light_config *cfg = dev->config;
	struct analog_light_data *data = dev->data;
	int ret;

	if (cfg->high_threshold_mv <= cfg->low_threshold_mv) {
		return -EINVAL;
	}

	if (!adc_is_ready_dt(&cfg->adc)) {
		LOG_ERR("ADC device %s not ready", cfg->adc.dev->name);
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&cfg->adc);
	if (ret != 0) {
		LOG_ERR("ADC channel setup failed (%d)", ret);
		return ret;
	}

	k_mutex_init(&data->lock);
	return 0;
}

#define ANALOG_LIGHT_DEFINE(inst)                                                                  \
	static struct analog_light_data analog_light_data_##inst;                                  \
                                                                                                   \
	static const struct analog_light_config analog_light_config_##inst = {                     \
		.adc = ADC_DT_SPEC_INST_GET(inst),                                                 \
		.low_threshold_mv = DT_INST_PROP(inst, low_threshold_mv),                           \
		.high_threshold_mv = DT_INST_PROP(inst, high_threshold_mv),                         \
		.averaging_samples = DT_INST_PROP(inst, averaging_samples),                         \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, analog_light_init, NULL, &analog_light_data_##inst,     \
				     &analog_light_config_##inst, POST_KERNEL,                       \
				     CONFIG_SENSOR_INIT_PRIORITY, &analog_light_api);

DT_INST_FOREACH_STATUS_OKAY(ANALOG_LIGHT_DEFINE)
