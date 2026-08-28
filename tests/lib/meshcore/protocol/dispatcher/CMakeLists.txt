cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_dispatcher_tdd_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../common/reference_meshcore.cmake)

zephyr_include_directories(
  ${MESHCORE_REFERENCE_INCLUDE_DIRS}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
  ${MESHCORE_TEST_COMMON_DIR}/include
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto
)

target_sources(app PRIVATE
  src/main.cpp
  src/test_support.cpp
  src/test_reference_env.cpp
  src/test_target_env.cpp
  ${MESHCORE_TEST_COMMON_DIR}/src/meshcore_hal_zephyr.c
  ${MESHCORE_PLATFORM_BRIDGE_SOURCES}
  ${MESHCORE_PROTOCOL_CLOCK_SOURCES}
  ${MESHCORE_PROTOCOL_UTILS_SOURCES}
  ${MESHCORE_PROTOCOL_PACKET_SOURCES}
  ${MESHCORE_PROTOCOL_PACKET_MANAGER_SOURCES}
  ${MESHCORE_PROTOCOL_RADIO_SOURCES}
  ${MESHCORE_PROTOCOL_DISPATCHER_SOURCES}
  ${MESHCORE_REFERENCE_STATIC_POOL_PACKET_MANAGER_SOURCES}
  ${MESHCORE_REFERENCE_DISPATCHER_SOURCES}
  ${MESHCORE_REFERENCE_PACKET_SOURCES}
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Crypto.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Hash.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA256.cpp
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
