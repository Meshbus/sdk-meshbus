cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_runtime_api_map_test)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(MESHCORE_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(MESHCORE_RUNTIME_API_MAP
  tests/lib/meshcore/runtime/runtime_api_map.json
)

execute_process(
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/../tools/check_runtime_api_map.py
          --repo-root ${MESHCORE_REPO_ROOT}
          --map ${MESHCORE_RUNTIME_API_MAP}
  WORKING_DIRECTORY ${MESHCORE_REPO_ROOT}
  RESULT_VARIABLE MESHCORE_RUNTIME_API_MAP_RESULT
  OUTPUT_VARIABLE MESHCORE_RUNTIME_API_MAP_OUTPUT
  ERROR_VARIABLE MESHCORE_RUNTIME_API_MAP_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT MESHCORE_RUNTIME_API_MAP_RESULT EQUAL 0)
  message(FATAL_ERROR
    "MeshCore runtime API map check failed\n"
    "${MESHCORE_RUNTIME_API_MAP_OUTPUT}\n"
    "${MESHCORE_RUNTIME_API_MAP_ERROR}"
  )
endif()

message(STATUS "${MESHCORE_RUNTIME_API_MAP_OUTPUT}")

target_sources(app PRIVATE src/main.c)
