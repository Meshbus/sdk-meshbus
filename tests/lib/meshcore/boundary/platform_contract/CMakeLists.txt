cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_platform_contract_test)

set(MESHCORE_TEST_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../../common)
include(${MESHCORE_TEST_COMMON_DIR}/meshcore_module.cmake)

zephyr_include_directories(
  ${MESHCORE_TEST_COMMON_DIR}/include
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
)

target_sources(app PRIVATE
  src/main.cpp
  ${MESHCORE_TEST_COMMON_DIR}/src/meshcore_hal_zephyr.c
  ${MESHCORE_RUNTIME_LIBRARY_SOURCES}
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
