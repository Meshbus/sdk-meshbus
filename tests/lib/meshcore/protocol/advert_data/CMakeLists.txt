cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_advert_data_tdd_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../common/reference_meshcore.cmake)

zephyr_include_directories(
  ${MESHCORE_REFERENCE_INCLUDE_DIRS}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${MESHCORE_TEST_COMMON_DIR}/include
  ${MESHCORE_TEST_COMMON_DIR}/lib
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto
)

target_sources(app PRIVATE
  src/main.cpp
  ${MESHCORE_SUPPORT_ADVERT_DATA_SOURCES}
  ${MESHCORE_SUPPORT_UTF8_SOURCES}
  ${MESHCORE_REFERENCE_ADVERT_DATA_SOURCES}
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
