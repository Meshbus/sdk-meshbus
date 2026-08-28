# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_INDICATOR)
	zephyr_library_sources(
		indicator.c
		indicator_buzzer.c
		indicator_light.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR
		mgmt.c
	)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_INDICATOR_SHELL shell.c)
endif()
