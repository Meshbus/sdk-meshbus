# SPDX-License-Identifier: Apache-2.0
# Suppress "unique_unit_address_if_enabled" to handle some overlaps
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")

# Allow application-owned board extensions to reuse this base board's DTSI
# files without copying the hardware description into the application.
list(APPEND DTS_EXTRA_CPPFLAGS "-I${BOARD_DIR}")
