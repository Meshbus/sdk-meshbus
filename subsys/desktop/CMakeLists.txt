# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
if(CONFIG_MBS_DESKTOP)
	zephyr_library_named(mbs_desktop)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
		${CMAKE_CURRENT_SOURCE_DIR}/../gnss
	)

	zephyr_library_include_directories(.)

	# Iterable sections for static registration (Apps / Widgets)
	zephyr_linker_sources(SECTIONS
		registry/iterables_apps.ld
		registry/iterables_dashboard_widgets.ld
	)

	# Subdirectories
	add_subdirectory(assets)
	add_subdirectory(registry)
	add_subdirectory(widgets)
	add_subdirectory(apps)
	add_subdirectory(services)
	add_subdirectory(notify)
	add_subdirectory(text)

	zephyr_library_sources(
		runtime.c
		app_runtime.c
		dashboard.c
		main_menu.c
		power_menu.c
		desktop_input.c
	)

	zephyr_library_sources_ifdef(CONFIG_MBS_DESKTOP_LAUNCHER
		launcher.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR
		mgmt.c
	)

	zephyr_library_sources_ifdef(CONFIG_MBS_DESKTOP_SHELL
		shell.c
	)
endif()
