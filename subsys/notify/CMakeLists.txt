# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_NOTIFY)
	zephyr_library_named(mbs_notify)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
	zephyr_library_sources(
		notify.c
	)

	zephyr_library_sources_ifdef(CONFIG_MBS_NOTIFY_TRANSPORT_SERIAL
		serial.c
	)

	zephyr_library_sources_ifdef(CONFIG_MBS_NOTIFY_SHELL
		shell.c
	)
endif()
