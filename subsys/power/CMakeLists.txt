# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_POWER)
	zephyr_library_named(mbs_power)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../settings
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
		${CMAKE_CURRENT_SOURCE_DIR}/../indicator
	)
	zephyr_linker_sources(DATA_SECTIONS
		${CMAKE_CURRENT_SOURCE_DIR}/../../include/zephyr/linker/meshbus.ld)
	zephyr_library_sources(
		power.c
	)
	zephyr_library_sources_ifdef(CONFIG_MCUMGR
		mgmt.c
	)
	zephyr_library_sources_ifdef(CONFIG_MBS_POWER_SHELL shell.c)
endif()
