# SPDX-License-Identifier: Apache-2.0

# zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/adc.h)

zephyr_library()
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/adc)

# zephyr_library_sources_ifdef(CONFIG_ADC adc_common.c)
# zephyr_library_sources_ifdef(CONFIG_ADC_SHELL adc_shell.c)
# zephyr_library_sources_ifdef(CONFIG_USERSPACE adc_handlers.c)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_ADC_ADS1110 adc_ads1110.c)
# zephyr-keep-sorted-stop
