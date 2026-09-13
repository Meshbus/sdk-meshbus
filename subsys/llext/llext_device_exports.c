/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <zephyr/llext/symbol.h>

/* DEVICE_API_IS(), also used by CONFIG_DEVICE_API_ASSERT in inline driver
 * calls, compares the host API pointer with these linker-defined bounds.
 * Export them even without assertions so an MBA can explicitly check a class.
 */
#define MBS_LLEXT_EXPORT_DEVICE_API(api)                                      \
	extern const uint8_t _##api##_driver_api_list_start[];               \
	extern const uint8_t _##api##_driver_api_ext_end[];                  \
	EXPORT_GROUP_SYMBOL(DEVICE, _##api##_driver_api_list_start);         \
	EXPORT_GROUP_SYMBOL(DEVICE, _##api##_driver_api_ext_end)

#if defined(CONFIG_ADC)
MBS_LLEXT_EXPORT_DEVICE_API(adc);
#endif
#if defined(CONFIG_GPIO)
MBS_LLEXT_EXPORT_DEVICE_API(gpio);
#endif
#if defined(CONFIG_I2C)
MBS_LLEXT_EXPORT_DEVICE_API(i2c);
#endif
#if defined(CONFIG_SPI)
MBS_LLEXT_EXPORT_DEVICE_API(spi);
#endif
#if defined(CONFIG_PWM)
MBS_LLEXT_EXPORT_DEVICE_API(pwm);
#endif
#if defined(CONFIG_SERIAL)
MBS_LLEXT_EXPORT_DEVICE_API(uart);
#endif
#if defined(CONFIG_SENSOR)
MBS_LLEXT_EXPORT_DEVICE_API(sensor);
#endif
