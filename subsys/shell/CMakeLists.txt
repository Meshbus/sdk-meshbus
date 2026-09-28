# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MBS_SHELL)
	zephyr_library_named(mbs_shell)
	zephyr_library_add_dependencies(mbs_proto)
	zephyr_library_sources(shell.c)
endif()
