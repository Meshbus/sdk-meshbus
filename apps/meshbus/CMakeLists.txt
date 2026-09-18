# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
# Loadable extensions need machine code, not the host's LTO intermediate
# objects. Set this before Zephyr captures the EDK compiler configuration.
list(APPEND LLEXT_EDK_REMOVE_FLAGS "-flto.*")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus)

if(NOT DEFINED meshbus_profile)
  message(FATAL_ERROR "Meshbus board profile was not loaded by ZephyrAppConfig.cmake")
endif()

if(BOARD STREQUAL "idea_mesh_tracker_c2" AND
   (NOT CONFIG_MBS_MANAGEMENT_OPERATOR OR
    CONFIG_MBS_MANAGEMENT_ENDPOINT OR CONFIG_MBS_FIRMWARE))
  message(FATAL_ERROR "C2 requires Management operator only and no Firmware endpoint")
endif()

if(CONFIG_LTO AND CONFIG_LLEXT AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
  # Zephyr's generated syscall export/weak-alias tables declare function
  # addresses as data symbols. GCC LTO cannot merge those declarations with
  # the real functions. Keep these two bridge objects as native code while
  # optimizing the firmware and preserving the complete LLEXT export table.
  set_property(SOURCE ${ZEPHYR_BASE}/subsys/llext/llext_export.c
    DIRECTORY ${ZEPHYR_BASE}/subsys/llext APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  set_property(SOURCE ${ZEPHYR_BINARY_DIR}/syscall_weakdefs_llext.c
    DIRECTORY ${ZEPHYR_BASE} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
endif()
target_sources(app PRIVATE src/main.c)
