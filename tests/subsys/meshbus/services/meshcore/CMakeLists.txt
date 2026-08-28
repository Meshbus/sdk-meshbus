cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshbus_meshcore_companion_contract_test)

target_sources(app PRIVATE src/test_companion.c)
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../subsys/meshbus/services/meshcore/meshcore_ack_handoff.c
)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=meshbus_channel_get"
  "-Wl,--wrap=meshbus_channel_reset"
  "-Wl,--wrap=meshbus_channel_set"
  "-Wl,--wrap=meshbus_meshcore_config_get"
  "-Wl,--wrap=meshbus_meshcore_config_set"
  "-Wl,--wrap=meshbus_contact_find_by_key"
  "-Wl,--wrap=meshbus_contact_find_by_prefix"
  "-Wl,--wrap=meshbus_contact_get"
  "-Wl,--wrap=meshbus_contact_reset"
  "-Wl,--wrap=meshbus_contact_set"
  "-Wl,--wrap=meshbus_contact_store_count"
  "-Wl,--wrap=meshbus_contact_store_size"
  "-Wl,--wrap=meshbus_meshcore_advert_request"
  "-Wl,--wrap=meshbus_meshcore_trace_request"
  "-Wl,--wrap=meshbus_contact_discover_path_request"
  "-Wl,--wrap=meshbus_contact_trace_path_request"
  "-Wl,--wrap=meshbus_contact_telemetry_request"
  "-Wl,--wrap=meshbus_contact_binary_request"
  "-Wl,--wrap=meshcore_platform_telemetry_node_get"
  "-Wl,--wrap=meshbus_meshcore_channel_data_send"
  "-Wl,--wrap=meshbus_meshcore_raw_data_send"
  "-Wl,--wrap=meshbus_meshcore_control_data_send"
  "-Wl,--wrap=meshbus_management_smp_request_with_secret"
  "-Wl,--wrap=meshbus_clock_time_set_unix_ms"
  "-Wl,--wrap=meshbus_channel_store_size"
  "-Wl,--wrap=meshbus_message_next"
  "-Wl,--wrap=meshbus_message_send_to_channel"
  "-Wl,--wrap=meshbus_message_send_to_node"
)
