# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(
	messages_cache.c
)

zephyr_library_sources_ifdef(CONFIG_MESHBUS_TELEMETRY
	telemetry_cache.c
)
