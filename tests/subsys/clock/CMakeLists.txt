# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_clock_contract_test)

target_sources(app PRIVATE src/test_timestamps.c)
if(CONFIG_MBS_CLOCK)
  target_sources(app PRIVATE src/test_api.c)
endif()
if(CONFIG_MBS_CLOCK_SHELL)
  target_sources(app PRIVATE src/test_shell.c)
endif()
