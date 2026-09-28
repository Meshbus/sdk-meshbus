/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/llext/symbol.h>
#include <llext/metadata.h>

#include "app_api.h"

static const struct mbs_llext_app_metadata metadata MBS_LLEXT_APP_METADATA_ATTR = {
	.magic = MBS_LLEXT_APP_METADATA_MAGIC,
	.metadata_version = MBS_LLEXT_APP_METADATA_VERSION,
	.size = sizeof(struct mbs_llext_app_metadata),
	.stack_size = 2048,
	.heap_size = 32768,
	.icon_data_size = MBS_LLEXT_APP_ICON_DATA_SIZE,
	.id = "mbs_peripherals",
	.name = "Native peripherals",
	.app_version = "1.0.0",
	.entry_point_symbol = "peripheral_entry",
	.edk_version = "0.1.0",
	.target = CONFIG_BOARD_TARGET,
};

void peripheral_entry(void *args)
{
	struct peripheral_result *result = args;
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(llext_i2c));
	const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(llext_spi));
	const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(llext_gpio));
	const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(llext_adc));
	uint8_t reg = 0x17;
	uint8_t tx = 0x3c;
	const struct spi_config spi_config = {
		.frequency = 1000000,
		.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	};
	const struct spi_buf tx_buf = {.buf = &tx, .len = 1};
	const struct spi_buf rx_buf = {.buf = &result->spi_value, .len = 1};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	const struct adc_channel_cfg channel = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 0,
	};
	const struct adc_sequence sequence = {
		.channels = BIT(0),
		.buffer = &result->adc_value,
		.buffer_size = sizeof(result->adc_value),
		.resolution = 12,
	};

	result->ready_mask = device_is_ready(i2c) | (device_is_ready(spi) << 1) |
		(device_is_ready(gpio) << 2) | (device_is_ready(adc) << 3);
	if (result->ready_mask != 0xf) {
		return;
	}
	result->i2c_rc = i2c_write_read(i2c, 0x42, &reg, 1, &result->i2c_value, 1);
	result->spi_rc = spi_transceive(spi, &spi_config, &tx_set, &rx_set);
	result->gpio_rc = gpio_pin_configure(gpio, 3, GPIO_OUTPUT_INACTIVE);
	if (result->gpio_rc == 0) {
		result->gpio_rc = gpio_pin_set(gpio, 3, 1);
		result->gpio_value = gpio_pin_get(gpio, 3);
	}
	result->adc_setup_rc = adc_channel_setup(adc, &channel);
	result->adc_read_rc = adc_read(adc, &sequence);
	result->adc_mv = result->adc_value;
	result->adc_mv_rc = adc_raw_to_millivolts(900, ADC_GAIN_1_4, 12, &result->adc_mv);
}

LL_EXTENSION_SYMBOL(peripheral_entry);
