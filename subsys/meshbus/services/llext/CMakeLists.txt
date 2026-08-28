# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

set(meshbus_llext_metadata_version_file
    "${CMAKE_CURRENT_SOURCE_DIR}/METADATA_VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${meshbus_llext_metadata_version_file}")
file(READ "${meshbus_llext_metadata_version_file}"
	MESHBUS_LLEXT_METADATA_VERSION)
string(STRIP "${MESHBUS_LLEXT_METADATA_VERSION}"
	MESHBUS_LLEXT_METADATA_VERSION)
if(NOT MESHBUS_LLEXT_METADATA_VERSION MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR
		"Meshbus LLEXT METADATA_VERSION must contain one positive integer")
endif()
configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/meshbus_llext_metadata_version.h.in"
	"${ZEPHYR_BINARY_DIR}/include/generated/meshbus_llext_metadata_version.h"
	@ONLY)
zephyr_include_directories("${ZEPHYR_BINARY_DIR}/include/generated")

if(CONFIG_MESHBUS_LLEXT)
	zephyr_library_sources(
		llext.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_LLEXT_SHELL shell.c)
endif()

if(CONFIG_MESHBUS_LLEXT_BRIDGE)
	zephyr_library_sources(
		llext_exports.c
		llext_bridge_common.c
		llext_zbus.c
	)
endif()
