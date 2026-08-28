#define DT_DRV_COMPAT ion_mini_pid

#include <limits.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "mini_pid.h"

LOG_MODULE_REGISTER(mini_pid, CONFIG_SENSOR_LOG_LEVEL);

static int mini_pid_attr_set(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, const struct sensor_value *val)
{
	struct mini_pid_data *data = dev->data;
	int32_t value;

	if (val == NULL || val->val2 != 0) {
		return -EINVAL;
	}

	if (chan != SENSOR_CHAN_VOC) {
		return -ENOTSUP;
	}

	switch ((uint32_t)attr) {
	case SENSOR_ATTR_MINI_PID_BASELINE:
		value = MAX(val->val1, 0);
		k_mutex_lock(&data->lock, K_FOREVER);
		data->baseline = value;
		k_mutex_unlock(&data->lock);
		LOG_DBG("MINI-PID baseline updated: %d uV", value);
		return 0;
	case SENSOR_ATTR_MINI_PID_SENSITIVITY:
		value = val->val1;
		if (value <= 0) {
			return -EINVAL;
		}
		k_mutex_lock(&data->lock, K_FOREVER);
		data->sensitivity = value;
		k_mutex_unlock(&data->lock);
		LOG_DBG("MINI-PID sensitivity updated: %d uV/ppm", value);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mini_pid_attr_get(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, struct sensor_value *val)
{
	struct mini_pid_data *data = dev->data;

	if (val == NULL) {
		return -EINVAL;
	}

	if (chan != SENSOR_CHAN_VOC) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	switch ((uint32_t)attr) {
	case SENSOR_ATTR_MINI_PID_BASELINE:
		val->val1 = data->baseline;
		val->val2 = 0;
		break;
	case SENSOR_ATTR_MINI_PID_SENSITIVITY:
		val->val1 = data->sensitivity;
		val->val2 = 0;
		break;
	default:
		k_mutex_unlock(&data->lock);
		return -ENOTSUP;
	}

	k_mutex_unlock(&data->lock);
	return 0;
}

static int mini_pid_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct mini_pid_config *cfg = dev->config;
	struct mini_pid_data *data = dev->data;
	union {
		int16_t raw16;
		int32_t raw32;
	} sample_raw = {0};
	size_t raw_buffer_size = (cfg->adc.resolution > 16U) ? sizeof(int32_t) : sizeof(int16_t);
	struct adc_sequence sequence = {
		.buffer = &sample_raw,
		.buffer_size = raw_buffer_size,
	};
	uint16_t samples = MAX(cfg->averaging_samples, 1U);
	int64_t voc_micro_ppm = 0;
	int64_t acc_uv = 0;
	int32_t avg_uv;
	int32_t diff_uv;
	int ret;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_VOC) &&
	    (chan != SENSOR_CHAN_VOLTAGE)) {
		return -ENOTSUP;
	}

	if (!device_is_ready(cfg->adc.dev)) {
		LOG_ERR("ADC device %s not ready", cfg->adc.dev->name);
		return -ENODEV;
	}

	ret = adc_sequence_init_dt(&cfg->adc, &sequence);
	if (ret != 0) {
		LOG_ERR("Failed to init ADC sequence (%d)", ret);
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	for (uint16_t i = 0U; i < samples; i++) {
		int32_t sample_uv;

		sample_raw.raw32 = 0;

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
	data->voltage = avg_uv;

	diff_uv = avg_uv - data->baseline;
	if (diff_uv < 0) {
		diff_uv = 0;
	}

	if (data->sensitivity > 0) {
		voc_micro_ppm =
			DIV_ROUND_CLOSEST((int64_t)diff_uv * 1000000LL, data->sensitivity);
	}

	if (voc_micro_ppm < 0) {
		voc_micro_ppm = 0;
	} else if (voc_micro_ppm > ((int64_t)INT32_MAX * 1000000LL + 999999LL)) {
		voc_micro_ppm = (int64_t)INT32_MAX * 1000000LL + 999999LL;
	}

	data->voc_micro_ppm = voc_micro_ppm;
	data->sample_valid = true;
	k_mutex_unlock(&data->lock);

	LOG_DBG("MINI-PID sample fetched: voltage=%d uV, VOC=%lld micro-ppm",
		avg_uv, (long long)voc_micro_ppm);

	return 0;
}

static int mini_pid_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	struct mini_pid_data *data = dev->data;
	int ret;

	if (val == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->sample_valid) {
		k_mutex_unlock(&data->lock);
		return -EAGAIN;
	}

	switch (chan) {
	case SENSOR_CHAN_VOLTAGE:
		ret = sensor_value_from_micro(val, data->voltage);
		break;
	case SENSOR_CHAN_VOC:
		ret = sensor_value_from_micro(val, data->voc_micro_ppm);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int mini_pid_init(const struct device *dev)
{
	const struct mini_pid_config *cfg = dev->config;
	struct mini_pid_data *data = dev->data;
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
	data->voltage = 0;
	data->voc_micro_ppm = 0;
	data->sample_valid = false;
	data->baseline = cfg->baseline_default < 0 ? 0 : cfg->baseline_default;
	if (cfg->baseline_default < 0) {
		LOG_WRN("MINI-PID baseline default is negative; clamping to zero");
	}

	if (cfg->sensitivity_default <= 0) {
		LOG_WRN("MINI-PID sensitivity default must be positive; using 1 uV/ppm");
		data->sensitivity = 1;
	} else {
		data->sensitivity = cfg->sensitivity_default;
	}

	LOG_INF("MINI-PID sensor initialized: baseline=%d uV, sensitivity=%d uV/ppm, avg=%u",
		data->baseline, data->sensitivity, cfg->averaging_samples);

	return 0;
}

static DEVICE_API(sensor, mini_pid_api) = {
	.attr_set = mini_pid_attr_set,
	.attr_get = mini_pid_attr_get,
	.sample_fetch = mini_pid_sample_fetch,
	.channel_get = mini_pid_channel_get,
};

#define MINI_PID_DEFINE(inst)                                                                     \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, averaging_samples, 1) > 0,                           \
		     "averaging-samples must be greater than 0");                                \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, baseline, 0) >= 0,                                  \
		     "baseline must be non-negative");                                         \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, baseline, 0) <= INT32_MAX,                          \
		     "baseline exceeds INT32_MAX");                                            \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, sensitivity, 1) > 0,                                \
		     "sensitivity must be positive");                                          \
	BUILD_ASSERT(DT_INST_PROP_OR(inst, sensitivity, 1) <= INT32_MAX,                       \
		     "sensitivity exceeds INT32_MAX");                                        \
                                                                                                  \
	static struct mini_pid_data mini_pid_data_##inst;                                      \
                                                                                                  \
	static const struct mini_pid_config mini_pid_config_##inst = {                         \
		.adc = ADC_DT_SPEC_INST_GET(inst),                                             \
		.baseline_default = (int32_t)DT_INST_PROP_OR(inst, baseline, 0),               \
		.sensitivity_default = (int32_t)DT_INST_PROP_OR(inst, sensitivity, 1),         \
		.averaging_samples = DT_INST_PROP_OR(inst, averaging_samples, 1),              \
	};                                                                                        \
                                                                                                  \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, mini_pid_init, NULL, &mini_pid_data_##inst,          \
				     &mini_pid_config_##inst, POST_KERNEL,                         \
				     CONFIG_SENSOR_INIT_PRIORITY, &mini_pid_api);

DT_INST_FOREACH_STATUS_OKAY(MINI_PID_DEFINE)
