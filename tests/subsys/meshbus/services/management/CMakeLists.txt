cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_management_contract_test)

get_filename_component(FOBE_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(MESHCORE_SERVICE_DIR
  "${FOBE_ROOT}/subsys/meshbus/services/meshcore")

target_include_directories(app PRIVATE ${MESHCORE_SERVICE_DIR})

target_sources(app PRIVATE src/test_api.c)
