# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

set(mbs_llext_metadata_version_file
    "${CMAKE_CURRENT_SOURCE_DIR}/METADATA_VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${mbs_llext_metadata_version_file}")
file(READ "${mbs_llext_metadata_version_file}"
	MBS_LLEXT_METADATA_VERSION)
string(STRIP "${MBS_LLEXT_METADATA_VERSION}"
	MBS_LLEXT_METADATA_VERSION)
if(NOT MBS_LLEXT_METADATA_VERSION MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR
		"Meshbus LLEXT METADATA_VERSION must contain one positive integer")
endif()
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/mbs_llext_metadata_version.h.in"
	"${ZEPHYR_BINARY_DIR}/include/generated/mbs_llext_metadata_version.h"
	@ONLY)
zephyr_include_directories("${ZEPHYR_BINARY_DIR}/include/generated")

if(CONFIG_MBS_LLEXT OR CONFIG_MBS_LLEXT_BRIDGE)
	zephyr_library_named(mbs_llext)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../settings
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
endif()

if(CONFIG_MBS_LLEXT)
	zephyr_library_sources(
		llext.c
		host_info.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)

	zephyr_library_sources_ifdef(CONFIG_MBS_LLEXT_SHELL shell.c)
endif()

if(CONFIG_MBS_LLEXT_BRIDGE)
	zephyr_library_sources(
		llext_exports.c
		llext_device_exports.c
		llext_bridge_common.c
		llext_zbus.c
	)
endif()
