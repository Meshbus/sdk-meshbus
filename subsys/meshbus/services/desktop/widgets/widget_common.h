/* SPDX-License-Identifier: Apache-2.0 */

#ifndef FOOBE_MESHBUS_DESKTOP_WIDGET_COMMON_H_
#define FOOBE_MESHBUS_DESKTOP_WIDGET_COMMON_H_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/meshbus/clock.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/meshbus/channel.h>
#if defined(CONFIG_MESHBUS_GNSS)
#include <zephyr/meshbus/gnss.h>
#endif
#include <zephyr/meshbus/contact.h>
#include <zephyr/meshbus/power.h>
#if defined(CONFIG_MESHBUS_RADIO)
#include <zephyr/meshbus/radio.h>
#endif
#include <zephyr/sys/clock.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/zui/zui.h>

#include "apps/app_ids.h"
#include "assets/assets_fonts.h"
#include "assets/assets_icons.h"
#include "assets/assets_xbms.h"
#include "desktop_private.h"
#include "services/messages_cache.h"
#include "text/desktop_text.h"
#include "widget_ids.h"

#define DASHBOARD_WIDGET_TICK_MS 1000U
#define INFO_WIDGET_MINUTE_MS 60000U
#define INFO_WIDGET_MIN_REFRESH_MS 1000U
#define INFO_WIDGET_DEVICE_NAME_MAX_LEN 32
#define INFO_WIDGET_VERSION_MAX_LEN 24
#define INFO_WIDGET_SERIAL_MAX_LEN 33
#define INFO_WIDGET_DEVICE_ID_BUF_LEN ((INFO_WIDGET_SERIAL_MAX_LEN - 1U) / 2U)
#define CLOCK_WIDGET_TICK_READY_MS 1000U
#define CLOCK_WIDGET_TICK_UNAVAILABLE_MS 3000U
#define CLOCK_WIDGET_SYNC_FALLBACK_UNIX 1767225600LL
#define POWER_WIDGET_TICK_ACTIVE_MS 1000U
#define POWER_WIDGET_TICK_IDLE_MS 3000U
#define POWER_WIDGET_TICK_OFF_MS 6000U
#define MESHCORE_WIDGET_TICK_MS 600U
#define MESHCORE_WIDGET_NODE_PREFIX_BYTES CONFIG_MESHBUS_CONTACT_PREFIX_BYTES
#define MESHCORE_WIDGET_ROLE_STR_MAX 10U
#define MESHCORE_WIDGET_NODE_ID_STR_MAX (MESHCORE_WIDGET_NODE_PREFIX_BYTES * 2U + 1U)

extern const uint8_t B_dish_30x32[];

void desktop_widget_strcpy(char *dst, size_t dst_size, const char *src);
void desktop_widget_frame(struct zui_draw_ctx *draw, int16_t x, int16_t y,
			  uint16_t width, uint16_t height);
float desktop_widget_usage_ratio(size_t used, size_t total);
bool desktop_widget_system_heap_stats_get(struct sys_memory_stats *stats);
void desktop_widget_progress(struct zui_draw_ctx *draw, const struct zui_rect *rect,
			     float ratio, const char *text);
const struct zui_icon *desktop_widget_common_icon(uint32_t icon_id);
void desktop_widget_hex_prefix_format(char *out, size_t out_size, const uint8_t *bytes,
				      size_t len);
uint32_t dashboard_widget_tick(struct meshbus_desktop_dashboard_widget *wctx);

#endif /* FOOBE_MESHBUS_DESKTOP_WIDGET_COMMON_H_ */
