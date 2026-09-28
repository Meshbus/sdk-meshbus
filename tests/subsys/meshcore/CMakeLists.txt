cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mbs_meshcore_companion_contract_test)

target_include_directories(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/meshcore
)

target_sources(app PRIVATE src/test_companion.c)
target_sources(app PRIVATE
  ${ZEPHYR_MESHBUS_MODULE_DIR}/subsys/meshcore/meshcore_ack_handoff.c
)

target_link_libraries(zephyr_interface INTERFACE
  "-Wl,--wrap=mbs_channel_get"
  "-Wl,--wrap=mbs_channel_reset"
  "-Wl,--wrap=mbs_channel_set"
  "-Wl,--wrap=mbs_meshcore_config_get"
  "-Wl,--wrap=mbs_meshcore_config_set"
  "-Wl,--wrap=mbs_contact_find_by_key"
  "-Wl,--wrap=mbs_contact_find_by_prefix"
  "-Wl,--wrap=mbs_contact_get"
  "-Wl,--wrap=mbs_contact_reset"
  "-Wl,--wrap=mbs_contact_set"
  "-Wl,--wrap=mbs_contact_store_count"
  "-Wl,--wrap=mbs_contact_store_size"
  "-Wl,--wrap=mbs_meshcore_advert_request"
  "-Wl,--wrap=mbs_meshcore_trace_request"
  "-Wl,--wrap=mbs_contact_discover_path_request"
  "-Wl,--wrap=mbs_contact_trace_path_request"
  "-Wl,--wrap=mbs_contact_telemetry_request"
  "-Wl,--wrap=mbs_contact_binary_request"
  "-Wl,--wrap=meshcore_platform_telemetry_node_get"
  "-Wl,--wrap=mbs_meshcore_channel_data_send"
  "-Wl,--wrap=mbs_meshcore_raw_data_send"
  "-Wl,--wrap=mbs_meshcore_control_data_send"
  "-Wl,--wrap=mbs_management_smp_request_with_secret"
  "-Wl,--wrap=mbs_clock_time_set_unix_ms"
  "-Wl,--wrap=mbs_channel_store_size"
  "-Wl,--wrap=mbs_message_next"
  "-Wl,--wrap=mbs_message_send_to_channel"
  "-Wl,--wrap=mbs_message_send_to_node"
)
