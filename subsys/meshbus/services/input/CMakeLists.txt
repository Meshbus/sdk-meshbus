# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHBUS_INPUT)
	zephyr_library_sources(input.c)
	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)

	# Button: compile if chosen meshbus,input-buttons exists
	dt_chosen(chosen_buttons PROPERTY "meshbus,input-buttons")

	if(chosen_buttons AND CONFIG_MESHBUS_INPUT_BUTTON)
		zephyr_library_sources(input_button.c)
	endif()

	# Encoder: compile if chosen meshbus,input-encoder or meshbus,input-encoder-sw exists
	dt_chosen(chosen_encoder PROPERTY "meshbus,input-encoder")

	if((chosen_encoder AND CONFIG_MESHBUS_INPUT_ENCODER))
		zephyr_library_sources(input_encoder.c)
	endif()

	dt_chosen(chosen_keypad PROPERTY "meshbus,input-keypad")

	if(chosen_keypad AND CONFIG_MESHBUS_INPUT_KEYPAD)
		zephyr_library_sources(input_keypad.c)
	endif()

	zephyr_library_sources_ifdef(CONFIG_MESHBUS_INPUT_SHELL
		shell.c
	)
endif()
