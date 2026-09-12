# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(mbs_settings)
zephyr_library_add_dependencies(mbs_proto)
zephyr_library_sources(settings.c)
