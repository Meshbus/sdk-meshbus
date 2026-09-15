# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

if(NOT CHIP_ROOT)
  message(FATAL_ERROR "Set CHIP_ROOT to the connectedhomeip checkout described in README.md")
endif()
list(APPEND EXTRA_ZEPHYR_MODULES "${CHIP_ROOT}/config/zephyr/chip-module")
# The generic Zephyr GN entry does not infer the C6 RISC-V architecture.
set(MATTER_GN_ARGS "--arg-string\ntarget_cpu\nriscv\n")
# Select the implementations required by this application's data model.
if(NOT DEFINED CHIP_GN_ZAP_FILE)
  set(CHIP_GN_ZAP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/environment.zap")
endif()
# Bound the nested build on development hosts; users can override this cache entry.
set(MATTER_GN_BUILD_JOBS 4 CACHE STRING "Maximum concurrent Matter Ninja jobs")
# Local cold/configuration measurements favored direct GN compiler invocation.
# Keep the outer Zephyr launcher unchanged and allow hosts to opt back in.
set(MATTER_GN_USE_COMPILER_LAUNCHERS OFF CACHE BOOL "Use Zephyr compiler launchers in Matter GN")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(matter_temperature LANGUAGES C CXX)

target_sources(app PRIVATE
  src/main.cpp
  src/environment.cpp
  src/air_quality.c
  ${CHIP_ROOT}/examples/providers/DeviceInfoProviderImpl.cpp
)
target_include_directories(app PRIVATE ${CHIP_ROOT}/examples/providers)
target_link_libraries(app PRIVATE chip)
set(ENV{CHIP_ROOT} "${CHIP_ROOT}")
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)
chip_configure_data_model(app
  ZAP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/environment.zap"
)
