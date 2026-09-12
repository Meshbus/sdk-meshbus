# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MCUMGR)
	zephyr_library_named(mbs_mgmt)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_sources(mgmt.c)
	zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../settings)
endif()
