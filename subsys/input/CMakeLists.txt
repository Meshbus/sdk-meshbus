# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_INPUT)
	zephyr_library_named(mbs_input)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/../mgmt
		${CMAKE_CURRENT_SOURCE_DIR}/../shell
	)
	zephyr_library_sources(input.c)
	zephyr_library_sources_ifdef(CONFIG_MCUMGR mgmt.c)

	# Button: compile if chosen meshbus,input-buttons exists
	dt_chosen(chosen_buttons PROPERTY "meshbus,input-buttons")

	if(chosen_buttons AND CONFIG_MBS_INPUT_BUTTON)
		zephyr_library_sources(input_button.c)
	endif()

	# Encoder: compile if chosen meshbus,input-encoder or meshbus,input-encoder-sw exists
	dt_chosen(chosen_encoder PROPERTY "meshbus,input-encoder")

	if((chosen_encoder AND CONFIG_MBS_INPUT_ENCODER))
		zephyr_library_sources(input_encoder.c)
	endif()

	dt_chosen(chosen_keypad PROPERTY "meshbus,input-keypad")

	if(chosen_keypad AND CONFIG_MBS_INPUT_KEYPAD)
		zephyr_library_sources(input_keypad.c)
	endif()

	zephyr_library_sources_ifdef(CONFIG_MBS_INPUT_SHELL
		shell.c
	)
endif()
