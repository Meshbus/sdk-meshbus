# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_COMPASS_COMPOSITE composite)
add_subdirectory_ifdef(CONFIG_FOBE_LSM6DS3_FAMILY st/lsm6ds3_family)
add_subdirectory_ifdef(CONFIG_SENSOR qst)
