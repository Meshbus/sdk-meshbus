# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_MESHCORE)
	zephyr_library_named(mbs_meshcore)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../settings
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
	include(${ZEPHYR_MESHCORE_MODULE_DIR}/cmake/meshcore_sources.cmake)

	zephyr_library_include_directories(
		.
		${MESHCORE_INTERNAL_INCLUDE_DIRS}
	)

	zephyr_library_sources(meshcore.c)
	zephyr_library_sources(meshcore_config.c)
	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)
	zephyr_library_sources_ifdef(CONFIG_MBS_MESHCORE_SHELL shell.c)

	if(CONFIG_MBS_MESHCORE_RUNTIME)
		zephyr_library_sources(
			meshcore_runtime.c
			meshcore_platform.c
			meshcore_host.c
		)
		if(CONFIG_MBS_MESHCORE_CLIENT)
			zephyr_library_sources(meshcore_ack_handoff.c)
		endif()
	endif()

	zephyr_library_sources_ifdef(CONFIG_MBS_MESHCORE_COMPANION_PROTOCOL
		companion/protocol.c
	)
	if(CONFIG_MBS_MESHCORE_COMPANION_PROTOCOL AND
	   NOT CONFIG_MBS_MESHCORE_RUNTIME)
		zephyr_library_sources(
			${MESHCORE_PROTOCOL_RNG_SOURCES}
			${MESHCORE_PROTOCOL_UTILS_SOURCES}
			${MESHCORE_PROTOCOL_IDENTITY_SOURCES}
			${MESHCORE_PROTOCOL_CRYPTO_SOURCES}
		)
	endif()
	if(CONFIG_MBS_MESHCORE_COMPANION_BLUETOOTH)
		zephyr_library_sources(companion/bluetooth.c)
		zephyr_library_sources_ifdef(CONFIG_ZTEST companion/bluetooth_test.c)
	endif()
endif()
