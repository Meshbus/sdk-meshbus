# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_indicator_contract_test)

if(INDICATOR_MINIMAL_TEST)
  target_sources(app PRIVATE src/test_minimal.c)
else()
target_sources(app PRIVATE src/test_api.c)

target_link_libraries(zephyr_interface INTERFACE
	"-Wl,--wrap=indicator_buzzer_is_ready"
	"-Wl,--wrap=indicator_buzzer_play"
	"-Wl,--wrap=indicator_buzzer_play_owned"
	"-Wl,--wrap=indicator_buzzer_play_rtttl"
)
endif()
