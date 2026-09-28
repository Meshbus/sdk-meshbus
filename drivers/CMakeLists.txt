# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_CHARGER charger)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_GNSS gnss)
add_subdirectory_ifdef(CONFIG_INPUT input)
add_subdirectory_ifdef(CONFIG_MFD mfd)
add_subdirectory_ifdef(CONFIG_MODULINO_LED_MATRIX display)
