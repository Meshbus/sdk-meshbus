# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_NOTIFY)
	zephyr_library_sources(
		notify.c
	)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_NOTIFY_TRANSPORT_SERIAL
		serial.c
	)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_NOTIFY_SHELL
		shell.c
	)
endif()
