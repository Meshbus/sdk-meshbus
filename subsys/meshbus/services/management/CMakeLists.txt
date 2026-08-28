# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_MANAGEMENT)
	zephyr_library_sources(management.c)
	zephyr_library_sources(management_common.c)
	zephyr_library_sources_ifdef(CONFIG_MESHBUS_MANAGEMENT_ENDPOINT management_session.c)
	zephyr_library_sources_ifdef(CONFIG_MESHBUS_MANAGEMENT_OPERATOR management_smp.c)
	zephyr_library_sources(mgmt.c)
	zephyr_library_sources_ifdef(CONFIG_MESHBUS_MANAGEMENT_SHELL shell.c)
endif()
