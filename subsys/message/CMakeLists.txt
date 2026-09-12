# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_MESSAGE)
	zephyr_library_named(mbs_message)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
	zephyr_library_sources(
		message.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR
		mgmt.c
	)

	zephyr_library_sources_ifdef(CONFIG_MBS_MESSAGE_SHELL
		shell.c
	)
endif()
