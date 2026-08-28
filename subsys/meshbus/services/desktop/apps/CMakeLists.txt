# SPDX-License-Identifier: Apache-2.0

# Apps are statically registered via iterable descriptors.

zephyr_library_sources(
	${CMAKE_CURRENT_SOURCE_DIR}/app_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_menu.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_info.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_display.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_bluetooth.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_clock.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_power.c
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_indicator.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_app.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_menu.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_packet.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_settings.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_cw.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_capture.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio/radio_noise.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss_menu.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss_status.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss_satellites.c
	${CMAKE_CURRENT_SOURCE_DIR}/gnss/gnss_settings.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_menu.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_qr.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_settings.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_radio.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore/meshcore_channels.c
	${CMAKE_CURRENT_SOURCE_DIR}/nodes/nodes_app.c
	${CMAKE_CURRENT_SOURCE_DIR}/messages/messages_app.c
)

zephyr_library_sources_ifdef(CONFIG_MESHBUS_TELEMETRY
	${CMAKE_CURRENT_SOURCE_DIR}/system/system_telemetry.c
)
