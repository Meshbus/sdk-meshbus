cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_management_service_dut_test)

target_sources(app PRIVATE src/test_service_dut.c)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=settings_save_one"
)
