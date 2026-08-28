# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(
	${CMAKE_CURRENT_SOURCE_DIR}/widget_common.c
	${CMAKE_CURRENT_SOURCE_DIR}/clock_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/meshcore_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/messages_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/nodes_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/radio_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/satellite_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/position_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/compass_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/usage_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/info_widget.c
	${CMAKE_CURRENT_SOURCE_DIR}/power_widget.c
)

zephyr_library_sources_ifdef(CONFIG_MESHBUS_TELEMETRY
	${CMAKE_CURRENT_SOURCE_DIR}/telemetry_widget.c
)
