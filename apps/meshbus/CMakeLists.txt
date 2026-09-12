# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus)

if(NOT "${BOARD}/${BOARD_QUALIFIERS}" STREQUAL
   "idea_mesh_tracker_c2/nrf54l15/cpuapp")
  message(FATAL_ERROR "Unsupported Meshbus device target")
endif()
if(NOT CONFIG_MBS_MANAGEMENT_OPERATOR OR
   CONFIG_MBS_MANAGEMENT_ENDPOINT OR CONFIG_MBS_FIRMWARE)
  message(FATAL_ERROR "C2 requires Management operator only and no Firmware endpoint")
endif()
target_sources(app PRIVATE src/main.c)
