# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# Include Zephyr's internal GNSS driver headers
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/gnss)

zephyr_library_sources_ifdef(CONFIG_GNSS_QUECTEL_L76K gnss_quectel_l76k.c)
