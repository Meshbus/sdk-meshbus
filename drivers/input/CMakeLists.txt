# Copyright (c) 2025 FoBE Projects
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_INPUT_TCA8418)
  zephyr_library()
  zephyr_library_sources(input_tca8418.c)
endif()
