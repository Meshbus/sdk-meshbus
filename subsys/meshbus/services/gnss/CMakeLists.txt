# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_GNSS)
        zephyr_library_sources(gnss.c)
	zephyr_library_sources(heading.c)
        zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)
        zephyr_library_sources_ifdef(CONFIG_MESHBUS_GNSS_SHELL shell.c)
endif()
