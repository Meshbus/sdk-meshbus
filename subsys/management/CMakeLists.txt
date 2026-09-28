# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_MANAGEMENT)
	zephyr_library_named(mbs_management)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../settings
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
	zephyr_library_sources(management.c)
	zephyr_library_sources(management_common.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_MANAGEMENT_ENDPOINT management_session.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_MANAGEMENT_OPERATOR management_smp.c)
	zephyr_library_sources(mgmt.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_MANAGEMENT_SHELL shell.c)
endif()
