cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_gnss_contract_test)

target_sources(app PRIVATE src/test_api.c)

target_link_libraries(zephyr_interface INTERFACE
	"-Wl,--wrap=settings_delete"
)

# Private integration seams used by this test fixture.
target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/gnss
)
