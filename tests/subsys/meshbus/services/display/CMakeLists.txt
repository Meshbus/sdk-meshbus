cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_display_contract_test)

target_sources(app PRIVATE src/test_api.c)
