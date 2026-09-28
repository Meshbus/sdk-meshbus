# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_role_lifecycle)
target_sources(app PRIVATE src/main.c)
target_link_libraries(zephyr_interface INTERFACE "-Wl,--wrap=settings_save_one" "-Wl,--wrap=settings_delete")
if(ROLE_RUNTIME_TEST)
  target_link_libraries(zephyr_interface INTERFACE "-Wl,--wrap=zbus_chan_pub"
    "-Wl,--wrap=meshcore_init" "-Wl,--wrap=meshcore_deinit" "-Wl,--wrap=meshcore_radio_tx_done" "-Wl,--wrap=meshcore_radio_rx_inject"
    "-Wl,--wrap=meshcore_node_advert_request" "-Wl,--wrap=k_work_submit_to_queue")
  target_compile_definitions(app PRIVATE TEST_REQUIRE_RUNTIME=1)
  target_sources(app PRIVATE src/radio_unavailable.c)
  target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/lora/lora-basics-modem)
endif()
