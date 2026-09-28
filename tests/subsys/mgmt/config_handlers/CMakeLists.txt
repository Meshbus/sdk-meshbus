# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_management_config_handlers_integration_test)

target_sources(app PRIVATE src/main.c)

# Keep the registered handlers and wire codecs real; control only their backends.
target_link_libraries(zephyr_interface INTERFACE
  -Wl,--wrap=mbs_clock_config_get
  -Wl,--wrap=mbs_clock_config_set
  -Wl,--wrap=mbs_clock_config_reset
  -Wl,--wrap=mbs_display_config_get
  -Wl,--wrap=mbs_display_config_set
  -Wl,--wrap=mbs_display_config_reset
)
