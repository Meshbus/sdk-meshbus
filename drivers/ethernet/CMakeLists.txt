# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ETH_CH390 eth_ch390.c)
