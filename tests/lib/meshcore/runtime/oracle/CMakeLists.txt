cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_oracle_parity_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../common/reference_meshcore.cmake)

set(MESHCORE_TARGET_C_SOURCES
  ${MESHCORE_TEST_COMMON_DIR}/src/meshcore_hal_zephyr.c
  ${MESHCORE_RUNTIME_LIBRARY_SOURCES}
)
set(MESHCORE_REFERENCE_CPP_SOURCES
  ${MESHCORE_REFERENCE_DISPATCHER_SOURCES}
  ${MESHCORE_REFERENCE_IDENTITY_SOURCES}
  ${MESHCORE_REFERENCE_MESH_SOURCES}
  ${MESHCORE_REFERENCE_PACKET_SOURCES}
  ${MESHCORE_REFERENCE_UTILS_SOURCES}
  ${MESHCORE_REFERENCE_DIR}/helpers/BaseChatMesh.cpp
  ${MESHCORE_REFERENCE_ADVERT_DATA_SOURCES}
  ${MESHCORE_REFERENCE_DIR}/helpers/TxtDataHelpers.cpp
  ${MESHCORE_REFERENCE_STATIC_POOL_PACKET_MANAGER_SOURCES}
)

zephyr_include_directories(
  include
  ${MESHCORE_REFERENCE_INCLUDE_DIRS}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
  ${MESHCORE_TEST_COMMON_DIR}/include
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519
)

target_sources(app PRIVATE
  src/suite_runtime_oracle.cpp
  src/case_helpers.cpp
  src/cases_send_side.cpp
  src/cases_receive_discover.cpp
  src/cases_receive_trace.cpp
  src/cases_receive_telemetry.cpp
  src/cases_negative.cpp
  src/reference_chat_harness.cpp
  src/test_support.cpp
  ${MESHCORE_TARGET_C_SOURCES}
  ${MESHCORE_REFERENCE_CPP_SOURCES}
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Crypto.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Hash.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA256.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA512.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BlockCipher.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AESCommon.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AES128.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Ed25519.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BigNumberUtil.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Curve25519.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/add_scalar.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/fe.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/ge.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/key_exchange.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/keypair.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sc.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/seed.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sha512.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sign.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/verify.c
)

target_compile_definitions(app PRIVATE MESHCORE_ENABLE_TEST_HOOKS)

set_source_files_properties(${MESHCORE_TARGET_C_SOURCES} PROPERTIES
  INCLUDE_DIRECTORIES
  "${MESHCORE_INTERNAL_INCLUDE_DIRS};${MESHCORE_TEST_COMMON_DIR}/include;${MESHCORE_TEST_COMMON_DIR}/lib/Crypto;${MESHCORE_TEST_COMMON_DIR}/lib/ed25519;${CMAKE_CURRENT_LIST_DIR}/include"
)
set_source_files_properties(${MESHCORE_REFERENCE_CPP_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;meshcore_reference_prefix.h"
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess>
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/tools/check_test_cases_sync.py
          --src-dir ${CMAKE_CURRENT_LIST_DIR}/src
          --cases ${CMAKE_CURRENT_LIST_DIR}/test_cases.md
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  RESULT_VARIABLE MESHCORE_RUNTIME_ORACLE_CASES_RESULT
  OUTPUT_VARIABLE MESHCORE_RUNTIME_ORACLE_CASES_OUTPUT
  ERROR_VARIABLE MESHCORE_RUNTIME_ORACLE_CASES_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT MESHCORE_RUNTIME_ORACLE_CASES_RESULT EQUAL 0)
  message(FATAL_ERROR
    "MeshCore runtime oracle test-case sync check failed\n"
    "${MESHCORE_RUNTIME_ORACLE_CASES_OUTPUT}\n"
    "${MESHCORE_RUNTIME_ORACLE_CASES_ERROR}"
  )
endif()

message(STATUS "${MESHCORE_RUNTIME_ORACLE_CASES_OUTPUT}")

add_custom_target(oracle_parity_check_test_cases
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/tools/check_test_cases_sync.py
          --src-dir ${CMAKE_CURRENT_LIST_DIR}/src
          --cases ${CMAKE_CURRENT_LIST_DIR}/test_cases.md
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  COMMENT "Check oracle_parity ZTEST list is synced with test_cases.md"
  VERBATIM
)
