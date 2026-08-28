# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
if(CONFIG_MESHBUS_DESKTOP_TEXT_LANG_EN)
	zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/en/desktop_text.c)
endif()

if(CONFIG_MESHBUS_DESKTOP_TEXT_LANG_ZH_CN)
	zephyr_library_sources(${CMAKE_CURRENT_SOURCE_DIR}/zh_cn/desktop_text.c)
endif()
