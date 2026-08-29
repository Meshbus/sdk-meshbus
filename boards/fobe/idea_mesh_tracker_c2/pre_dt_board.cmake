# SPDX-License-Identifier: Apache-2.0
# Suppress "unique_unit_address_if_enabled" to handle some overlaps
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")

# Board extensions keep their variant DTS in a different board root. Make the
# base board directory available so those variants can reuse the hardware
# description includes without copying them into the application repository.
list(APPEND DTS_EXTRA_CPPFLAGS "-I${BOARD_DIR}")
