/* SPDX-License-Identifier: Apache-2.0 */

#include "widget_common.h"

const uint8_t B_dish_30x32[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x03, 0x00,
	0x00, 0xfc, 0x03, 0xf0, 0x00, 0x00, 0x0c, 0xf0, 0x00, 0x00, 0x0c, 0x30, 0x0f,
	0xf0, 0x30, 0x30, 0x0f, 0xf0, 0x30, 0x3c, 0xf0, 0x00, 0x33, 0x3c, 0xf0, 0x00,
	0x33, 0x33, 0x03, 0x3f, 0x33, 0x33, 0x03, 0x3f, 0x33, 0xc3, 0x30, 0x33, 0x30,
	0xc3, 0x30, 0x33, 0x30, 0xc3, 0x00, 0x3f, 0x00, 0xc3, 0x00, 0x3f, 0x00, 0x03,
	0xc3, 0xc0, 0x00, 0x03, 0xc3, 0xc0, 0x00, 0x03, 0x33, 0x0c, 0x03, 0x03, 0x33,
	0x0c, 0x03, 0x0c, 0x0c, 0x00, 0x03, 0x0c, 0x0c, 0x00, 0x03, 0x0c, 0xf0, 0x30,
	0x0c, 0x0c, 0xf0, 0x30, 0x0c, 0x30, 0x00, 0x0f, 0x0c, 0x30, 0x00, 0x0f, 0x0c,
	0xf0, 0x00, 0xf0, 0x0f, 0xf0, 0x00, 0xf0, 0x0f, 0x00, 0x0f, 0xc0, 0x00, 0x00,
	0x0f, 0xc0, 0x00, 0x00, 0xf0, 0x3f, 0x00, 0x00, 0xf0, 0x3f, 0x00};

void desktop_widget_strcpy(char *dst, size_t dst_size, const char *src)
{
	if (dst == NULL || dst_size == 0U) {
		return;
	}
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}

	(void)strncpy(dst, src, dst_size - 1U);
	dst[dst_size - 1U] = '\0';
}

void desktop_widget_frame(struct zui_draw_ctx *draw, int16_t x, int16_t y,
				 uint16_t width, uint16_t height)
{
	zui_draw_line(draw, (struct zui_point){.x = x + 2, .y = y},
		      (struct zui_point){.x = x + (int16_t)width - 2, .y = y});
	zui_draw_line(draw, (struct zui_point){.x = x + 1, .y = y + (int16_t)height - 1},
		      (struct zui_point){.x = x + (int16_t)width,
					 .y = y + (int16_t)height - 1});
	zui_draw_line(draw, (struct zui_point){.x = x + 2, .y = y + (int16_t)height},
		      (struct zui_point){.x = x + (int16_t)width - 1,
					 .y = y + (int16_t)height});
	zui_draw_line(draw, (struct zui_point){.x = x, .y = y + 2},
		      (struct zui_point){.x = x, .y = y + (int16_t)height - 2});
	zui_draw_line(draw, (struct zui_point){.x = x + (int16_t)width - 1, .y = y + 1},
		      (struct zui_point){.x = x + (int16_t)width - 1,
					 .y = y + (int16_t)height - 2});
	zui_draw_line(draw, (struct zui_point){.x = x + (int16_t)width, .y = y + 2},
		      (struct zui_point){.x = x + (int16_t)width,
					 .y = y + (int16_t)height - 2});
	zui_draw_dot(draw, (struct zui_point){.x = x + 1, .y = y + 1});
}

float desktop_widget_usage_ratio(size_t used, size_t total)
{
	float ratio;

	if (total == 0U) {
		return 0.0f;
	}

	ratio = (float)used / (float)total;
	if (ratio < 0.0f) {
		return 0.0f;
	}
	if (ratio > 1.0f) {
		return 1.0f;
	}

	return ratio;
}

bool desktop_widget_system_heap_stats_get(struct sys_memory_stats *stats)
{
	if (stats == NULL) {
		return false;
	}

	*stats = (struct sys_memory_stats){0};

#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && (K_HEAP_MEM_POOL_SIZE > 0)
	extern struct k_heap _system_heap;
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&_system_heap.lock);
	ret = sys_heap_runtime_stats_get(&_system_heap.heap, stats);
	k_spin_unlock(&_system_heap.lock, key);

	return ret == 0;
#else
	return false;
#endif
}

void desktop_widget_progress(struct zui_draw_ctx *draw, const struct zui_rect *rect,
				    float ratio, const char *text)
{
	struct zui_rect inner;
	struct zui_rect fill;
	uint16_t fill_width;

	if (draw == NULL || rect == NULL) {
		return;
	}

	if (ratio < 0.0f) {
		ratio = 0.0f;
	} else if (ratio > 1.0f) {
		ratio = 1.0f;
	}

	inner = (struct zui_rect){
		.x = rect->x + 1,
		.y = rect->y + 1,
		.width = rect->width > 2U ? rect->width - 2U : 0U,
		.height = rect->height > 2U ? rect->height - 2U : 0U,
	};
	fill_width = (uint16_t)(ratio * (float)inner.width);
	fill = (struct zui_rect){
		.x = inner.x,
		.y = inner.y,
		.width = fill_width,
		.height = inner.height,
	};

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, &inner);
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	zui_draw_round_rect(draw, rect, 3);
	zui_draw_box(draw, &fill);

	if (text != NULL) {
		zui_draw_set_color(draw, ZUI_COLOR_XOR);
		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		zui_draw_text_aligned(draw,
				      (struct zui_point){.x = rect->x + (int16_t)rect->width / 2,
							 .y = rect->y + 2},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_TOP, text);
		zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	}
}

const struct zui_icon *desktop_widget_common_icon(uint32_t icon_id)
{
	return zui_asset_pack_icon_by_id(zui_asset_pack_default(), icon_id);
}

void desktop_widget_hex_prefix_format(char *out, size_t out_size, const uint8_t *bytes,
					     size_t len)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}
	if (bytes == NULL || len == 0U) {
		desktop_widget_strcpy(out, out_size, DESKTOP_TEXT_COMMON_UNKNOWN);
		return;
	}

	for (size_t i = 0U; i < len && (off + 2U) < out_size; i++) {
		out[off++] = hex[(bytes[i] >> 4) & 0x0F];
		out[off++] = hex[bytes[i] & 0x0F];
	}
	out[off] = '\0';
}

uint32_t dashboard_widget_tick(struct meshbus_desktop_dashboard_widget *wctx)
{
	ARG_UNUSED(wctx);

	return DASHBOARD_WIDGET_TICK_MS;
}
