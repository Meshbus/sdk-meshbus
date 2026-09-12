# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_desktop_integration_test)

set(FOBE_ROOT "${ZEPHYR_MESHBUS_MODULE_DIR}")
set(DESKTOP_DIR "${FOBE_ROOT}/subsys/desktop")

# CONFIG_MBS_GNSS is defined below without a chosen Compass or GNSS source.
# Compiling the real widget here keeps its source guard covered; the registry
# contract proves that it contributes no descriptor in this topology.
target_sources(app PRIVATE
	src/test_input.c
	src/test_registry.c
	${DESKTOP_DIR}/desktop_input.c
	${DESKTOP_DIR}/registry/apps_registry.c
	${DESKTOP_DIR}/registry/dashboard_widgets_registry.c
	${DESKTOP_DIR}/widgets/compass_widget.c
)

target_include_directories(app PRIVATE
	${DESKTOP_DIR}
	${DESKTOP_DIR}/registry
)

target_compile_definitions(app PRIVATE
	CONFIG_MBS_INPUT=1
	CONFIG_MBS_DESKTOP=1
	CONFIG_MBS_DESKTOP_LOG_LEVEL=3
	CONFIG_MBS_DESKTOP_LAUNCHER=1
	CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE=2048
	CONFIG_MBS_DESKTOP_APP_THREAD_PRIORITY=0
	CONFIG_MBS_GNSS=1
	CONFIG_ZUI_INPUT_QUEUE_SIZE=4
)

zephyr_linker_sources(SECTIONS ${DESKTOP_DIR}/registry/iterables_apps.ld)
zephyr_linker_sources(SECTIONS
	${DESKTOP_DIR}/registry/iterables_dashboard_widgets.ld)
