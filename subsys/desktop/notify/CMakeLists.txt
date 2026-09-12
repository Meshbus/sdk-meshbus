# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(
	notify_bluetooth.c
)

zephyr_library_sources_ifdef(CONFIG_MBS_MESSAGE
	notify_message.c
)
