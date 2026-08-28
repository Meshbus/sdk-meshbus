#include <zephyr/linker/iterable_sections.h>

/*
 * Iterable sections used by this sample.
 *
 * Registered entries are created via STRUCT_SECTION_ITERABLE(meshbus_desktop_dashboard_widget_desc, ...)
 */
ITERABLE_SECTION_ROM(meshbus_desktop_dashboard_widget_desc, Z_LINK_ITERABLE_SUBALIGN)

/* Enforce at least one statically-registered widget. */
ASSERT(_meshbus_desktop_dashboard_widget_desc_list_end > _meshbus_desktop_dashboard_widget_desc_list_start,
       "ZUI Desktop: widgets iterable is empty; define at least one meshbus_desktop_dashboard_widget_desc")
