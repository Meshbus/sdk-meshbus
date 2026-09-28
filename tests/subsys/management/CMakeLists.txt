cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_management_contract_test)

set(FOBE_ROOT "${ZEPHYR_MESHBUS_MODULE_DIR}")
set(MESHCORE_SERVICE_DIR
  "${FOBE_ROOT}/subsys/meshcore")

target_include_directories(app PRIVATE ${MESHCORE_SERVICE_DIR})

target_sources(app PRIVATE src/test_api.c)

# Private integration seams used by this test fixture.
target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/mgmt
)
