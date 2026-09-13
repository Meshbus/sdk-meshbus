/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>

/* Test-owned drivers exercise MBA relocation and native API dispatch only. */
static int test_i2c_transfer(const struct device *dev, struct i2c_msg *msgs,
			     uint8_t count, uint16_t addr)
{
	ARG_UNUSED(dev);
	if (addr != 0x42 || count != 2 || msgs[0].len != 1 || msgs[1].len != 1 ||
	    msgs[0].buf[0] != 0x17 || (msgs[0].flags & I2C_MSG_READ) != 0 ||
	    (msgs[1].flags & I2C_MSG_READ) == 0) {
		return -EINVAL;
	}
	msgs[1].buf[0] = 0xa5;
	return 0;
}

static DEVICE_API(i2c, test_i2c_api) = {.transfer = test_i2c_transfer};
DEVICE_DT_DEFINE(DT_NODELABEL(llext_i2c), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 90, &test_i2c_api);

static int test_spi_transceive(const struct device *dev, const struct spi_config *config,
			       const struct spi_buf_set *tx, const struct spi_buf_set *rx)
{
	ARG_UNUSED(dev);
	if (config->frequency != 1000000 || SPI_WORD_SIZE_GET(config->operation) != 8 ||
	    tx == NULL || rx == NULL || tx->count != 1 || rx->count != 1 ||
	    tx->buffers[0].len != 1 || rx->buffers[0].len != 1) {
		return -EINVAL;
	}
	*(uint8_t *)rx->buffers[0].buf = *(const uint8_t *)tx->buffers[0].buf ^ 0xff;
	return 0;
}

static DEVICE_API(spi, test_spi_api) = {.transceive = test_spi_transceive};
DEVICE_DT_DEFINE(DT_NODELABEL(llext_spi), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 90, &test_spi_api);

struct test_gpio_data {
	struct gpio_driver_data common;
	gpio_port_value_t value;
};

static int test_gpio_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	struct test_gpio_data *data = dev->data;

	if (pin != 3 || (flags & GPIO_OUTPUT) == 0) {
		return -EINVAL;
	}
	data->value = (flags & GPIO_OUTPUT_INIT_HIGH) != 0 ? BIT(pin) : 0;
	return 0;
}

static int test_gpio_set(const struct device *dev, gpio_port_pins_t pins)
{
	struct test_gpio_data *data = dev->data;

	data->value |= pins;
	return 0;
}

static int test_gpio_clear(const struct device *dev, gpio_port_pins_t pins)
{
	struct test_gpio_data *data = dev->data;

	data->value &= ~pins;
	return 0;
}

static int test_gpio_get(const struct device *dev, gpio_port_value_t *value)
{
	struct test_gpio_data *data = dev->data;

	*value = data->value;
	return 0;
}

static struct test_gpio_data gpio_data;
static const struct gpio_driver_config gpio_config = {.port_pin_mask = BIT(3)};
static DEVICE_API(gpio, test_gpio_api) = {
	.pin_configure = test_gpio_configure,
	.port_set_bits_raw = test_gpio_set,
	.port_clear_bits_raw = test_gpio_clear,
	.port_get_raw = test_gpio_get,
};
DEVICE_DT_DEFINE(DT_NODELABEL(llext_gpio), NULL, NULL, &gpio_data, &gpio_config,
		 POST_KERNEL, 90, &test_gpio_api);

static int test_adc_setup(const struct device *dev, const struct adc_channel_cfg *cfg)
{
	ARG_UNUSED(dev);
	return cfg->channel_id == 0 && cfg->gain == ADC_GAIN_1 &&
		cfg->reference == ADC_REF_INTERNAL ? 0 : -EINVAL;
}

static int test_adc_read(const struct device *dev, const struct adc_sequence *sequence)
{
	ARG_UNUSED(dev);
	if (sequence->channels != BIT(0) || sequence->resolution != 12 ||
	    sequence->buffer_size < sizeof(int16_t)) {
		return -EINVAL;
	}
	*(int16_t *)sequence->buffer = 1234;
	return 0;
}

static DEVICE_API(adc, test_adc_api) = {
	.channel_setup = test_adc_setup,
	.read = test_adc_read,
};
DEVICE_DT_DEFINE(DT_NODELABEL(llext_adc), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 90, &test_adc_api);
