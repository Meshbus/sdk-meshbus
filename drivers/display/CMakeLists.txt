# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_MODULINO_LED_MATRIX modulino_led_matrix.c)
