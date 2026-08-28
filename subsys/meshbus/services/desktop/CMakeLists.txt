# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
if(CONFIG_MESHBUS_DESKTOP)
	zephyr_library()

	zephyr_include_directories(.)

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
		dashboard.c
		main_menu.c
		power_menu.c
		desktop_input.c
	)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_DESKTOP_LAUNCHER
		launcher.c
	)

	zephyr_library_sources_ifdef(CONFIG_MCUMGR
		mgmt.c
	)

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_DESKTOP_SHELL
		shell.c
	)
endif()
