cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_telemetry_contract_test)

zephyr_linker_sources(DATA_SECTIONS power_action_iterable.ld)

target_sources(app PRIVATE
	src/fake_power_domain.c
	src/fake_sensor.c
	src/test_api.c
)

target_link_libraries(zephyr_interface INTERFACE
	"-Wl,--wrap=settings_delete"
)

# Private integration seams used by this test fixture.
target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/telemetry
)
