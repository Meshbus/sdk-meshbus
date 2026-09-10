# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_desktop_boot_logo_test)

# Exercise the real runtime/host/draw lifecycle without unrelated product
# widgets and services. main.c substitutes the screen contents and display.
get_filename_component(MESHBUS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
set(DESKTOP_DIR ${MESHBUS_ROOT}/subsys/meshbus/services/desktop)
set(BOOT_LOGO_DURATION_MS 1000 CACHE STRING "Boot logo hold for this scenario")
target_sources(app PRIVATE src/main.c ${DESKTOP_DIR}/runtime.c)
target_include_directories(app PRIVATE ${DESKTOP_DIR})
target_compile_definitions(app PRIVATE
	CONFIG_MESHBUS_DESKTOP_LOG_LEVEL=3
	CONFIG_MESHBUS_DESKTOP_APP_SHARED_STACK_SIZE=2048
	CONFIG_MESHBUS_DESKTOP_BOOT_LOGO_DURATION_MS=${BOOT_LOGO_DURATION_MS}
)
