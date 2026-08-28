# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_meshcore_request_acceptance_integration_test)

include(${ZEPHYR_MESHCORE_MODULE_DIR}/cmake/meshcore_sources.cmake)

get_filename_component(FOBE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
set(MESHCORE_SERVICE_DIR
  "${FOBE_ROOT}/subsys/meshbus/services/meshcore")

target_include_directories(app PRIVATE
  ${MESHCORE_SERVICE_DIR}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
)

target_sources(app PRIVATE src/test_acceptance.c)
