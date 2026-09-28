# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_bluetooth_contract_test)

target_sources(app PRIVATE
  src/fake_bt_hci.c
  src/test_api.c
)

target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/bluetooth
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/meshcore
)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=bt_is_ready"
  "-Wl,--wrap=bt_enable"
  "-Wl,--wrap=bt_conn_auth_cb_register"
  "-Wl,--wrap=bt_conn_auth_info_cb_register"
  "-Wl,--wrap=bt_le_adv_start"
  "-Wl,--wrap=bt_le_adv_update_data"
  "-Wl,--wrap=bt_le_adv_stop"
  "-Wl,--wrap=bt_set_name"
  "-Wl,--wrap=bt_get_name"
  "-Wl,--wrap=settings_load_subtree"
  "-Wl,--wrap=bt_conn_ref"
  "-Wl,--wrap=bt_conn_unref"
  "-Wl,--wrap=bt_conn_get_dst"
  "-Wl,--wrap=bt_conn_set_security"
  "-Wl,--wrap=bt_conn_get_security"
  "-Wl,--wrap=bt_conn_disconnect"
  "-Wl,--wrap=bt_unpair"
  "-Wl,--wrap=bt_conn_foreach"
  "-Wl,--wrap=bt_conn_get_info"
  "-Wl,--wrap=bt_gatt_get_mtu"
  "-Wl,--wrap=bt_gatt_is_subscribed"
  "-Wl,--wrap=bt_gatt_notify_cb"
  "-Wl,--wrap=bt_nus_inst_send"
  "-Wl,--wrap=meshcore_companion_adapter_disconnected"
)
