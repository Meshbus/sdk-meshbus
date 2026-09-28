# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_message_service_test)

target_sources(app PRIVATE src/test_api.c)
target_sources_ifdef(
  CONFIG_TEST_MBS_MESSAGE_SERVICE_DUT
  app PRIVATE
  src/test_service_dut.c
)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=mbs_meshcore_config_get"
  "-Wl,--wrap=mbs_contact_find_by_prefix"
  "-Wl,--wrap=mbs_channel_get"
)
