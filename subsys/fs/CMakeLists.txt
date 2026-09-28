# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_FS)
	zephyr_library_named(mbs_fs)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
	)
	zephyr_library_sources(fs.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_FS_MGMT mgmt.c)
endif()
