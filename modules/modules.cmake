# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

# This file sets up integration files for external modules that use
# kconfig-ext: True and cmake-ext: True in their module.yml.

# Detools module integration
set(ZEPHYR_DETOOLS_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/detools)
set(ZEPHYR_DETOOLS_KCONFIG ${CMAKE_CURRENT_LIST_DIR}/detools/Kconfig)

set(ZEPHYR_HEATSHRINK_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/heatshrink)
set(ZEPHYR_HEATSHRINK_KCONFIG ${CMAKE_CURRENT_LIST_DIR}/heatshrink/Kconfig)

set(ZEPHYR_MESHCORE_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/meshcore)
set(ZEPHYR_MESHCORE_KCONFIG ${CMAKE_CURRENT_LIST_DIR}/meshcore/Kconfig)
