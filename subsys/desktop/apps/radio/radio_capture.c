/* SPDX-License-Identifier: Apache-2.0 */

#include "radio_private.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include "assets/assets_icons.h"
#include "text/desktop_text.h"

static atomic_ptr_t radio_capture_app_ptr;
static atomic_t radio_capture_cb_inflight;
K_SEM_DEFINE(radio_capture_cb_idle_sem, 0, K_SEM_MAX_LIMIT);

static void radio_capture_cb_end(void)
{
	if (atomic_dec(&radio_capture_cb_inflight) == 1) {
		k_sem_give(&radio_capture_cb_idle_sem);
	}
}

static void radio_capture_listener_cb(const struct zbus_channel *chan)
{
	const struct mbs_radio_receive_event *evt;
	struct radio_app *app;
	struct radio_packet_capture_entry *entry;
	k_spinlock_key_t key;
	uint8_t copy_len;

	atomic_inc(&radio_capture_cb_inflight);
	app = (struct radio_app *)atomic_ptr_get(&radio_capture_app_ptr);
	if (app == NULL || atomic_get(&app->capture_enabled) == 0 ||
	    chan != &mbs_radio_receive_chan) {
		goto out;
	}

	evt = (const struct mbs_radio_receive_event *)zbus_chan_const_msg(chan);
	if (evt == NULL) {
		goto out;
	}

	copy_len = (evt->len > RADIO_PACKET_CAPTURE_DATA_MAX) ? RADIO_PACKET_CAPTURE_DATA_MAX :
								(uint8_t)evt->len;
	key = k_spin_lock(&app->capture_lock);
	entry = &app->capture_cache.entry;
	entry->ts_ms = k_uptime_get_32();
	entry->raw_len = evt->len;
	entry->len = copy_len;
	entry->rssi = evt->rssi;
	entry->snr = evt->snr;
	memset(entry->data, 0, sizeof(entry->data));
	memcpy(entry->data, evt->data, copy_len);
	app->capture_cache.count = 1U;
	app->capture_cache.seq++;
	atomic_set(&app->capture_dirty, 1);
	k_spin_unlock(&app->capture_lock, key);

out:
	radio_capture_cb_end();
}

ZBUS_LISTENER_DEFINE(radio_rx_listener, radio_capture_listener_cb);
ZBUS_CHAN_ADD_OBS(mbs_radio_receive_chan, radio_rx_listener, 1);

void radio_capture_set_enabled(bool enabled)
{
	(void)zbus_obs_set_enable(&radio_rx_listener, enabled);
}

void radio_capture_attach_app(struct radio_app *app)
{
	atomic_ptr_set(&radio_capture_app_ptr, (atomic_ptr_val_t)app);
}

bool radio_capture_is_attached(struct radio_app *app)
{
	return (struct radio_app *)atomic_ptr_get(&radio_capture_app_ptr) == app;
}

void radio_capture_detach_app(struct radio_app *app)
{
	if (radio_capture_is_attached(app)) {
		atomic_ptr_set(&radio_capture_app_ptr, (atomic_ptr_val_t)NULL);
	}
}

void radio_capture_wait_idle(void)
{
	while (k_sem_take(&radio_capture_cb_idle_sem, K_NO_WAIT) == 0) {
	}
	while (atomic_get(&radio_capture_cb_inflight) > 0) {
		(void)k_sem_take(&radio_capture_cb_idle_sem, K_FOREVER);
	}
}

void radio_capture_reset(struct radio_app *app)
{
	k_spinlock_key_t key;
	bool running;
	bool decode;

	if (app == NULL) {
		return;
	}

	running = app->capture_model.running;
	decode = app->capture_model.decode;
	key = k_spin_lock(&app->capture_lock);
	memset(&app->capture_cache, 0, sizeof(app->capture_cache));
	app->capture_cache.hex_visible_lines = 1U;
	atomic_set(&app->capture_dirty, 1);
	k_spin_unlock(&app->capture_lock, key);

	memset(&app->capture_model, 0, sizeof(app->capture_model));
	app->capture_model.running = running;
	app->capture_model.decode = decode;
	app->capture_model.hex_visible_lines = 1U;
}

bool radio_capture_drain(struct radio_app *app)
{
	k_spinlock_key_t key;
	bool keep_running;
	bool keep_decode;
	uint8_t keep_scroll;
	uint8_t keep_total;
	uint8_t keep_visible;
	uint32_t prev_seq;

	if (app == NULL || atomic_get(&app->capture_dirty) == 0) {
		return false;
	}

	keep_running = app->capture_model.running;
	keep_decode = app->capture_model.decode;
	keep_scroll = app->capture_model.hex_scroll_line;
	keep_total = app->capture_model.hex_total_lines;
	keep_visible = app->capture_model.hex_visible_lines;
	prev_seq = app->capture_model.seq;

	key = k_spin_lock(&app->capture_lock);
	app->capture_model = app->capture_cache;
	atomic_clear(&app->capture_dirty);
	k_spin_unlock(&app->capture_lock, key);

	app->capture_model.running = keep_running;
	app->capture_model.decode = keep_decode;
	app->capture_model.hex_total_lines = keep_total;
	app->capture_model.hex_visible_lines = keep_visible == 0U ? 1U : keep_visible;
	app->capture_model.hex_scroll_line = (app->capture_model.seq != prev_seq) ? 0U : keep_scroll;
	return true;
}
static void radio_capture_draw_text_line(struct zui_draw_ctx *draw, uint8_t line_idx,
					 uint8_t start_line, uint8_t visible_lines,
					 uint8_t line_step, int16_t x, int16_t y,
					 const char *line)
{
	uint16_t end_line;

	if (draw == NULL || line == NULL || visible_lines == 0U || line_step == 0U) {
		return;
	}

	end_line = (uint16_t)start_line + visible_lines;
	if (line_idx < start_line || line_idx >= end_line) {
		return;
	}

	zui_draw_text(draw,
		      (struct zui_point){.x = x,
					 .y = (int16_t)(y + ((line_idx - start_line) *
							    line_step))},
		      line);
}

static uint8_t radio_capture_render_hex(struct zui_draw_ctx *draw, const uint8_t *data,
					uint8_t len, uint8_t start_line,
					uint8_t visible_lines, uint8_t line_step,
					int16_t x, int16_t y, bool draw_lines)
{
	char line[48];
	size_t line_len = 0U;
	uint8_t line_idx = 0U;

	if (draw == NULL) {
		return 0U;
	}

	if (data == NULL || len == 0U) {
		if (draw_lines) {
			radio_capture_draw_text_line(draw, 0U, start_line, visible_lines,
						     line_step, x, y, DESKTOP_TEXT_RADIO_EMPTY);
		}
		return 1U;
	}

	line[0] = '\0';
	for (uint8_t i = 0U; i < len; i++) {
		int rc;
		size_t prev_len = line_len;

		if (line_len == 0U) {
			rc = snprintk(line, sizeof(line), "%02X", data[i]);
			line_len = rc > 0 ? (size_t)rc : 0U;
			continue;
		}

		rc = snprintk(&line[line_len], sizeof(line) - line_len, " %02X", data[i]);
		if (rc > 0) {
			line_len += (size_t)rc;
		}

		if (zui_draw_text_width(draw, line) > RADIO_PACKET_CAPTURE_HEX_W) {
			line[prev_len] = '\0';
			if (draw_lines) {
				radio_capture_draw_text_line(draw, line_idx, start_line,
							     visible_lines, line_step, x, y,
							     line);
			}
			line_idx++;

			rc = snprintk(line, sizeof(line), "%02X", data[i]);
			line_len = rc > 0 ? (size_t)rc : 0U;
		}
	}

	if (line_len > 0U) {
		if (draw_lines) {
			radio_capture_draw_text_line(draw, line_idx, start_line, visible_lines,
						     line_step, x, y, line);
		}
		line_idx++;
	}

	return line_idx;
}

static char radio_capture_decode_char(uint8_t value)
{
	if (value >= ' ' && value <= '~') {
		return (char)value;
	}
	if (value == '\t') {
		return ' ';
	}

	return '.';
}

static uint8_t radio_capture_render_text(struct zui_draw_ctx *draw, const uint8_t *data,
					 uint8_t len, uint8_t start_line,
					 uint8_t visible_lines, uint8_t line_step,
					 int16_t x, int16_t y, bool draw_lines)
{
	char line[48];
	size_t line_len = 0U;
	uint8_t line_idx = 0U;

	if (draw == NULL) {
		return 0U;
	}

	if (data == NULL || len == 0U) {
		if (draw_lines) {
			radio_capture_draw_text_line(draw, 0U, start_line, visible_lines,
						     line_step, x, y, DESKTOP_TEXT_RADIO_EMPTY);
		}
		return 1U;
	}

	line[0] = '\0';
	for (uint8_t i = 0U; i < len; i++) {
		char decoded = radio_capture_decode_char(data[i]);

		if (data[i] == '\r') {
			continue;
		}
		if (data[i] == '\n') {
			if (draw_lines) {
				radio_capture_draw_text_line(draw, line_idx, start_line,
							     visible_lines, line_step, x, y,
							     line);
			}
			line_idx++;
			line_len = 0U;
			line[0] = '\0';
			continue;
		}
		if (line_len + 1U >= sizeof(line)) {
			if (draw_lines) {
				radio_capture_draw_text_line(draw, line_idx, start_line,
							     visible_lines, line_step, x, y,
							     line);
			}
			line_idx++;
			line_len = 0U;
		}

		line[line_len++] = decoded;
		line[line_len] = '\0';
		if (zui_draw_text_width(draw, line) > RADIO_PACKET_CAPTURE_HEX_W) {
			line[--line_len] = '\0';
			if (draw_lines) {
				radio_capture_draw_text_line(draw, line_idx, start_line,
							     visible_lines, line_step, x, y,
							     line);
			}
			line_idx++;
			line[0] = decoded;
			line[1] = '\0';
			line_len = 1U;
		}
	}

	if (line_len > 0U || line_idx == 0U) {
		if (draw_lines) {
			radio_capture_draw_text_line(draw, line_idx, start_line, visible_lines,
						     line_step, x, y, line);
		}
		line_idx++;
	}

	return line_idx;
}

static void radio_draw_scrollbar_pos(struct zui_draw_ctx *draw, int16_t x, int16_t y,
				     uint16_t height, size_t pos, size_t total)
{
	uint16_t block_h;
	uint16_t block_y;

	if (draw == NULL || height == 0U) {
		return;
	}

	zui_draw_set_color(draw, ZUI_COLOR_WHITE);
	zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(x - 3),
					      .y = y,
					      .width = 3U,
					      .height = height});
	zui_draw_set_color(draw, ZUI_COLOR_BLACK);
	for (int16_t i = y; i < y + (int16_t)height; i += 2) {
		zui_draw_dot(draw, (struct zui_point){.x = (int16_t)(x - 2), .y = i});
	}
	if (total == 0U) {
		return;
	}

	block_h = MAX((uint16_t)(height / total), 1U);
	block_y = (uint16_t)(y + ((uint32_t)height * pos) / total);
	zui_draw_box(draw, &(struct zui_rect){.x = (int16_t)(x - 3),
					      .y = (int16_t)block_y,
					      .width = 3U,
					      .height = block_h});
}

static void radio_capture_set_running(struct radio_app *app, bool running)
{
	if (app == NULL) {
		return;
	}

	app->capture_model.running = running;
	if (running) {
		radio_capture_reset(app);
	}
	atomic_set(&app->capture_enabled, running ? 1 : 0);
	radio_capture_set_enabled(running);
	radio_schedule_tick(app);
	radio_request_redraw(app);
}

static void radio_capture_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct radio_app *app = user_data;
	struct radio_packet_capture_model *m = &app->capture_model;
	const struct zui_icon *right_icon;
	struct zui_font_metrics metrics;
	size_t line_step;
	size_t font_h;
	uint8_t max_scroll = 0U;
	char line[32];

	zui_draw_clear(draw);
	zui_draw_set_font(draw, ZUI_FONT_PRIMARY);
	zui_draw_text(draw, (struct zui_point){2, 10}, DESKTOP_TEXT_RADIO_CAPTURE);
	right_icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
					       ZUI_ASSET_ICON_BUTTON_RIGHT);
	zui_draw_icon(draw, (struct zui_point){51, 3}, right_icon);
	zui_draw_text(draw, (struct zui_point){47, 62}, DESKTOP_TEXT_RADIO_PACKET);
	zui_draw_round_rect(draw, &(struct zui_rect){1, 13, 56, 12}, 3);
	zui_draw_round_rect(draw, &(struct zui_rect){1, 26, 56, 12}, 3);
	zui_draw_round_rect(draw, &(struct zui_rect){1, 39, 56, 12}, 3);
	zui_draw_round_rect(draw, &(struct zui_rect){58, 1, 69, 50}, 3);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){3, 23}, DESKTOP_TEXT_RADIO_RSSI);
	zui_draw_text(draw, (struct zui_point){3, 36}, DESKTOP_TEXT_RADIO_SNR);
	zui_draw_text(draw, (struct zui_point){3, 49}, DESKTOP_TEXT_RADIO_LEN);
	if (m->count == 0U) {
		zui_draw_text_aligned(draw, (struct zui_point){54, 15}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, DESKTOP_TEXT_RADIO_DASH);
		zui_draw_text_aligned(draw, (struct zui_point){54, 28}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, DESKTOP_TEXT_RADIO_DASH);
		zui_draw_text_aligned(draw, (struct zui_point){54, 41}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, DESKTOP_TEXT_RADIO_DASH);
		m->hex_scroll_line = 0U;
		m->hex_total_lines = 0U;
		m->hex_visible_lines = 1U;
	} else {
		(void)snprintk(line, sizeof(line), "%d", (int)m->entry.rssi);
		zui_draw_text_aligned(draw, (struct zui_point){54, 15}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, line);
		(void)snprintk(line, sizeof(line), "%d", (int)m->entry.snr);
		zui_draw_text_aligned(draw, (struct zui_point){54, 28}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, line);
		if (m->entry.raw_len > m->entry.len) {
			(void)snprintk(line, sizeof(line), "%u+", (unsigned int)m->entry.raw_len);
		} else {
			(void)snprintk(line, sizeof(line), "%u", (unsigned int)m->entry.raw_len);
		}
		zui_draw_text_aligned(draw, (struct zui_point){54, 41}, ZUI_ALIGN_RIGHT,
				      ZUI_ALIGN_TOP, line);

		zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
		if (zui_draw_font_metrics(draw, ZUI_FONT_SECONDARY, &metrics) != 0) {
			metrics.leading_default = 8U;
			metrics.height = 8U;
		}
		line_step = metrics.leading_default == 0U ? 1U : metrics.leading_default;
		font_h = metrics.height == 0U ? 1U : metrics.height;
		m->hex_visible_lines = (uint8_t)(RADIO_PACKET_CAPTURE_HEX_H / line_step);
		if (m->hex_visible_lines == 0U) {
			m->hex_visible_lines = 1U;
		}
		if (m->decode) {
			m->hex_total_lines = radio_capture_render_text(draw, m->entry.data,
								       m->entry.len, 0U, 0U,
								       (uint8_t)line_step, 0, 0,
								       false);
		} else {
			m->hex_total_lines = radio_capture_render_hex(draw, m->entry.data,
								      m->entry.len, 0U, 0U,
								      (uint8_t)line_step, 0, 0,
								      false);
		}
		if (m->hex_total_lines > m->hex_visible_lines) {
			max_scroll = m->hex_total_lines - m->hex_visible_lines;
		}
		if (m->hex_scroll_line > max_scroll) {
			m->hex_scroll_line = max_scroll;
		}
		if (m->decode) {
			(void)radio_capture_render_text(
				draw, m->entry.data, m->entry.len, m->hex_scroll_line,
				m->hex_visible_lines, (uint8_t)line_step, RADIO_PACKET_CAPTURE_HEX_X,
				RADIO_PACKET_CAPTURE_HEX_Y + (int16_t)font_h, true);
		} else {
			(void)radio_capture_render_hex(
				draw, m->entry.data, m->entry.len, m->hex_scroll_line,
				m->hex_visible_lines, (uint8_t)line_step, RADIO_PACKET_CAPTURE_HEX_X,
				RADIO_PACKET_CAPTURE_HEX_Y + (int16_t)font_h, true);
		}
		radio_draw_scrollbar_pos(draw, RADIO_PACKET_CAPTURE_HEX_SCROLL_X,
					 RADIO_PACKET_CAPTURE_HEX_Y,
					 RADIO_PACKET_CAPTURE_HEX_H,
					 m->hex_scroll_line, (size_t)max_scroll + 1U);
	}
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
		.left = DESKTOP_TEXT_COMMON_BUTTON_CLEAR,
		.center = m->decode ? DESKTOP_TEXT_RADIO_TEXT : DESKTOP_TEXT_RADIO_HEX,
		.right = m->running ? DESKTOP_TEXT_COMMON_BUTTON_STOP :
				      DESKTOP_TEXT_COMMON_BUTTON_START,
	});
}

static bool radio_capture_input(const struct zui_input_event *event, void *user_data)
{
	struct radio_app *app = user_data;
	uint8_t max_scroll;

	if (app == NULL || event == NULL) {
		return false;
	}
	if (app->capture_press_pending && event->code == app->capture_pending_code &&
	    (event->action == ZUI_INPUT_ACTION_CLICK || event->action == ZUI_INPUT_ACTION_RELEASE)) {
		if (event->action == ZUI_INPUT_ACTION_RELEASE) {
			app->capture_press_pending = false;
		}
		return true;
	}
	if (event->action == ZUI_INPUT_ACTION_PRESS &&
	    (event->code == ZUI_INPUT_CODE_RIGHT || event->code == ZUI_INPUT_CODE_SELECT ||
	     event->code == ZUI_INPUT_CODE_LEFT)) {
		app->capture_press_pending = true;
		app->capture_pending_code = event->code;
		if (event->code == ZUI_INPUT_CODE_RIGHT) {
			radio_capture_set_running(app, !app->capture_model.running);
		} else if (event->code == ZUI_INPUT_CODE_SELECT) {
			app->capture_model.decode = !app->capture_model.decode;
			app->capture_model.hex_scroll_line = 0U;
			radio_request_redraw(app);
		} else {
			radio_capture_reset(app);
			radio_request_redraw(app);
		}
		return true;
	}
	if (radio_should_consume_edge(event)) {
		return true;
	}
	if (radio_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		radio_capture_set_running(app, false);
		radio_switch(app, RADIO_SCREEN_PACKET);
		return true;
	}
	if (radio_is_click(event) &&
	    (event->code == ZUI_INPUT_CODE_UP || event->code == ZUI_INPUT_CODE_DOWN)) {
		max_scroll = (app->capture_model.hex_total_lines > app->capture_model.hex_visible_lines) ?
				     (app->capture_model.hex_total_lines -
				      app->capture_model.hex_visible_lines) :
				     0U;
		if (event->code == ZUI_INPUT_CODE_UP && app->capture_model.hex_scroll_line > 0U) {
			app->capture_model.hex_scroll_line--;
		} else if (event->code == ZUI_INPUT_CODE_DOWN &&
			   app->capture_model.hex_scroll_line < max_scroll) {
			app->capture_model.hex_scroll_line++;
		}
		radio_request_redraw(app);
		return true;
	}
	if (!radio_is_click(event)) {
		return false;
	}
	if (event->code == ZUI_INPUT_CODE_RIGHT) {
		radio_capture_set_running(app, !app->capture_model.running);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_SELECT) {
		app->capture_model.decode = !app->capture_model.decode;
		app->capture_model.hex_scroll_line = 0U;
		radio_request_redraw(app);
		return true;
	}
	if (event->code == ZUI_INPUT_CODE_LEFT) {
		radio_capture_reset(app);
		radio_request_redraw(app);
		return true;
	}

	return false;
}
const struct zui_screen_ops radio_capture_ops = {
	.draw = radio_capture_draw,
	.input = radio_capture_input,
};
