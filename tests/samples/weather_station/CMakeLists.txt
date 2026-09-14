# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weather_packets_test)
set(sample_src ${CMAKE_CURRENT_SOURCE_DIR}/../../../samples/sensor/weather_station/src)
target_include_directories(app PRIVATE ${sample_src})
target_sources(app PRIVATE src/main.c ${sample_src}/ble_packets.c)
target_sources(app PRIVATE ${sample_src}/ble_text.c)
