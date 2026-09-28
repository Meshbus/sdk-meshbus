# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_gnss_heading_contract_test)

zephyr_linker_sources(DATA_SECTIONS power_action_iterable.ld)

if(CONFIG_COMPASS_COMPOSITE)
	target_sources(app PRIVATE src/test_api.c)
else()
	target_sources(app PRIVATE src/test_direct.c)
endif()
