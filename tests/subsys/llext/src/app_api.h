/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef MBS_LLEXT_TEST_APP_API_H_
#define MBS_LLEXT_TEST_APP_API_H_

#include <stdint.h>

struct peripheral_result {
	int ready_mask;
	int i2c_rc;
	int spi_rc;
	int gpio_rc;
	int gpio_value;
	int adc_setup_rc;
	int adc_read_rc;
	int adc_mv_rc;
	int32_t adc_mv;
	uint8_t i2c_value;
	uint8_t spi_value;
	int16_t adc_value;
};

#define MBS_LLEXT_TEST_EVT_APP 3

int mbs_llext_test_hook(int event);

#endif /* MBS_LLEXT_TEST_APP_API_H_ */
