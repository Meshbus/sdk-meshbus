# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_radio_service_test)

target_sources(app PRIVATE src/test_api.c)
target_sources_ifdef(CONFIG_TEST_MBS_RADIO_SERVICE_DUT app PRIVATE
		     src/test_service_dut.c)
