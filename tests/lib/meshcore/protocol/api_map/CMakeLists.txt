cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_protocol_api_map_test)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FOBE_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)

set(MESHCORE_PROTOCOL_API_MAP
  tests/lib/meshcore/protocol/protocol_api_map.json
)

execute_process(
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/../tools/check_protocol_api_map.py
          --repo-root ${FOBE_REPO_ROOT}
          --map ${MESHCORE_PROTOCOL_API_MAP}
  WORKING_DIRECTORY ${FOBE_REPO_ROOT}
  RESULT_VARIABLE MESHCORE_PROTOCOL_API_MAP_RESULT
  OUTPUT_VARIABLE MESHCORE_PROTOCOL_API_MAP_OUTPUT
  ERROR_VARIABLE MESHCORE_PROTOCOL_API_MAP_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT MESHCORE_PROTOCOL_API_MAP_RESULT EQUAL 0)
  message(FATAL_ERROR
    "MeshCore protocol API map check failed\n"
    "${MESHCORE_PROTOCOL_API_MAP_OUTPUT}\n"
    "${MESHCORE_PROTOCOL_API_MAP_ERROR}"
  )
endif()

message(STATUS "${MESHCORE_PROTOCOL_API_MAP_OUTPUT}")

target_sources(app PRIVATE src/main.c)
