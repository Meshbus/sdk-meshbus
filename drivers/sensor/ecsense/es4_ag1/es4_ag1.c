#define DT_DRV_COMPAT ecsense_es4_ag1

#include <limits.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "es4_ag1.h"

LOG_MODULE_REGISTER(es4_ag1, CONFIG_SENSOR_LOG_LEVEL);

static int es4_ag1_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, const struct sensor_value *val)
{
	struct es4_ag1_data *data = dev->data;
	int64_t value;

	if (val == NULL) {
		return -EINVAL;
	}

	if (chan != SENSOR_CHAN_VOC) {
		return -ENOTSUP;
	}

	if (val->val2 != 0) {
		return -EINVAL;
	}

	value = val->val1;

	switch ((uint32_t)attr) {
	case SENSOR_ATTR_ES4_AG1_ZERO_OFFSET:
		if (value < INT32_MIN || value > INT32_MAX) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->zero_offset_uv = (int32_t)value;
		k_mutex_unlock(&data->lock);
		return 0;
	case SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE:
		if (value <= 0 || value > INT32_MAX) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->transimpedance_ohms = (int32_t)value;
		k_mutex_unlock(&data->lock);
		return 0;
	case SENSOR_ATTR_ES4_AG1_SENSITIVITY:
		if (value <= 0 || value > INT32_MAX) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->sensitivity_nanoamp_per_ppm = (int32_t)value;
		k_mutex_unlock(&data->lock);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int es4_ag1_attr_get(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, struct sensor_value *val)
{
	struct es4_ag1_data *data = dev->data;

	if (val == NULL) {
		return -EINVAL;
	}

	if (chan != SENSOR_CHAN_VOC) {
		return -ENOTSUP;
	}

	switch ((uint32_t)attr) {
	case SENSOR_ATTR_ES4_AG1_ZERO_OFFSET:
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->zero_offset_uv;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;
	case SENSOR_ATTR_ES4_AG1_TRANSIMPEDANCE:
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->transimpedance_ohms;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;
	case SENSOR_ATTR_ES4_AG1_SENSITIVITY:
		k_mutex_lock(&data->lock, K_FOREVER);
		val->val1 = data->sensitivity_nanoamp_per_ppm;
		val->val2 = 0;
		k_mutex_unlock(&data->lock);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int es4_ag1_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct es4_ag1_config *cfg = dev->config;
	struct es4_ag1_data *data = dev->data;
	int64_t acc_uv = 0;
	int64_t delta_uv;
	int64_t denom;
	int64_t voc_micro_ppm;
	uint16_t samples;
	int32_t avg_uv;
	size_t raw_buffer_size;
	int ret;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_VOC) &&
	    (chan != SENSOR_CHAN_VOLTAGE)) {
		return -ENOTSUP;
	}

	if (!device_is_ready(cfg->adc.dev)) {
		LOG_ERR("ADC device %s not ready", cfg->adc.dev->name);
		return -ENODEV;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	samples = MAX(cfg->averaging_samples, (uint16_t)1U);
	raw_buffer_size = (cfg->adc.resolution > 16U) ? sizeof(int32_t) : sizeof(int16_t);

	for (uint16_t i = 0; i < samples; i++) {
		union {
			int16_t raw16;
			int32_t raw32;
		} sample_raw = {0};
		int32_t sample_uv;
		struct adc_sequence sequence = {
			.buffer = &sample_raw,
			.buffer_size = raw_buffer_size,
		};

		ret = adc_sequence_init_dt(&cfg->adc, &sequence);
		if (ret < 0) {
			LOG_ERR("Failed to init ADC sequence (%d)", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		ret = adc_read_dt(&cfg->adc, &sequence);
		if (ret < 0) {
			LOG_ERR("ADC read failed (%d)", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		sample_uv = (cfg->adc.resolution > 16U) ? sample_raw.raw32 : sample_raw.raw16;
		ret = adc_raw_to_microvolts_dt(&cfg->adc, &sample_uv);
		if (ret < 0) {
			LOG_ERR("ADC conversion failed (%d)", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		acc_uv += sample_uv;
	}

	avg_uv = (int32_t)DIV_ROUND_CLOSEST(acc_uv, samples);
	data->avg_uv = avg_uv;

	if (cfg->inverted_polarity) {
		delta_uv = (int64_t)data->zero_offset_uv - avg_uv;
	} else {
		delta_uv = (int64_t)avg_uv - data->zero_offset_uv;
	}

	if (delta_uv < 0) {
		delta_uv = 0;
	}

	denom = (int64_t)data->transimpedance_ohms * data->sensitivity_nanoamp_per_ppm;
	voc_micro_ppm = DIV_ROUND_CLOSEST(delta_uv * 1000000000LL, denom);

	if (voc_micro_ppm < 0) {
		voc_micro_ppm = 0;
	} else if (voc_micro_ppm > ((int64_t)INT32_MAX * 1000000LL + 999999LL)) {
		voc_micro_ppm = (int64_t)INT32_MAX * 1000000LL + 999999LL;
	}

	data->voc_micro_ppm = voc_micro_ppm;
	data->sample_valid = true;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int es4_ag1_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct es4_ag1_data *data = dev->data;
	int ret;

	if (val == NULL) {
		return -EINVAL;
	}

	switch (chan) {
	case SENSOR_CHAN_VOLTAGE:
		k_mutex_lock(&data->lock, K_FOREVER);
		if (!data->sample_valid) {
			k_mutex_unlock(&data->lock);
			return -EAGAIN;
		}
		ret = sensor_value_from_micro(val, data->avg_uv);
		k_mutex_unlock(&data->lock);
		return ret;
	case SENSOR_CHAN_VOC:
		k_mutex_lock(&data->lock, K_FOREVER);
		if (!data->sample_valid) {
			k_mutex_unlock(&data->lock);
			return -EAGAIN;
		}
		ret = sensor_value_from_micro(val, data->voc_micro_ppm);
		k_mutex_unlock(&data->lock);
		return ret;
	default:
		return -ENOTSUP;
	}
}

static int es4_ag1_init(const struct device *dev)
{
	const struct es4_ag1_config *cfg = dev->config;
	struct es4_ag1_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->adc.dev)) {
		LOG_ERR("ADC device %s not ready", cfg->adc.dev->name);
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&cfg->adc);
	if (ret < 0) {
		LOG_ERR("Failed to setup ADC channel (%d)", ret);
		return ret;
	}

	k_mutex_init(&data->lock);
	data->avg_uv = 0;
	data->voc_micro_ppm = 0;
	data->zero_offset_uv = cfg->zero_offset_uv_default;
	data->transimpedance_ohms = cfg->transimpedance_ohms_default;
	data->sensitivity_nanoamp_per_ppm = cfg->sensitivity_nanoamp_per_ppm_default;
	data->sample_valid = false;

	LOG_INF("ES4-AG1 initialized: zero=%d uV, tia=%d ohm, sensitivity=%d nA/ppm, avg=%u, inverted=%d",
		data->zero_offset_uv, data->transimpedance_ohms,
		data->sensitivity_nanoamp_per_ppm, cfg->averaging_samples,
		cfg->inverted_polarity);

	return 0;
}

static DEVICE_API(sensor, es4_ag1_api) = {
	.attr_set = es4_ag1_attr_set,
	.attr_get = es4_ag1_attr_get,
	.sample_fetch = es4_ag1_sample_fetch,
	.channel_get = es4_ag1_channel_get,
};

#define ES4_AG1_DEFINE(inst)                                                                     \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, averaging_samples, 16) > 0,                          \
		     "averaging-samples must be greater than 0");                                 \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, averaging_samples, 16) <= UINT16_MAX,                \
		     "averaging-samples exceeds UINT16_MAX");                                    \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, zero_offset_microvolt, 0) >= INT32_MIN,              \
		     "zero-offset-microvolt below INT32_MIN");                                  \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, zero_offset_microvolt, 0) <= INT32_MAX,              \
		     "zero-offset-microvolt exceeds INT32_MAX");                                 \
	BUILD_ASSERT(DT_INST_PROP(inst, transimpedance_ohms) > 0,                                \
		     "transimpedance-ohms must be positive");                                    \
	BUILD_ASSERT(DT_INST_PROP(inst, transimpedance_ohms) <= INT32_MAX,                       \
		     "transimpedance-ohms exceeds INT32_MAX");                                   \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, sensitivity_nanoamp_per_ppm, 55) > 0,                \
		     "sensitivity-nanoamp-per-ppm must be positive");                            \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, sensitivity_nanoamp_per_ppm, 55) <= INT32_MAX,       \
		     "sensitivity-nanoamp-per-ppm exceeds INT32_MAX");                           \
                                                                                                   \
	static struct es4_ag1_data es4_ag1_data_##inst;                                          \
                                                                                                   \
	static const struct es4_ag1_config es4_ag1_config_##inst = {                             \
		.adc = ADC_DT_SPEC_INST_GET(inst),                                               \
		.zero_offset_uv_default =                                                    \
			(int32_t)DT_INST_PROP_OR(inst, zero_offset_microvolt, 0),             \
		.transimpedance_ohms_default = (int32_t)DT_INST_PROP(inst, transimpedance_ohms), \
		.sensitivity_nanoamp_per_ppm_default =                                           \
			(int32_t)DT_INST_PROP_OR(inst, sensitivity_nanoamp_per_ppm, 55),      \
		.inverted_polarity = DT_INST_PROP_OR(inst, inverted_polarity, 0),                 \
		.averaging_samples = (uint16_t)DT_INST_PROP_OR(inst, averaging_samples, 16),     \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, es4_ag1_init, NULL, &es4_ag1_data_##inst,             \
				     &es4_ag1_config_##inst, POST_KERNEL,                         \
				     CONFIG_SENSOR_INIT_PRIORITY, &es4_ag1_api);

DT_INST_FOREACH_STATUS_OKAY(ES4_AG1_DEFINE)
