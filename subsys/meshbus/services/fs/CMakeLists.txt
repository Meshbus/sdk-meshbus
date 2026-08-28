# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_FS)
	zephyr_library_sources(fs.c)
	zephyr_library_sources_ifdef(CONFIG_MESHBUS_FS_MGMT mgmt.c)
endif()
