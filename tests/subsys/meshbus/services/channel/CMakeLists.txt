# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_channel_contract_test)

target_sources(app PRIVATE
  src/test_api.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/settings/test_settings.c
)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=settings_save_one"
  "-Wl,--wrap=settings_delete"
)
