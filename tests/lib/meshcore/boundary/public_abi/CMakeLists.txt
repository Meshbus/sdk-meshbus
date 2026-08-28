cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_public_abi_test)

set(MESHCORE_TEST_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../../common)
include(${MESHCORE_TEST_COMMON_DIR}/meshcore_module.cmake)

zephyr_include_directories(
  ${MESHCORE_PUBLIC_INCLUDE_DIRS}
)

target_sources(app PRIVATE
  src/main.cpp
  src/public_abi_c.c
)
