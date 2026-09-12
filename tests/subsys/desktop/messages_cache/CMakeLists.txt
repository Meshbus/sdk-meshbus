# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_desktop_messages_cache_test)

set(SDK_ROOT "${ZEPHYR_MESHBUS_MODULE_DIR}")
set(DESKTOP_DIR "${SDK_ROOT}/subsys/desktop")

# Exercise the real cache through its private snapshot API and the public
# Message channel, without compiling the Desktop host or app runtime.
target_sources(app PRIVATE src/main.c ${DESKTOP_DIR}/services/messages_cache.c)
target_include_directories(app PRIVATE ${DESKTOP_DIR})
target_compile_definitions(app PRIVATE CONFIG_MBS_DESKTOP_MESSAGE_CACHE_COUNT=3)
target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=mbs_clock_realtime_is_valid"
  "-Wl,--wrap=mbs_clock_timestamp_ms_get"
)
