# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_desktop_messages_cache_test)

get_filename_component(SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
set(DESKTOP_DIR "${SDK_ROOT}/subsys/meshbus/services/desktop")

# Exercise the real cache through its private snapshot API and the public
# Message channel, without compiling the Desktop host or app runtime.
target_sources(app PRIVATE src/main.c ${DESKTOP_DIR}/services/messages_cache.c)
target_include_directories(app PRIVATE ${DESKTOP_DIR})
target_compile_definitions(app PRIVATE CONFIG_MESHBUS_DESKTOP_MESSAGE_CACHE_COUNT=3)
target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=meshbus_time_realtime_is_valid"
  "-Wl,--wrap=meshbus_time_timestamp_ms_get"
)
