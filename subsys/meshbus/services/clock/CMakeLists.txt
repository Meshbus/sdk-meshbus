# Copyright (c) 2026 FoBE Studio
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_CLOCK)
	zephyr_library_sources(clock.c)
	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)
	zephyr_library_sources_ifdef(CONFIG_MESHBUS_CLOCK_SHELL shell.c)
endif()
