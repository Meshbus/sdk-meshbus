/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>

#if !defined(CONFIG_LLEXT_EXPORT_DEVICES) || !defined(CONFIG_I2C) || \
	!defined(CONFIG_SPI) || !defined(CONFIG_GPIO) || !defined(CONFIG_ADC)
#error "Export an EDK from a host with DT device exports and I2C/SPI/GPIO/ADC enabled"
#endif

/* C2 controllers. Adapt these nodes to the consuming board's devicetree. */
static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c21));
static const struct device *const spi = DEVICE_DT_GET(DT_NODELABEL(spi00));
static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc));

/* These recipes are compiled into the MBA, but only run when called below. */
int read_i2c_register(uint16_t address, uint8_t reg, uint8_t *value)
{
	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}
	return i2c_write_read(i2c, address, &reg, sizeof(reg), value, sizeof(*value));
}

int exchange_spi(const struct spi_config *config, const struct spi_buf_set *tx,
		 const struct spi_buf_set *rx)
{
	if (!device_is_ready(spi)) {
		return -ENODEV;
	}
	return spi_transceive(spi, config, tx, rx);
}

int set_gpio_output(gpio_pin_t pin, int value)
{
	if (!device_is_ready(gpio)) {
		return -ENODEV;
	}
	return gpio_pin_configure(gpio, pin, value ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW);
}

int sample_adc(const struct adc_channel_cfg *channel, uint8_t resolution, int16_t *value)
{
	struct adc_sequence sequence = {
		.channels = BIT(channel->channel_id),
		.buffer = value,
		.buffer_size = sizeof(*value),
		.resolution = resolution,
	};
	int rc;

	if (!device_is_ready(adc)) {
		return -ENODEV;
	}
	rc = adc_channel_setup(adc, channel);
	return rc == 0 ? adc_read(adc, &sequence) : rc;
}

void native_peripherals_main(void *args)
{
	ARG_UNUSED(args);
	printk("Native peripherals: I2C=%d SPI=%d GPIO=%d ADC=%d\n",
	       device_is_ready(i2c), device_is_ready(spi), device_is_ready(gpio),
	       device_is_ready(adc));
	/* Add calls to the recipes above for your wiring and peripheral protocol. */
}

LL_EXTENSION_SYMBOL(native_peripherals_main);
