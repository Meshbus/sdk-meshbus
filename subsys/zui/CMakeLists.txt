# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

# Meshbus SDK ZUI framework

zephyr_library()

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# NOTE: Sources are currently hosted under the ZUI sample tree.
# This subsystem CMakeLists is the integration point so applications can enable
# ZUI via CONFIG_ZUI=y. A follow-up change can relocate sources into this
# directory without changing the public API.

zephyr_library_sources(
	# ZUI logging module registration
	util/zui_log.c

	# ZUI Memory Pool Implementation
	zui_mem.c

	# New host/router/screen/draw API
	zui_host.c
	zui_screen.c
	zui_draw.c
	zui_assets.c
	zui_input.c
	components/actions.c
	components/blank.c
	components/composite.c
	components/element.c
	components/file_picker.c
	components/form.c
	components/hex_editor.c
	components/list.c
	components/modal.c
	components/number_editor.c
	components/progress.c
	components/sublist.c
	components/text_editor.c
	components/text_view.c
	zui_compress.c
	zui_toast.c
)

if(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zephyr_library_sources(zui_predictive.c)
	set(ZUI_PREDICTIVE_DICT_SOURCE "")
	set(ZUI_PREDICTIVE_DICT_SOURCE_LICENSE
	    "${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_SOURCE_LICENSE}")
	if(DEFINED CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE AND
	   NOT "${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE}" STREQUAL "")
		find_package(Python3 REQUIRED COMPONENTS Interpreter)
		set(ZUI_PREDICTIVE_DICT_SOURCE "${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE}")
		if(NOT IS_ABSOLUTE "${ZUI_PREDICTIVE_DICT_SOURCE}")
			get_filename_component(
				ZUI_PREDICTIVE_DICT_SOURCE
				"${APPLICATION_SOURCE_DIR}/${ZUI_PREDICTIVE_DICT_SOURCE}"
				ABSOLUTE
			)
		endif()
	elseif(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_BUILTIN_ENGLISH_DICT)
		find_package(Python3 REQUIRED COMPONENTS Interpreter)
		set(ZUI_PREDICTIVE_DICT_SOURCE
		    "${CMAKE_CURRENT_SOURCE_DIR}/dicts/en_subtlex_words.txt")
		set(ZUI_PREDICTIVE_DICT_SOURCE_LICENSE
		    "ISC; derived from words/subtlex-word-frequencies")
	endif()

	if(NOT "${ZUI_PREDICTIVE_DICT_SOURCE}" STREQUAL "")
		set(ZUI_PREDICTIVE_DICT_OUTPUT
		    "${CMAKE_CURRENT_BINARY_DIR}/zui_predictive_dict_generated.c")
		add_custom_command(
			OUTPUT "${ZUI_PREDICTIVE_DICT_OUTPUT}"
			COMMAND
				${Python3_EXECUTABLE}
				"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/zui/gen_predictive_dict.py"
				--input "${ZUI_PREDICTIVE_DICT_SOURCE}"
				--output "${ZUI_PREDICTIVE_DICT_OUTPUT}"
				--budget "${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SIZE}"
				--max-candidates
				"${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_CANDIDATES}"
				--max-word-len
				"${CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_MAX_SEQUENCE_LEN}"
				--source-note "${ZUI_PREDICTIVE_DICT_SOURCE}"
				--source-license
				"${ZUI_PREDICTIVE_DICT_SOURCE_LICENSE}"
			DEPENDS
				"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/zui/gen_predictive_dict.py"
				"${ZUI_PREDICTIVE_DICT_SOURCE}"
			VERBATIM
		)
		zephyr_library_sources("${ZUI_PREDICTIVE_DICT_OUTPUT}")
	else()
		zephyr_library_sources(zui_predictive_dict_empty.c)
	endif()
endif()

zephyr_library_sources_ifdef(CONFIG_ZUI_COMMON_ICONS
	zui_common_icons.c
)

zephyr_library_sources_ifdef(CONFIG_LLEXT_EXPORT_SYMBOL_GROUP_ZUI
	zui_llext_exports.c
)

zephyr_library_sources_ifdef(CONFIG_ZUI_SHELL
	zui_shell.c
)
