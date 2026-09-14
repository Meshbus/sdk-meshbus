# SPDX-License-Identifier: Apache-2.0

set(MBS_ROOT "${ZEPHYR_MESHBUS_MODULE_DIR}")
set(DESKTOP_DIR "${MBS_ROOT}/subsys/desktop")
set(MBA_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Real Desktop lifecycle, registry, loader and threads with the selected storage.
# Only UI presentation, scheduling at exit, and LLEXT failure injection vary.
target_sources(app PRIVATE
	${MBA_TEST_DIR}/src/main.c
	${DESKTOP_DIR}/app_runtime.c
	${DESKTOP_DIR}/packages.c
	${DESKTOP_DIR}/registry/apps_registry.c
)
target_include_directories(app PRIVATE ${DESKTOP_DIR})
target_compile_definitions(app PRIVATE
	CONFIG_MBS_DESKTOP=1
	CONFIG_MBS_DESKTOP_PACKAGES=1
	CONFIG_MBS_DESKTOP_LOG_LEVEL=3
	CONFIG_MBS_DESKTOP_LAUNCHER=1
	CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE=2048
	CONFIG_MBS_DESKTOP_APP_THREAD_PRIORITY=0
)
zephyr_linker_sources(SECTIONS ${DESKTOP_DIR}/registry/iterables_apps.ld)
zephyr_link_libraries(
	-Wl,--wrap=llext_unload
	-Wl,--wrap=mbs_llext_host_info_get
	-Wl,--wrap=fs_rename
	-Wl,--wrap=fs_unlink
	-Wl,--wrap=fs_statvfs
	-Wl,--wrap=zui_desktop_request_app_exit
	-Wl,--wrap=desktop_app_registry_start
	-Wl,--wrap=zui_host_set_layer_enabled
	-Wl,--wrap=zui_host_send_layer_to_front
	-Wl,--wrap=zui_router_current
)

foreach(kind app bad_entry)
	set(package ${PROJECT_BINARY_DIR}/llext/${kind}.mba)
	add_llext_target(mba_${kind}_ext
		OUTPUT ${package}
		SOURCES ${MBA_TEST_DIR}/src/${kind}_ext.c
	)
	if(TARGET mbs_proto)
		add_dependencies(mba_${kind}_ext_llext_lib mbs_proto)
	endif()
	generate_inc_file_for_target(app ${package}
		${ZEPHYR_BINARY_DIR}/include/generated/mba_${kind}.inc)
	add_custom_command(OUTPUT ${package} APPEND
		COMMAND ${CMAKE_OBJCOPY}
			--set-section-flags .meshbus.llext.meta=contents,readonly ${package}
	)
endforeach()
