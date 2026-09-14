# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weather_station)
target_sources(app PRIVATE src/main.c src/air_quality.c)
target_sources(app PRIVATE src/weather_ble.c src/ble_packets.c)
target_sources(app PRIVATE src/ble_text.c)
