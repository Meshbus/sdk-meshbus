#include <zephyr/linker/iterable_sections.h>

/*
 * Iterable sections used by this sample.
 *
 * Registered entries are created via STRUCT_SECTION_ITERABLE(mbs_desktop_app_desc, ...)
 */
ITERABLE_SECTION_ROM(mbs_desktop_app_desc, Z_LINK_ITERABLE_SUBALIGN)

/* Enforce at least one statically-registered app. */
ASSERT(_mbs_desktop_app_desc_list_end > _mbs_desktop_app_desc_list_start,
       "ZUI Desktop: apps iterable is empty; define at least one mbs_desktop_app_desc")
