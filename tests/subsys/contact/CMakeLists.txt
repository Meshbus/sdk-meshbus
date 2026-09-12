# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_contact_contract_test)

if(NOT CONFIG_MBS_CONTACT_ADVERT_RAW_STORE)
	target_sources(app PRIVATE src/raw_advert_store_disabled.c)
else()
	target_sources(app PRIVATE src/test_api.c)
	target_link_libraries(zephyr_interface INTERFACE
	  "-Wl,--wrap=settings_save_one"
	)
endif()

# Private integration seams used by this test fixture.
target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/settings
)
