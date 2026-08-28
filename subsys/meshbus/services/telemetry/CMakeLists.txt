# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_TELEMETRY)
	zephyr_library_sources(telemetry.c sensor_channel_sync.c)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_TELEMETRY_SHELL shell.c)
endif()
