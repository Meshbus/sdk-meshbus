cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_power_contract_test)

target_sources(app PRIVATE src/test_api.c)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=settings_save_one"
  "-Wl,--wrap=sys_poweroff"
  "-Wl,--wrap=sys_reboot"
)
