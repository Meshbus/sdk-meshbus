cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_clock_tdd_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../common/reference_meshcore.cmake)

zephyr_include_directories(
  ${MESHCORE_REFERENCE_INCLUDE_DIRS}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
  ${MESHCORE_TEST_COMMON_DIR}/include
)

target_sources(app PRIVATE
  src/main.cpp
  ${MESHCORE_TEST_COMMON_DIR}/src/meshcore_hal_zephyr.c
  ${MESHCORE_PLATFORM_BRIDGE_SOURCES}
  ${MESHCORE_PROTOCOL_CLOCK_SOURCES}
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
