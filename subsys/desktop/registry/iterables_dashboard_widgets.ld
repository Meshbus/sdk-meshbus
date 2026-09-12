#include <zephyr/linker/iterable_sections.h>

/*
 * Iterable sections used by this sample.
 *
 * Registered entries are created via STRUCT_SECTION_ITERABLE(mbs_desktop_dashboard_widget_desc, ...)
 */
ITERABLE_SECTION_ROM(mbs_desktop_dashboard_widget_desc, Z_LINK_ITERABLE_SUBALIGN)

/* Enforce at least one statically-registered widget. */
ASSERT(_mbs_desktop_dashboard_widget_desc_list_end > _mbs_desktop_dashboard_widget_desc_list_start,
       "ZUI Desktop: widgets iterable is empty; define at least one mbs_desktop_dashboard_widget_desc")
