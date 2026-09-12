/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <desktop/desktop.h>
#include <contact/contact.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/zui/zui.h>

#include "apps/app_ids.h"
#include "assets/assets_icons.h"
#include "text/desktop_text.h"

LOG_MODULE_REGISTER(mbs_desktop_nodes, CONFIG_MBS_DESKTOP_LOG_LEVEL);

#define NODES_SCREEN_LIST   1U
#define NODES_SCREEN_MENU   2U
#define NODES_SCREEN_ALIAS  3U
#define NODES_SCREEN_DETAIL 4U
#define NODES_SCREEN_MODAL  5U

#define NODES_LIST_ITEM_EMPTY 0U
#define NODES_LIST_ITEM_BASE  1U

#define NODES_MENU_ALIAS      1U
#define NODES_MENU_NAME	      2U
#define NODES_MENU_ROLE	      3U
#define NODES_MENU_PUBLIC_KEY 4U
#define NODES_MENU_PERMISSION 5U
#define NODES_MENU_TELEMETRY  6U
#define NODES_MENU_DISCOVER   7U
#define NODES_MENU_TRACE      8U
#define NODES_MENU_BROADCAST  9U
#define NODES_PERMISSION_BASE 0x40U
#define NODES_PERMISSION_LOCATION 0x41U
#define NODES_PERMISSION_ENVIRONMENT 0x42U
#define NODES_MENU_APPLY      0xE0U
#define NODES_MENU_DELETE     0xE2U

#define NODES_MENU_ITEMS_MAX 12U
#define NODES_NAME_MAX	     32U
#define NODES_TEXT_MAX	     256U
#define NODES_VALUE_MAX	     32U
#define NODES_PUBLIC_KEY_PREFIX_SIZE CONFIG_MBS_CONTACT_PREFIX_BYTES
#define NODES_PREFIX_HEX_MAX	     (NODES_PUBLIC_KEY_PREFIX_SIZE * 2U + 1U)
#define NODES_PUBLIC_KEY_HEX_MAX    (MBS_CONTACT_PUBLIC_KEY_SIZE * 2U + 1U)
#define NODES_OUT_PATH_MAX	     192U
#define NODES_SNAPSHOT_DELAY_MS     50U
#define NODES_EVENT_WORK_DELAY_MS  20U
#define NODES_REQUEST_TIMEOUT_MS    ((uint32_t)CONFIG_MBS_CONTACT_REQUEST_TIMEOUT_MS)
#define NODES_ROW_CACHE_SIZE	     3U

#define NODES_EVENT_FLAG_SNAPSHOT  BIT(0)
#define NODES_EVENT_FLAG_DISCOVER  BIT(1)
#define NODES_EVENT_FLAG_TRACE     BIT(2)
#define NODES_EVENT_FLAG_TELEMETRY BIT(3)

struct nodes_contact_row {
	struct zui_list_item item;
	char label[NODES_NAME_MAX];
	char detail[NODES_VALUE_MAX];
	char name[NODES_NAME_MAX];
	char alias[NODES_NAME_MAX];
	char id_short[NODES_PREFIX_HEX_MAX];
	uint8_t public_key[MBS_CONTACT_PUBLIC_KEY_SIZE];
	uint8_t public_key_len;
	uint8_t out_path[MBS_CONTACT_OUTPATH_MAX_LEN];
	uint8_t out_path_len;
	uint8_t path_hash_size;
	uint8_t prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t slot;
	mbs_contact_role role;
	uint32_t flags;
	bool is_neighbor;
	bool has_position;
	int32_t latitude_e7;
	int32_t longitude_e7;
};

struct nodes_contact_row_cache_entry {
	struct nodes_contact_row row;
	size_t index;
	uint8_t slot;
	bool valid;
};

struct nodes_discover_event {
	uint8_t prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t path_len;
	uint8_t path[MBS_CONTACT_OUTPATH_MAX_LEN];
};

struct nodes_trace_event {
	uint8_t prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t state;
};

struct nodes_telemetry_event {
	uint8_t prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t payload_len;
	uint8_t payload[MBS_CONTACT_TELEMETRY_PAYLOAD_MAX_LEN];
};

struct nodes_telemetry_parse_result {
	bool has_battery_pct;
	uint8_t battery_pct;
	bool has_latitude_e7;
	int32_t latitude_e7;
	bool has_longitude_e7;
	int32_t longitude_e7;
};

enum nodes_detail_kind {
	NODES_DETAIL_NONE,
	NODES_DETAIL_PUBLIC_KEY,
	NODES_DETAIL_TELEMETRY,
	NODES_DETAIL_DISCOVER_PATH,
	NODES_DETAIL_TRACE_PATH,
};

enum nodes_modal_kind {
	NODES_MODAL_NONE,
	NODES_MODAL_DELETE,
};

enum nodes_menu_page {
	NODES_MENU_PAGE_ROOT,
	NODES_MENU_PAGE_PERMISSION,
};

struct nodes_app {
	struct zui_host *host;
	struct zui_router *router;
	struct k_sem exit_sem;
	bool exit_requested;

	struct zui_sublist *list;
	struct zui_form *menu;
	struct zui_text_editor *alias_editor;
	struct zui_text_view *detail_view;
	struct zui_modal *modal;

	struct zui_screen *list_screen;
	struct zui_screen *menu_screen;
	struct zui_screen *alias_screen;
	struct zui_screen *detail_screen;
	struct zui_screen *modal_screen;

	struct k_work_delayable snapshot_work;
	struct k_work_delayable request_timeout_work;
	struct k_work_delayable event_work;
	struct k_spinlock event_lock;
	uint32_t event_flags;
	struct nodes_discover_event discover_event;
	struct nodes_trace_event trace_event;
	struct nodes_telemetry_event telemetry_event;

	struct zui_list_item empty_item;
	uint8_t contact_slots[CONFIG_MBS_CONTACT_MAX_CONTACTS];
	struct nodes_contact_row selected_row;
	struct nodes_contact_row_cache_entry row_cache[NODES_ROW_CACHE_SIZE];
	size_t contact_count;
	size_t selected_index;
	uint8_t row_cache_next;
	bool selected_row_valid;
	enum nodes_menu_page menu_page;
	size_t menu_root_selected;
	struct zui_form_item menu_items[NODES_MENU_ITEMS_MAX];
	char menu_values[NODES_MENU_ITEMS_MAX][NODES_VALUE_MAX];
	size_t menu_item_count;

	char alias_buf[NODES_NAME_MAX];
	char selected_public_key[NODES_PUBLIC_KEY_HEX_MAX];
	char selected_out_path[NODES_OUT_PATH_MAX];
	char detail_text[NODES_TEXT_MAX];
	char modal_text[NODES_TEXT_MAX];
	uint32_t editing_flags;
	bool discover_req_pending;
	uint8_t discover_req_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	bool trace_req_pending;
	uint8_t trace_req_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	bool telemetry_req_pending;
	uint8_t telemetry_req_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	bool discover_response_valid;
	uint8_t discover_response_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	char discover_response_path[NODES_OUT_PATH_MAX];
	bool trace_response_valid;
	uint8_t trace_response_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	uint8_t trace_response_state;
	bool telemetry_response_valid;
	uint8_t telemetry_response_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE];
	bool telemetry_has_battery_pct;
	uint8_t telemetry_battery_pct;
	bool telemetry_has_position;
	int32_t telemetry_latitude_e7;
	int32_t telemetry_longitude_e7;
	uint32_t current_screen;
	enum nodes_detail_kind detail_kind;
	enum nodes_modal_kind modal_kind;
};

static void nodes_list_selected(struct zui_sublist *list, uint32_t id, size_t index,
				const struct zui_input_event *event, void *user_data);
static void nodes_menu_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data);
static void nodes_menu_changed(struct zui_form *form, uint32_t id, size_t option_index,
			       void *user_data);
static void nodes_alias_submitted(struct zui_text_editor *editor, const char *text,
				  void *user_data);
static void nodes_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			       const struct zui_input_event *event, void *user_data);
static void nodes_update_menu(struct nodes_app *app);
static void nodes_open_telemetry(struct nodes_app *app);
static void nodes_open_discover_path(struct nodes_app *app);
static void nodes_open_trace_path(struct nodes_app *app);
static void nodes_refresh_current_view(struct nodes_app *app);
static void nodes_snapshot_work(struct k_work *work);
static void nodes_request_timeout_work(struct k_work *work);
static void nodes_event_work(struct k_work *work);

static atomic_ptr_t nodes_active_app;
static atomic_t nodes_event_inflight;
K_SEM_DEFINE(nodes_event_idle, 0, K_SEM_MAX_LIMIT);

static bool nodes_flag_is_set(uint32_t flags, uint32_t bit)
{
	return (flags & bit) != 0U;
}

static void nodes_update_flag(uint32_t *flags, uint32_t bit, size_t option_index)
{
	if (flags == NULL) {
		return;
	}

	if (option_index != 0U) {
		*flags |= bit;
	} else {
		*flags &= ~bit;
	}
}

static bool nodes_is_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK;
}

static bool nodes_should_consume_edge(const struct zui_input_event *event)
{
	if (event == NULL ||
	    (event->action != ZUI_INPUT_ACTION_PRESS && event->action != ZUI_INPUT_ACTION_RELEASE)) {
		return false;
	}

	switch (event->code) {
	case ZUI_INPUT_CODE_UP:
	case ZUI_INPUT_CODE_DOWN:
	case ZUI_INPUT_CODE_LEFT:
	case ZUI_INPUT_CODE_RIGHT:
	case ZUI_INPUT_CODE_SELECT:
	case ZUI_INPUT_CODE_BACK:
		return true;
	default:
		return false;
	}
}

static void nodes_request_redraw(struct nodes_app *app)
{
	if (app != NULL && app->host != NULL) {
		(void)zui_host_request_redraw(app->host);
	}
}

static void nodes_switch(struct nodes_app *app, uint32_t screen_id)
{
	if (app == NULL || app->router == NULL) {
		return;
	}
	if (zui_router_switch(app->router, screen_id) == 0) {
		app->current_screen = screen_id;
		nodes_request_redraw(app);
	}
}

static void nodes_exit(struct nodes_app *app)
{
	if (app == NULL || app->exit_requested) {
		return;
	}

	app->exit_requested = true;
	(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	k_sem_give(&app->exit_sem);
	nodes_request_redraw(app);
}

static void nodes_toast(struct nodes_app *app, const char *text)
{
	if (app == NULL || app->host == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_NODES_TITLE,
		.text = text,
		.icon = &I_error_24x24,
		.timeout_ms = 1200U,
	});
}

static const char *nodes_role_text(mbs_contact_role role)
{
	return role == MBS_CONTACT_ROLE_REPEATER ? DESKTOP_TEXT_NODES_ROLE_REPEATER :
						    DESKTOP_TEXT_NODES_ROLE_CLIENT;
}

static const struct zui_icon *nodes_role_icon(mbs_contact_role role)
{
	switch (role) {
	case MBS_CONTACT_ROLE_REPEATER:
		return &I_repeater_10px;
	case MBS_CONTACT_ROLE_ROOM:
		return &I_room_10px;
	case MBS_CONTACT_ROLE_SENSOR:
		return &I_sensor_10px;
	case MBS_CONTACT_ROLE_CHAT:
	default:
		return &I_client_10px;
	}
}

static void nodes_format_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (bytes == NULL) {
		return;
	}

	for (size_t i = 0U; i < len && (off + 2U) < out_size; i++) {
		out[off++] = hex[(bytes[i] >> 4) & 0x0F];
		out[off++] = hex[bytes[i] & 0x0F];
	}
	out[off] = '\0';
}

static void nodes_format_out_path_bytes(const uint8_t *path, size_t path_len,
					uint8_t path_hash_size, char *out, size_t out_size)
{
	size_t hash_size;
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (path == NULL || path_len == 0U) {
		return;
	}

	hash_size = (size_t)path_hash_size;
	if (hash_size == 0U || hash_size > MBS_CONTACT_PATH_HASH_SIZE_MAX) {
		hash_size = 1U;
	}
	for (size_t i = 0U; i < path_len && (off + 3U) < out_size; i++) {
		if (i > 0U && (i % hash_size) == 0U) {
			out[off++] = '-';
		}
		off += snprintk(&out[off], out_size - off, "%02X", path[i]);
	}
	out[off] = '\0';
}

static void nodes_format_path_bytes(const uint8_t *path, size_t path_len, char *out,
				    size_t out_size)
{
	size_t off = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	out[0] = '\0';
	if (path == NULL || path_len == 0U) {
		(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_NODES_NEIGHBOR);
		return;
	}

	for (size_t i = 0U; i < path_len && (off + 3U) < out_size; i++) {
		if (i > 0U) {
			out[off++] = '-';
		}
		off += snprintk(&out[off], out_size - off, "%02X", path[i]);
	}
	out[off] = '\0';
}

static bool nodes_prefix_equal(const uint8_t *lhs, const uint8_t *rhs)
{
	return lhs != NULL && rhs != NULL && memcmp(lhs, rhs, NODES_PUBLIC_KEY_PREFIX_SIZE) == 0;
}

static bool nodes_row_prefix_equal(const struct nodes_contact_row *row, const uint8_t *prefix)
{
	return row != NULL && nodes_prefix_equal(row->prefix, prefix);
}

static uint8_t nodes_battery_pct_from_mv(uint16_t mv)
{
	if (mv <= 3300U) {
		return 0U;
	}
	if (mv >= 4200U) {
		return 100U;
	}
	return (uint8_t)(((uint32_t)(mv - 3300U) * 100U) / 900U);
}

static int32_t nodes_parse_s24_be(const uint8_t *buf)
{
	int32_t value = ((int32_t)buf[0] << 16) | ((int32_t)buf[1] << 8) | (int32_t)buf[2];

	if ((value & 0x00800000) != 0) {
		value |= (int32_t)0xFF000000;
	}
	return value;
}

static void nodes_parse_telemetry_lpp(const uint8_t *payload, size_t payload_len,
				      struct nodes_telemetry_parse_result *out)
{
	size_t off = 0U;

	if (out == NULL) {
		return;
	}

	memset(out, 0, sizeof(*out));
	while (payload != NULL && (off + 2U) <= payload_len) {
		uint8_t type = payload[off + 1U];
		size_t value_size = 0U;
		const uint8_t *value;

		off += 2U;
		if (type == 116U) {
			value_size = 2U;
		} else if (type == 136U) {
			value_size = 9U;
		}
		if (value_size == 0U || (off + value_size) > payload_len) {
			break;
		}

		value = &payload[off];
		if (type == 116U) {
			uint16_t raw = ((uint16_t)value[0] << 8) | (uint16_t)value[1];

			out->has_battery_pct = true;
			out->battery_pct = nodes_battery_pct_from_mv((uint16_t)(raw * 10U));
		} else if (type == 136U) {
			out->has_latitude_e7 = true;
			out->has_longitude_e7 = true;
			out->latitude_e7 = nodes_parse_s24_be(value) * 1000;
			out->longitude_e7 = nodes_parse_s24_be(value + 3U) * 1000;
		}
		off += value_size;
	}
}

static void nodes_format_coord(char *buf, size_t buf_size, int32_t value_e7)
{
	int32_t abs_value;
	int32_t deg;
	uint32_t frac;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	abs_value = value_e7 < 0 ? -value_e7 : value_e7;
	deg = abs_value / 10000000;
	frac = (uint32_t)(abs_value % 10000000);
	(void)snprintk(buf, buf_size, "%s%d.%05u", value_e7 < 0 ? "-" : "", (int)deg,
		       (unsigned int)(frac / 100U));
}

static const struct nodes_contact_row *nodes_selected_row(const struct nodes_app *app)
{
	if (app == NULL || app->contact_count == 0U || app->selected_index >= app->contact_count ||
	    !app->selected_row_valid) {
		return NULL;
	}

	return &app->selected_row;
}

static struct nodes_contact_row *nodes_selected_row_mut(struct nodes_app *app)
{
	if (app == NULL || app->contact_count == 0U || app->selected_index >= app->contact_count ||
	    !app->selected_row_valid) {
		return NULL;
	}

	return &app->selected_row;
}

static bool nodes_selected_prefix_equal(const struct nodes_app *app, const uint8_t *prefix)
{
	return nodes_row_prefix_equal(nodes_selected_row(app), prefix);
}

static void nodes_prepare_contact_row(struct nodes_contact_row *row, size_t row_index, uint8_t slot,
				   const mbs_contact *contact);

static void nodes_row_cache_invalidate(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(app->row_cache); i++) {
		app->row_cache[i].valid = false;
	}
	app->row_cache_next = 0U;
}

static struct nodes_contact_row_cache_entry *nodes_row_cache_find(struct nodes_app *app,
							      size_t index,
							      uint8_t slot)
{
	if (app == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(app->row_cache); i++) {
		struct nodes_contact_row_cache_entry *entry = &app->row_cache[i];

		if (entry->valid && entry->index == index && entry->slot == slot) {
			return entry;
		}
	}

	return NULL;
}

static struct nodes_contact_row_cache_entry *nodes_row_cache_alloc(struct nodes_app *app)
{
	struct nodes_contact_row_cache_entry *entry;

	if (app == NULL) {
		return NULL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(app->row_cache); i++) {
		if (!app->row_cache[i].valid) {
			return &app->row_cache[i];
		}
	}

	entry = &app->row_cache[app->row_cache_next % ARRAY_SIZE(app->row_cache)];
	app->row_cache_next = (uint8_t)((app->row_cache_next + 1U) % ARRAY_SIZE(app->row_cache));
	return entry;
}

static const struct nodes_contact_row *nodes_cached_row_at(struct nodes_app *app, size_t index)
{
	mbs_contact contact = meshbus_Contact_init_zero;
	struct nodes_contact_row_cache_entry *entry;
	uint8_t slot;

	if (app == NULL || index >= app->contact_count ||
	    index >= ARRAY_SIZE(app->contact_slots)) {
		return NULL;
	}

	slot = app->contact_slots[index];
	entry = nodes_row_cache_find(app, index, slot);
	if (entry != NULL) {
		return &entry->row;
	}

	entry = nodes_row_cache_alloc(app);
	if (entry == NULL || mbs_contact_get(slot, &contact) != 0) {
		return NULL;
	}

	nodes_prepare_contact_row(&entry->row, index, slot, &contact);
	entry->index = index;
	entry->slot = slot;
	entry->valid = true;
	return &entry->row;
}

static bool nodes_select_index(struct nodes_app *app, size_t index)
{
	const struct nodes_contact_row *row;

	if (app == NULL || index >= app->contact_count) {
		return false;
	}
	row = nodes_cached_row_at(app, index);
	if (row == NULL) {
		app->selected_row_valid = false;
		return false;
	}

	app->selected_row = *row;
	app->selected_index = index;
	app->selected_row_valid = true;
	return true;
}

static bool nodes_select_prefix(struct nodes_app *app, const uint8_t *prefix)
{
	if (app == NULL || prefix == NULL) {
		return false;
	}

	for (size_t i = 0U; i < app->contact_count; i++) {
		const struct nodes_contact_row *row = nodes_cached_row_at(app, i);

		if (row != NULL && nodes_row_prefix_equal(row, prefix)) {
			app->selected_row = *row;
			app->selected_index = i;
			app->selected_row_valid = true;
			return true;
		}
	}

	return false;
}

static void nodes_clear_response_state(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	app->discover_response_valid = false;
	app->trace_response_valid = false;
	app->telemetry_response_valid = false;
	app->telemetry_has_battery_pct = false;
	app->telemetry_has_position = false;
	app->discover_response_path[0] = '\0';
}

static void nodes_clear_selected_detail_cache(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	app->selected_public_key[0] = '\0';
	app->selected_out_path[0] = '\0';
}

static void nodes_update_selected_detail_cache(struct nodes_app *app)
{
	const struct nodes_contact_row *row;

	if (app == NULL) {
		return;
	}

	row = nodes_selected_row(app);
	nodes_clear_selected_detail_cache(app);
	if (row == NULL) {
		return;
	}

	nodes_format_hex(row->public_key, row->public_key_len, app->selected_public_key,
			 sizeof(app->selected_public_key));
	nodes_format_out_path_bytes(row->out_path, row->out_path_len, row->path_hash_size,
				    app->selected_out_path, sizeof(app->selected_out_path));
}

static void nodes_reset_selection_state(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	app->selected_index = 0U;
	app->selected_row_valid = false;
	app->menu_page = NODES_MENU_PAGE_ROOT;
	app->menu_root_selected = 0U;
	app->editing_flags = 0U;
	app->discover_req_pending = false;
	app->trace_req_pending = false;
	app->telemetry_req_pending = false;
	memset(app->discover_req_prefix, 0, sizeof(app->discover_req_prefix));
	memset(app->trace_req_prefix, 0, sizeof(app->trace_req_prefix));
	memset(app->telemetry_req_prefix, 0, sizeof(app->telemetry_req_prefix));
	app->alias_buf[0] = '\0';
	nodes_clear_selected_detail_cache(app);
	nodes_clear_response_state(app);
}

static const char *nodes_trace_status_for_row(const struct nodes_app *app,
					      const struct nodes_contact_row *row)
{
	if (app == NULL) {
		return DESKTOP_TEXT_NODES_STATUS_TRACE_NONE;
	}
	if (app->trace_req_pending) {
		return DESKTOP_TEXT_NODES_STATUS_TRACE_WAITING;
	}
	if (row != NULL && app->trace_response_valid &&
	    nodes_row_prefix_equal(row, app->trace_response_prefix) &&
	    app->trace_response_state != 0U) {
		return DESKTOP_TEXT_NODES_STATUS_TRACE_SUCCESS;
	}
	return DESKTOP_TEXT_NODES_STATUS_TRACE_NONE;
}

static size_t nodes_list_count(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL || app->contact_count == 0U) {
		return 1U;
	}

	return app->contact_count;
}

static int nodes_list_item(size_t index, struct zui_list_item *item, void *user_data)
{
	struct nodes_app *app = user_data;
	const struct nodes_contact_row *row;

	if (app == NULL || item == NULL) {
		return -EINVAL;
	}

	if (app->contact_count == 0U) {
		*item = app->empty_item;
		return 0;
	}
	if (index >= app->contact_count) {
		return -ENOENT;
	}
	row = nodes_cached_row_at(app, index);
	if (row == NULL) {
		return -ENOENT;
	}

	*item = row->item;
	return 0;
}

static void nodes_prepare_contact_row(struct nodes_contact_row *row, size_t row_index, uint8_t slot,
				   const mbs_contact *contact)
{
	uint8_t prefix[NODES_PUBLIC_KEY_PREFIX_SIZE] = {0};
	size_t copy_len;

	if (row == NULL || contact == NULL) {
		return;
	}

	memset(row, 0, sizeof(*row));
	copy_len = MIN((size_t)contact->public_key.size, (size_t)NODES_PUBLIC_KEY_PREFIX_SIZE);
	if (copy_len > 0U) {
		memcpy(prefix, contact->public_key.bytes, copy_len);
		memcpy(row->prefix, prefix, sizeof(row->prefix));
		nodes_format_hex(prefix, sizeof(prefix), row->id_short, sizeof(row->id_short));
	} else {
		(void)snprintk(row->id_short, sizeof(row->id_short), "%s",
			       DESKTOP_TEXT_NODES_DASH);
	}

	(void)snprintk(row->name, sizeof(row->name), "%s",
		       contact->name[0] != '\0' ? contact->name : DESKTOP_TEXT_NODES_DASH);
	(void)snprintk(row->alias, sizeof(row->alias), "%s", contact->alias);

	if (row->alias[0] != '\0') {
		size_t alias_len = MIN(strlen(row->alias), sizeof(row->label) - 1U);

		memcpy(row->label, row->alias, alias_len);
		row->label[alias_len] = '\0';
	} else if (contact->name[0] != '\0') {
		(void)snprintk(row->label, sizeof(row->label), "%s", contact->name);
	} else if (copy_len > 0U) {
		nodes_format_hex(prefix, sizeof(prefix), row->label, sizeof(row->label));
	} else {
		(void)snprintk(row->label, sizeof(row->label), "%s", DESKTOP_TEXT_NODES_DASH);
	}
	(void)snprintk(row->detail, sizeof(row->detail), "%s", nodes_role_text(contact->role));
	copy_len = MIN((size_t)contact->public_key.size, (size_t)MBS_CONTACT_PUBLIC_KEY_SIZE);
	if (copy_len > 0U) {
		memcpy(row->public_key, contact->public_key.bytes, copy_len);
		row->public_key_len = (uint8_t)copy_len;
	}
	copy_len = MIN((size_t)contact->out_path.size, (size_t)MBS_CONTACT_OUTPATH_MAX_LEN);
	if (copy_len > 0U) {
		memcpy(row->out_path, contact->out_path.bytes, copy_len);
		row->out_path_len = (uint8_t)copy_len;
		row->path_hash_size = contact->path_hash_size;
	}
	row->slot = slot;
	row->role = contact->role;
	row->is_neighbor = contact->is_neighbor;
	row->has_position = contact->latitude != 0 || contact->longitude != 0;
	row->latitude_e7 = contact->latitude * 10;
	row->longitude_e7 = contact->longitude * 10;
	row->item = (struct zui_list_item){
		.id = (uint32_t)NODES_LIST_ITEM_BASE + (uint32_t)row_index,
		.label = row->label,
		.detail = row->detail,
		.icon = nodes_role_icon(contact->role),
	};
	row->flags = contact->flags;
}

static void nodes_load_contact_snapshot(struct nodes_app *app)
{
	uint8_t store_count;
	uint8_t store_size;
	size_t found = 0U;
	int64_t start_ms;

	if (app == NULL) {
		return;
	}

	start_ms = k_uptime_get();
	store_count = mbs_contact_store_count();
	store_size = mbs_contact_store_size();
	nodes_row_cache_invalidate(app);

	for (uint8_t slot = 0U; slot < store_size && found < store_count &&
	     found < ARRAY_SIZE(app->contact_slots); slot++) {
		mbs_contact contact = meshbus_Contact_init_zero;

		if (mbs_contact_get(slot, &contact) != 0) {
			continue;
		}
		app->contact_slots[found] = slot;
		found++;
	}

	app->selected_index = 0U;
	app->contact_count = found;
	if (app->contact_count == 0U) {
		nodes_reset_selection_state(app);
	} else if (!nodes_select_index(app, 0U)) {
		nodes_reset_selection_state(app);
	}
	if (app->list != NULL) {
		(void)zui_sublist_reload(app->list);
	}
	LOG_DBG("nodes app: contact snapshot count=%u store_count=%u store_size=%u elapsed=%lldms",
		(unsigned int)found, (unsigned int)store_count, (unsigned int)store_size,
		(long long)(k_uptime_get() - start_ms));
	nodes_request_redraw(app);
}

static void nodes_snapshot_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct nodes_app *app = CONTAINER_OF(dwork, struct nodes_app, snapshot_work);

	nodes_load_contact_snapshot(app);
}

static void nodes_request_timeout_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct nodes_app *app = CONTAINER_OF(dwork, struct nodes_app, request_timeout_work);

	app->discover_req_pending = false;
	app->trace_req_pending = false;
	app->telemetry_req_pending = false;
	nodes_refresh_current_view(app);
}

static void nodes_schedule_request_timeout(struct nodes_app *app)
{
	if (app != NULL) {
		(void)k_work_reschedule(&app->request_timeout_work, K_MSEC(NODES_REQUEST_TIMEOUT_MS));
	}
}

static void nodes_refresh_current_view(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->current_screen == NODES_SCREEN_MENU) {
		nodes_update_menu(app);
	}
	if (app->current_screen != NODES_SCREEN_DETAIL) {
		nodes_request_redraw(app);
		return;
	}

	switch (app->detail_kind) {
	case NODES_DETAIL_TELEMETRY:
		nodes_open_telemetry(app);
		break;
	case NODES_DETAIL_DISCOVER_PATH:
		nodes_open_discover_path(app);
		break;
	case NODES_DETAIL_TRACE_PATH:
		nodes_open_trace_path(app);
		break;
	case NODES_DETAIL_PUBLIC_KEY:
		nodes_request_redraw(app);
		break;
	case NODES_DETAIL_NONE:
	default:
		nodes_request_redraw(app);
		break;
	}
}

static void nodes_apply_snapshot_event(struct nodes_app *app)
{
	const struct nodes_contact_row *selected = nodes_selected_row(app);
	uint8_t selected_prefix[NODES_PUBLIC_KEY_PREFIX_SIZE] = {0};
	bool has_selected = false;
	bool selected_preserved = false;

	if (app == NULL) {
		return;
	}

	if (selected != NULL) {
		memcpy(selected_prefix, selected->prefix, sizeof(selected_prefix));
		has_selected = true;
	}

	nodes_load_contact_snapshot(app);
	if (has_selected) {
		selected_preserved = nodes_select_prefix(app, selected_prefix);
		if (!selected_preserved) {
			nodes_clear_response_state(app);
		}
	}
	if (nodes_selected_row(app) != NULL) {
		(void)snprintk(app->alias_buf, sizeof(app->alias_buf), "%s",
			       nodes_selected_row(app)->alias);
		app->editing_flags = nodes_selected_row(app)->flags;
		nodes_update_selected_detail_cache(app);
		if (app->list != NULL) {
			(void)zui_sublist_select(app->list, app->selected_index);
		}
	} else {
		nodes_reset_selection_state(app);
	}
	nodes_refresh_current_view(app);
}

static void nodes_apply_discover_event(struct nodes_app *app,
					const struct nodes_discover_event *event)
{
	if (app == NULL || event == NULL) {
		return;
	}

	if (nodes_prefix_equal(app->discover_req_prefix, event->prefix)) {
		app->discover_req_pending = false;
	}
	if (!nodes_selected_prefix_equal(app, event->prefix)) {
		nodes_refresh_current_view(app);
		return;
	}

	memcpy(app->discover_response_prefix, event->prefix, sizeof(app->discover_response_prefix));
	nodes_format_path_bytes(event->path, event->path_len, app->discover_response_path,
				sizeof(app->discover_response_path));
	app->discover_response_valid = true;
	nodes_refresh_current_view(app);
}

static void nodes_apply_trace_event(struct nodes_app *app, const struct nodes_trace_event *event)
{
	if (app == NULL || event == NULL) {
		return;
	}

	if (nodes_prefix_equal(app->trace_req_prefix, event->prefix)) {
		app->trace_req_pending = false;
	}
	if (!nodes_selected_prefix_equal(app, event->prefix)) {
		nodes_refresh_current_view(app);
		return;
	}

	memcpy(app->trace_response_prefix, event->prefix, sizeof(app->trace_response_prefix));
	app->trace_response_state = event->state;
	app->trace_response_valid = true;
	nodes_refresh_current_view(app);
}

static void nodes_apply_telemetry_event(struct nodes_app *app,
					 const struct nodes_telemetry_event *event)
{
	struct nodes_telemetry_parse_result parsed;

	if (app == NULL || event == NULL) {
		return;
	}

	if (nodes_prefix_equal(app->telemetry_req_prefix, event->prefix)) {
		app->telemetry_req_pending = false;
	}
	if (!nodes_selected_prefix_equal(app, event->prefix)) {
		nodes_refresh_current_view(app);
		return;
	}

	nodes_parse_telemetry_lpp(event->payload, event->payload_len, &parsed);
	memcpy(app->telemetry_response_prefix, event->prefix, sizeof(app->telemetry_response_prefix));
	app->telemetry_response_valid = true;
	app->telemetry_has_battery_pct = parsed.has_battery_pct;
	app->telemetry_battery_pct = parsed.battery_pct;
	app->telemetry_has_position = parsed.has_latitude_e7 && parsed.has_longitude_e7;
	app->telemetry_latitude_e7 = parsed.latitude_e7;
	app->telemetry_longitude_e7 = parsed.longitude_e7;
	nodes_refresh_current_view(app);
}

static void nodes_event_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct nodes_app *app = CONTAINER_OF(dwork, struct nodes_app, event_work);
	struct nodes_discover_event discover;
	struct nodes_trace_event trace;
	struct nodes_telemetry_event telemetry;
	uint32_t flags;
	k_spinlock_key_t key;

	key = k_spin_lock(&app->event_lock);
	flags = app->event_flags;
	app->event_flags = 0U;
	if ((flags & NODES_EVENT_FLAG_DISCOVER) != 0U) {
		discover = app->discover_event;
	}
	if ((flags & NODES_EVENT_FLAG_TRACE) != 0U) {
		trace = app->trace_event;
	}
	if ((flags & NODES_EVENT_FLAG_TELEMETRY) != 0U) {
		telemetry = app->telemetry_event;
	}
	k_spin_unlock(&app->event_lock, key);

	if ((flags & NODES_EVENT_FLAG_SNAPSHOT) != 0U) {
		nodes_apply_snapshot_event(app);
	}
	if ((flags & NODES_EVENT_FLAG_DISCOVER) != 0U) {
		nodes_apply_discover_event(app, &discover);
	}
	if ((flags & NODES_EVENT_FLAG_TRACE) != 0U) {
		nodes_apply_trace_event(app, &trace);
	}
	if ((flags & NODES_EVENT_FLAG_TELEMETRY) != 0U) {
		nodes_apply_telemetry_event(app, &telemetry);
	}
}

static void nodes_menu_set_value(struct nodes_app *app, size_t index, const char *text)
{
	if (app == NULL || index >= ARRAY_SIZE(app->menu_values)) {
		return;
	}

	(void)snprintk(app->menu_values[index], sizeof(app->menu_values[index]), "%s",
		       text != NULL ? text : DESKTOP_TEXT_COMMON_EMPTY);
}

static void nodes_menu_add(struct nodes_app *app, uint32_t id, const char *label,
			   const char *value)
{
	size_t index;

	if (app == NULL || app->menu_item_count >= ARRAY_SIZE(app->menu_items)) {
		return;
	}

	index = app->menu_item_count++;
	nodes_menu_set_value(app, index, value);
	app->menu_items[index] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = app->menu_values[index],
	};
}

static void nodes_menu_add_route(struct nodes_app *app, uint32_t id, const char *label)
{
	size_t index;

	if (app == NULL || app->menu_item_count >= ARRAY_SIZE(app->menu_items)) {
		return;
	}

	index = app->menu_item_count++;
	nodes_menu_set_value(app, index, DESKTOP_TEXT_COMMON_ROUTE);
	app->menu_items[index] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = app->menu_values[index],
		.value_align = ZUI_FORM_VALUE_ALIGN_RIGHT,
	};
}

static void nodes_menu_add_options(struct nodes_app *app, uint32_t id, const char *label,
				   const char *const *options, size_t option_count,
				   size_t option_index)
{
	size_t index;

	if (app == NULL || app->menu_item_count >= ARRAY_SIZE(app->menu_items)) {
		return;
	}

	index = app->menu_item_count++;
	app->menu_items[index] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = option_count,
		.option_index = option_index,
	};
}

static void nodes_update_form(struct nodes_app *app, const char *title)
{
	if (app == NULL || app->menu == NULL) {
		return;
	}

	(void)zui_form_update(app->menu, &(struct zui_form_config){
		.title = title != NULL ? title : DESKTOP_TEXT_NODES_NODE,
		.items = app->menu_items,
		.item_count = app->menu_item_count,
		.activated = nodes_menu_activated,
		.changed = nodes_menu_changed,
		.user_data = app,
	});
}

static void nodes_update_permission_menu(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	app->menu_item_count = 0U;
	nodes_menu_add_options(app, NODES_PERMISSION_BASE, DESKTOP_TEXT_NODES_PERMISSION_BASE,
			       DESKTOP_TEXT_COMMON_BOOL_VALUES,
			       ARRAY_SIZE(DESKTOP_TEXT_COMMON_BOOL_VALUES),
			       nodes_flag_is_set(app->editing_flags,
						 MBS_CONTACT_FLAG_TELEMETRY_BASE) ?
						       1U :
						       0U);
	nodes_menu_add_options(app, NODES_PERMISSION_LOCATION,
			       DESKTOP_TEXT_NODES_PERMISSION_LOCATION,
			       DESKTOP_TEXT_COMMON_BOOL_VALUES,
			       ARRAY_SIZE(DESKTOP_TEXT_COMMON_BOOL_VALUES),
			       nodes_flag_is_set(app->editing_flags,
						 MBS_CONTACT_FLAG_TELEMETRY_LOCATION) ?
						       1U :
						       0U);
	nodes_menu_add_options(app, NODES_PERMISSION_ENVIRONMENT,
			       DESKTOP_TEXT_NODES_PERMISSION_ENVIRONMENT,
			       DESKTOP_TEXT_COMMON_BOOL_VALUES,
			       ARRAY_SIZE(DESKTOP_TEXT_COMMON_BOOL_VALUES),
			       nodes_flag_is_set(app->editing_flags,
						 MBS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT) ?
						       1U :
						       0U);
	nodes_update_form(app, DESKTOP_TEXT_NODES_PERMISSION_TITLE);
}

static void nodes_update_root_menu(struct nodes_app *app)
{
	const struct nodes_contact_row *row;
	const char *name = DESKTOP_TEXT_NODES_NODE;
	const char *alias = DESKTOP_TEXT_NODES_DASH;
	const char *id_short = DESKTOP_TEXT_NODES_DASH;
	const char *title = DESKTOP_TEXT_NODES_NODE;

	if (app == NULL) {
		return;
	}

	row = nodes_selected_row(app);
	if (row != NULL) {
		name = row->name;
		alias = app->alias_buf[0] != '\0' ? app->alias_buf : DESKTOP_TEXT_NODES_DASH;
		id_short = row->id_short;
		title = row->label[0] != '\0' ? row->label : row->id_short;
	}

	app->menu_item_count = 0U;
	if (row == NULL) {
		nodes_menu_add(app, 0U, DESKTOP_TEXT_NODES_NO_NODE, DESKTOP_TEXT_COMMON_EMPTY);
		goto update;
	}

	nodes_menu_add(app, NODES_MENU_ALIAS, DESKTOP_TEXT_NODES_MENU_ALIAS, alias);
	nodes_menu_add(app, NODES_MENU_NAME, DESKTOP_TEXT_NODES_MENU_NAME, name);
	nodes_menu_add(app, NODES_MENU_ROLE, DESKTOP_TEXT_NODES_MENU_ROLE,
		       nodes_role_text(row->role));
	nodes_menu_add(app, NODES_MENU_PUBLIC_KEY, DESKTOP_TEXT_NODES_MENU_PUBLIC_KEY, id_short);
	nodes_menu_add_route(app, NODES_MENU_PERMISSION, DESKTOP_TEXT_NODES_MENU_PERMISSION);
	if (row->role != MBS_CONTACT_ROLE_REPEATER) {
		nodes_menu_add_route(app, NODES_MENU_TELEMETRY,
				     DESKTOP_TEXT_NODES_MENU_TELEMETRY);
		if (app->selected_out_path[0] != '\0') {
			nodes_menu_add(app, NODES_MENU_DISCOVER,
				       DESKTOP_TEXT_NODES_MENU_DISCOVER_PATH,
				       app->selected_out_path);
		} else {
			nodes_menu_add_route(app, NODES_MENU_DISCOVER,
					     DESKTOP_TEXT_NODES_MENU_DISCOVER_PATH);
		}
		nodes_menu_add_route(app, NODES_MENU_TRACE, DESKTOP_TEXT_NODES_MENU_TRACE_PATH);
	}
	nodes_menu_add(app, NODES_MENU_BROADCAST, DESKTOP_TEXT_NODES_MENU_BROADCAST,
		       DESKTOP_TEXT_COMMON_EMPTY);
	nodes_menu_add(app, NODES_MENU_APPLY, DESKTOP_TEXT_COMMON_ACTION_APPLY,
		       DESKTOP_TEXT_COMMON_EMPTY);
	nodes_menu_add(app, NODES_MENU_DELETE, DESKTOP_TEXT_NODES_MENU_DELETE,
		       DESKTOP_TEXT_COMMON_EMPTY);

update:
	nodes_update_form(app, title);
}

static void nodes_update_menu(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	if (app->menu_page == NODES_MENU_PAGE_PERMISSION) {
		nodes_update_permission_menu(app);
	} else {
		nodes_update_root_menu(app);
	}
}

static void nodes_return_to_root_menu(struct nodes_app *app)
{
	if (app == NULL) {
		return;
	}

	app->menu_page = NODES_MENU_PAGE_ROOT;
	nodes_update_menu(app);
	if (app->menu != NULL && zui_form_count(app->menu) > 0U) {
		(void)zui_form_select(app->menu,
				      MIN(app->menu_root_selected,
					  zui_form_count(app->menu) - 1U));
	}
}

static void nodes_open_permission_menu(struct nodes_app *app)
{
	if (app == NULL || app->menu == NULL) {
		return;
	}

	app->menu_root_selected = zui_form_selected(app->menu);
	app->menu_page = NODES_MENU_PAGE_PERMISSION;
	nodes_update_menu(app);
	if (zui_form_count(app->menu) > 0U) {
		(void)zui_form_select(app->menu, 0U);
	}
	nodes_switch(app, NODES_SCREEN_MENU);
}

static void nodes_prepare_text_view(struct nodes_app *app, const char *title, const char *text)
{
	if (app == NULL || app->detail_view == NULL) {
		return;
	}

	if (text != app->detail_text) {
		(void)snprintk(app->detail_text, sizeof(app->detail_text), "%s",
			       text != NULL ? text : DESKTOP_TEXT_COMMON_EMPTY);
	}
	(void)zui_text_view_update(app->detail_view, &(struct zui_text_view_config){
		.title = title != NULL ? title : DESKTOP_TEXT_NODES_TITLE,
		.text = app->detail_text,
		.font = ZUI_FONT_PRIMARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	nodes_switch(app, NODES_SCREEN_DETAIL);
}

static void nodes_open_public_key(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);

	if (app == NULL) {
		return;
	}

	app->detail_kind = NODES_DETAIL_PUBLIC_KEY;
	nodes_prepare_text_view(app, DESKTOP_TEXT_NODES_PUBLIC_KEY_TITLE,
				row != NULL && app->selected_public_key[0] != '\0' ?
					app->selected_public_key :
					DESKTOP_TEXT_NODES_NO_NODE);
}

static void nodes_open_telemetry(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	bool has_battery = false;
	uint8_t battery_pct = 0U;
	bool has_position = false;
	int32_t latitude_e7 = 0;
	int32_t longitude_e7 = 0;
	char battery[16];
	char latitude[24];
	char longitude[24];

	if (app == NULL) {
		return;
	}

	app->detail_kind = NODES_DETAIL_TELEMETRY;
	if (row == NULL) {
		nodes_prepare_text_view(app, DESKTOP_TEXT_NODES_TELEMETRY_TITLE,
					DESKTOP_TEXT_NODES_TELEMETRY_NO_NODE);
		return;
	}

	if (app->telemetry_response_valid &&
	    nodes_row_prefix_equal(row, app->telemetry_response_prefix)) {
		has_battery = app->telemetry_has_battery_pct;
		battery_pct = app->telemetry_battery_pct;
		has_position = app->telemetry_has_position;
		latitude_e7 = app->telemetry_latitude_e7;
		longitude_e7 = app->telemetry_longitude_e7;
	} else if (row->has_position) {
		has_position = true;
		latitude_e7 = row->latitude_e7;
		longitude_e7 = row->longitude_e7;
	}

	if (has_battery || has_position) {
		if (has_battery) {
			(void)snprintk(battery, sizeof(battery), "%u%%", (unsigned int)battery_pct);
		} else {
			(void)snprintk(battery, sizeof(battery), "%s", DESKTOP_TEXT_NODES_DASH);
		}
		if (has_position) {
			nodes_format_coord(latitude, sizeof(latitude), latitude_e7);
			nodes_format_coord(longitude, sizeof(longitude), longitude_e7);
		} else {
			(void)snprintk(latitude, sizeof(latitude), "%s", DESKTOP_TEXT_NODES_DASH);
			(void)snprintk(longitude, sizeof(longitude), "%s", DESKTOP_TEXT_NODES_DASH);
		}
		(void)snprintk(app->detail_text, sizeof(app->detail_text), "%s: %s\n%s: %s\n%s: %s",
			       DESKTOP_TEXT_NODES_MENU_BATTERY, battery,
			       DESKTOP_TEXT_NODES_MENU_LATITUDE, latitude,
			       DESKTOP_TEXT_NODES_MENU_LONGITUDE, longitude);
	} else {
		(void)snprintk(app->detail_text, sizeof(app->detail_text), "%s",
			       DESKTOP_TEXT_NODES_TELEMETRY_NO_NODE);
	}
	nodes_prepare_text_view(app, DESKTOP_TEXT_NODES_TELEMETRY_TITLE, app->detail_text);
}

static void nodes_open_discover_path(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	const char *target;
	const char *path;

	if (app == NULL) {
		return;
	}

	app->detail_kind = NODES_DETAIL_DISCOVER_PATH;
	target = row != NULL ? row->label : DESKTOP_TEXT_NODES_NODE;
	path = row != NULL && app->discover_response_valid &&
		       nodes_row_prefix_equal(row, app->discover_response_prefix) ?
	       app->discover_response_path :
	       row != NULL && app->selected_out_path[0] != '\0' ? app->selected_out_path :
	       (row != NULL && row->is_neighbor ? DESKTOP_TEXT_NODES_NEIGHBOR : DESKTOP_TEXT_NODES_DASH);
	(void)snprintk(app->detail_text, sizeof(app->detail_text),
		       DESKTOP_TEXT_NODES_DISCOVER_READY_TEXT_FORMAT, target, path);
	nodes_prepare_text_view(app, DESKTOP_TEXT_NODES_TITLE_DISCOVER_PATH, app->detail_text);
}

static void nodes_open_trace_path(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	const char *target;
	const char *path;

	if (app == NULL) {
		return;
	}

	app->detail_kind = NODES_DETAIL_TRACE_PATH;
	target = row != NULL ? row->label : DESKTOP_TEXT_NODES_NODE;
	path = row != NULL && app->selected_out_path[0] != '\0' ? app->selected_out_path :
	       (row != NULL && row->is_neighbor ? DESKTOP_TEXT_NODES_NEIGHBOR : DESKTOP_TEXT_NODES_DASH);
	(void)snprintk(app->detail_text, sizeof(app->detail_text),
		       DESKTOP_TEXT_NODES_TRACE_PATH_TEXT_FORMAT, target, path,
		       nodes_trace_status_for_row(app, row));
	nodes_prepare_text_view(app, DESKTOP_TEXT_NODES_TITLE_TRACE_PATH, app->detail_text);
}

static void nodes_open_delete_modal(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);

	if (app == NULL || app->modal == NULL) {
		return;
	}

	(void)snprintk(app->modal_text, sizeof(app->modal_text),
		       DESKTOP_TEXT_NODES_ACTION_CONFIRM_DELETE_TEXT_FORMAT,
		       row != NULL ? row->label : DESKTOP_TEXT_NODES_NODE);
	app->modal_kind = NODES_MODAL_DELETE;
	(void)zui_modal_update(app->modal, &(struct zui_modal_config){
		.title = DESKTOP_TEXT_NODES_ACTION_CONFIRM_DELETE_HEADER,
		.text = app->modal_text,
		.left_button = DESKTOP_TEXT_COMMON_CANCEL,
		.right_button = DESKTOP_TEXT_COMMON_OK,
		.result = nodes_modal_result,
		.user_data = app,
	});
	nodes_switch(app, NODES_SCREEN_MODAL);
}

static int nodes_apply_contact_changes(struct nodes_app *app)
{
	struct nodes_contact_row *row = nodes_selected_row_mut(app);
	mbs_contact contact = meshbus_Contact_init_zero;
	int rc;

	if (app == NULL || row == NULL) {
		return -ENOENT;
	}

	rc = mbs_contact_find_by_prefix(row->prefix, &contact);
	LOG_DBG("nodes app: contact_find_by_prefix slot=%u -> %d", (unsigned int)row->slot, rc);
	if (rc != 0) {
		return rc;
	}

	(void)snprintk(contact.alias, sizeof(contact.alias), "%s", app->alias_buf);
	contact.alias[sizeof(contact.alias) - 1U] = '\0';
	contact.flags = app->editing_flags;

	rc = mbs_contact_set(contact.public_key.bytes, &contact);
	LOG_DBG("nodes app: contact_set slot=%u flags=0x%08x -> %d", (unsigned int)row->slot,
		(unsigned int)contact.flags, rc);
	if (rc != 0) {
		return rc;
	}

	rc = mbs_contact_find_by_prefix(row->prefix, &contact);
	if (rc != 0) {
		return rc;
	}

	nodes_prepare_contact_row(&app->selected_row, app->selected_index, row->slot, &contact);
	app->selected_row_valid = true;
	(void)snprintk(app->alias_buf, sizeof(app->alias_buf), "%s",
		       app->selected_row.alias);
	app->editing_flags = app->selected_row.flags;
	nodes_update_selected_detail_cache(app);
	nodes_update_menu(app);
	nodes_request_redraw(app);
	return 0;
}

static int nodes_delete_selected_contact(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	int rc;

	if (app == NULL || row == NULL) {
		return -ENOENT;
	}

	rc = mbs_contact_reset(row->prefix);
	LOG_DBG("nodes app: contact_reset slot=%u -> %d", (unsigned int)row->slot, rc);
	if (rc != 0) {
		return rc;
	}

	nodes_load_contact_snapshot(app);
	if (app->contact_count > 0U) {
		(void)snprintk(app->alias_buf, sizeof(app->alias_buf), "%s",
			       app->selected_row.alias);
		app->editing_flags = app->selected_row.flags;
		nodes_update_selected_detail_cache(app);
	} else {
		app->alias_buf[0] = '\0';
		app->editing_flags = 0U;
		nodes_clear_selected_detail_cache(app);
	}
	nodes_update_menu(app);
	return 0;
}

static void nodes_request_failed(struct nodes_app *app, int rc)
{
	nodes_toast(app, rc == -EBUSY ? DESKTOP_TEXT_NODES_REQUEST_ERROR_BUSY_RETRY_LATER :
					DESKTOP_TEXT_NODES_REQUEST_ERROR_FAILURE);
}

static void nodes_request_sent(struct nodes_app *app, const char *text)
{
	if (app == NULL || app->host == NULL) {
		return;
	}

	(void)zui_toast_show(app->host, &(struct zui_toast_config){
		.title = DESKTOP_TEXT_NODES_SENT,
		.text = text,
		.icon = &I_done_24x24,
		.timeout_ms = 1200U,
	});
}

static void nodes_send_discover_request(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	int rc;

	if (app == NULL || row == NULL || app->discover_req_pending) {
		return;
	}

	rc = mbs_contact_discover_path_request(row->prefix, NULL);
	LOG_DBG("nodes app: discover_path_request prefix=%s -> %d", row->id_short, rc);
	if (rc != 0) {
		nodes_request_failed(app, rc);
		nodes_open_discover_path(app);
		return;
	}

	memcpy(app->discover_req_prefix, row->prefix, sizeof(app->discover_req_prefix));
	app->discover_response_valid = false;
	app->discover_req_pending = true;
	nodes_schedule_request_timeout(app);
	nodes_request_sent(app, DESKTOP_TEXT_NODES_DISCOVER_PATH_REQUEST_SENT);
	nodes_open_discover_path(app);
}

static void nodes_send_trace_request(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	int rc;

	if (app == NULL || row == NULL || app->trace_req_pending) {
		return;
	}
	if (row->is_neighbor) {
		nodes_toast(app, DESKTOP_TEXT_NODES_REQUEST_ERROR_NEIGHBOR_NOT_ALLOW);
		return;
	}

	rc = mbs_contact_trace_path_request(row->prefix, NULL);
	LOG_DBG("nodes app: trace_path_request prefix=%s -> %d", row->id_short, rc);
	if (rc != 0) {
		nodes_request_failed(app, rc);
		nodes_open_trace_path(app);
		return;
	}

	memcpy(app->trace_req_prefix, row->prefix, sizeof(app->trace_req_prefix));
	app->trace_response_valid = false;
	app->trace_req_pending = true;
	nodes_schedule_request_timeout(app);
	nodes_request_sent(app, DESKTOP_TEXT_NODES_TRACE_PATH_REQUEST_SENT);
	nodes_open_trace_path(app);
}

static void nodes_send_telemetry_request(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	int rc;

	if (app == NULL || row == NULL || app->telemetry_req_pending) {
		return;
	}

	rc = mbs_contact_telemetry_request(row->prefix, NULL);
	LOG_DBG("nodes app: telemetry_request prefix=%s -> %d", row->id_short, rc);
	if (rc != 0) {
		nodes_request_failed(app, rc);
		nodes_open_telemetry(app);
		return;
	}

	memcpy(app->telemetry_req_prefix, row->prefix, sizeof(app->telemetry_req_prefix));
	app->telemetry_response_valid = false;
	app->telemetry_has_battery_pct = false;
	app->telemetry_has_position = false;
	app->telemetry_req_pending = true;
	nodes_schedule_request_timeout(app);
	nodes_request_sent(app, DESKTOP_TEXT_NODES_REQUEST_SENT);
	nodes_open_telemetry(app);
}

static void nodes_send_contact_advert_request(struct nodes_app *app)
{
	const struct nodes_contact_row *row = nodes_selected_row(app);
	int rc;

	if (app == NULL || row == NULL) {
		return;
	}

	rc = mbs_contact_share_request(row->prefix);
	LOG_DBG("nodes app: contact_advert_request prefix=%s -> %d", row->id_short, rc);
	if (rc != 0) {
		nodes_request_failed(app, rc);
		return;
	}

	nodes_request_sent(app, DESKTOP_TEXT_NODES_BROADCAST_REQUEST_SENT);
}

static void nodes_list_selected(struct zui_sublist *list, uint32_t id, size_t index,
				const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	ARG_UNUSED(list);

	if (app == NULL || !nodes_is_click(event) || id == NODES_LIST_ITEM_EMPTY ||
	    index >= app->contact_count) {
		return;
	}

	if (!nodes_select_index(app, index)) {
		return;
	}
	app->menu_page = NODES_MENU_PAGE_ROOT;
	app->menu_root_selected = 0U;
	nodes_clear_response_state(app);
	(void)snprintk(app->alias_buf, sizeof(app->alias_buf), "%s",
		       app->selected_row.alias);
	app->editing_flags = app->selected_row.flags;
	nodes_update_selected_detail_cache(app);
	nodes_update_menu(app);
	nodes_switch(app, NODES_SCREEN_MENU);
}

static void nodes_menu_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;
	int rc;

	ARG_UNUSED(form);

	if (app == NULL || !nodes_is_click(event)) {
		return;
	}

	switch (id) {
	case NODES_MENU_ALIAS:
		(void)zui_text_editor_update(app->alias_editor, &(struct zui_text_editor_config){
			.title = DESKTOP_TEXT_NODES_MENU_ALIAS,
			.buffer = app->alias_buf,
			.buffer_size = sizeof(app->alias_buf),
			.clear_default_text = false,
			.submitted = nodes_alias_submitted,
			.user_data = app,
		});
		nodes_switch(app, NODES_SCREEN_ALIAS);
		break;
	case NODES_MENU_PUBLIC_KEY:
		nodes_open_public_key(app);
		break;
	case NODES_MENU_PERMISSION:
		nodes_open_permission_menu(app);
		break;
	case NODES_MENU_TELEMETRY:
		nodes_open_telemetry(app);
		break;
	case NODES_MENU_DISCOVER:
		nodes_open_discover_path(app);
		break;
	case NODES_MENU_TRACE:
		nodes_open_trace_path(app);
		break;
	case NODES_MENU_BROADCAST:
		nodes_send_contact_advert_request(app);
		break;
	case NODES_MENU_APPLY:
		rc = nodes_apply_contact_changes(app);
		if (rc == 0) {
			(void)zui_toast_show(app->host, &(struct zui_toast_config){
				.title = DESKTOP_TEXT_COMMON_OK,
				.text = DESKTOP_TEXT_COMMON_SETTINGS_APPLIED,
				.icon = &I_save_24x24,
				.timeout_ms = 900U,
			});
		} else {
			nodes_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID);
		}
		break;
	case NODES_MENU_DELETE:
		nodes_open_delete_modal(app);
		break;
	default:
		break;
	}
}

static void nodes_menu_changed(struct zui_form *form, uint32_t id, size_t option_index,
			       void *user_data)
{
	struct nodes_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	switch (id) {
	case NODES_PERMISSION_BASE:
		nodes_update_flag(&app->editing_flags, MBS_CONTACT_FLAG_TELEMETRY_BASE,
				  option_index);
		break;
	case NODES_PERMISSION_LOCATION:
		nodes_update_flag(&app->editing_flags,
				  MBS_CONTACT_FLAG_TELEMETRY_LOCATION, option_index);
		break;
	case NODES_PERMISSION_ENVIRONMENT:
		nodes_update_flag(&app->editing_flags,
				  MBS_CONTACT_FLAG_TELEMETRY_ENVIRONMENT, option_index);
		break;
	default:
		break;
	}
}

static void nodes_alias_submitted(struct zui_text_editor *editor, const char *text, void *user_data)
{
	struct nodes_app *app = user_data;

	ARG_UNUSED(editor);

	if (app == NULL) {
		return;
	}

	(void)snprintk(app->alias_buf, sizeof(app->alias_buf), "%s", text != NULL ? text : "");
	nodes_update_menu(app);
	nodes_switch(app, NODES_SCREEN_MENU);
}

static void nodes_modal_result(struct zui_modal *modal, enum zui_modal_result result,
			       const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;
	int rc;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	if (app == NULL) {
		return;
	}

	if (app->modal_kind == NODES_MODAL_DELETE) {
		app->modal_kind = NODES_MODAL_NONE;
		if (result == ZUI_MODAL_RESULT_RIGHT) {
			rc = nodes_delete_selected_contact(app);
			if (rc == 0) {
				(void)zui_toast_show(app->host, &(struct zui_toast_config){
					.title = DESKTOP_TEXT_NODES_DELETED,
					.text = DESKTOP_TEXT_NODES_NODE_DELETED,
					.icon = &I_done_24x24,
					.timeout_ms = 1200U,
				});
				nodes_switch(app, NODES_SCREEN_LIST);
			} else {
				nodes_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID);
				nodes_switch(app, NODES_SCREEN_MENU);
			}
			return;
		}
		nodes_switch(app, NODES_SCREEN_MENU);
		return;
	}

	app->modal_kind = NODES_MODAL_NONE;
	nodes_switch(app, NODES_SCREEN_MENU);
}

static bool nodes_forward_input(struct zui_screen *screen, const struct zui_input_event *event)
{
	return screen != NULL && event != NULL && zui_screen_submit_input(screen, event) > 0;
}

static void nodes_forward_enter(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_enter(screen);
	}
}

static void nodes_forward_exit(struct zui_screen *screen)
{
	if (screen != NULL) {
		(void)zui_screen_exit(screen);
	}
}

static void nodes_list_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_sublist_get_screen(app->list), draw);
	}
}

static bool nodes_list_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (nodes_should_consume_edge(event)) {
		return true;
	}
	if (nodes_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		nodes_exit(app);
		return true;
	}
	if (nodes_forward_input(zui_sublist_get_screen(app->list), event)) {
		nodes_request_redraw(app);
		return true;
	}

	return false;
}

static void nodes_list_enter(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_enter(zui_sublist_get_screen(app->list));
	}
}

static void nodes_list_exit(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_exit(zui_sublist_get_screen(app->list));
	}
}

static void nodes_menu_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_form_get_screen(app->menu), draw);
	}
}

static bool nodes_menu_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (nodes_should_consume_edge(event)) {
		return true;
	}
	if (nodes_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		if (app->menu_page == NODES_MENU_PAGE_PERMISSION) {
			nodes_return_to_root_menu(app);
			return true;
		}
		nodes_switch(app, NODES_SCREEN_LIST);
		return true;
	}
	if (nodes_forward_input(zui_form_get_screen(app->menu), event)) {
		nodes_request_redraw(app);
		return true;
	}

	return false;
}

static void nodes_menu_enter(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_update_menu(app);
		nodes_forward_enter(zui_form_get_screen(app->menu));
	}
}

static void nodes_menu_exit(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_exit(zui_form_get_screen(app->menu));
	}
}

static void nodes_alias_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_text_editor_get_screen(app->alias_editor), draw);
	}
}

static bool nodes_alias_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (nodes_should_consume_edge(event)) {
		return true;
	}
	if (event != NULL && event->action == ZUI_INPUT_ACTION_LONG_PRESS &&
	    event->code == ZUI_INPUT_CODE_BACK) {
		nodes_switch(app, NODES_SCREEN_MENU);
		return true;
	}
	if (nodes_forward_input(zui_text_editor_get_screen(app->alias_editor), event)) {
		nodes_request_redraw(app);
		return true;
	}

	return false;
}

static void nodes_alias_enter(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_enter(zui_text_editor_get_screen(app->alias_editor));
	}
}

static void nodes_alias_exit(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_exit(zui_text_editor_get_screen(app->alias_editor));
	}
}

static void nodes_detail_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return;
	}

	(void)zui_screen_draw(zui_text_view_get_screen(app->detail_view), draw);
	if (app->detail_kind == NODES_DETAIL_DISCOVER_PATH) {
		zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
			.center = app->discover_req_pending ? DESKTOP_TEXT_NODES_DISCOVERING :
							      DESKTOP_TEXT_NODES_BUTTON_REDISCOVER,
		});
	} else if (app->detail_kind == NODES_DETAIL_TRACE_PATH) {
		zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
			.center = app->trace_req_pending ? DESKTOP_TEXT_NODES_TRACING :
							   DESKTOP_TEXT_NODES_BUTTON_RETRACE,
		});
	} else if (app->detail_kind == NODES_DETAIL_TELEMETRY) {
		zui_draw_button_hints(draw, &(struct zui_draw_button_hint){
			.center = app->telemetry_req_pending ? DESKTOP_TEXT_NODES_REQUESTING :
							       DESKTOP_TEXT_COMMON_REFRESH,
		});
	}
}

static bool nodes_detail_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (nodes_should_consume_edge(event)) {
		return true;
	}
	if (nodes_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		nodes_switch(app, NODES_SCREEN_MENU);
		return true;
	}
	if (nodes_is_click(event) && event->code == ZUI_INPUT_CODE_SELECT) {
		if (app->detail_kind == NODES_DETAIL_DISCOVER_PATH) {
			if (!app->discover_req_pending) {
				nodes_send_discover_request(app);
			}
		} else if (app->detail_kind == NODES_DETAIL_TRACE_PATH) {
			nodes_send_trace_request(app);
		} else if (app->detail_kind == NODES_DETAIL_TELEMETRY) {
			nodes_send_telemetry_request(app);
		} else if (app->detail_kind == NODES_DETAIL_PUBLIC_KEY) {
			return true;
		} else {
			nodes_toast(app, DESKTOP_TEXT_COMMON_SETTINGS_INVALID);
		}
		return true;
	}
	if (nodes_forward_input(zui_text_view_get_screen(app->detail_view), event)) {
		nodes_request_redraw(app);
		return true;
	}

	return false;
}

static void nodes_detail_enter(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_enter(zui_text_view_get_screen(app->detail_view));
	}
}

static void nodes_detail_exit(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_exit(zui_text_view_get_screen(app->detail_view));
	}
}

static void nodes_modal_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_modal_get_screen(app->modal), draw);
	}
}

static bool nodes_modal_input(const struct zui_input_event *event, void *user_data)
{
	struct nodes_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (nodes_should_consume_edge(event)) {
		return true;
	}
	if (nodes_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		nodes_modal_result(app->modal, ZUI_MODAL_RESULT_LEFT, event, app);
		return true;
	}
	if (nodes_forward_input(zui_modal_get_screen(app->modal), event)) {
		nodes_request_redraw(app);
		return true;
	}

	return false;
}

static void nodes_modal_enter(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_enter(zui_modal_get_screen(app->modal));
	}
}

static void nodes_modal_exit(void *user_data)
{
	struct nodes_app *app = user_data;

	if (app != NULL) {
		nodes_forward_exit(zui_modal_get_screen(app->modal));
	}
}

static void nodes_event_listener_done(void)
{
	if (atomic_dec(&nodes_event_inflight) == 1) {
		k_sem_give(&nodes_event_idle);
	}
}

static void nodes_event_wait_idle(void)
{
	while (k_sem_take(&nodes_event_idle, K_NO_WAIT) == 0) {
	}
	while (atomic_get(&nodes_event_inflight) > 0) {
		(void)k_sem_take(&nodes_event_idle, K_FOREVER);
	}
}

static void nodes_queue_node_event(struct nodes_app *app, const struct zbus_channel *chan)
{
	k_spinlock_key_t key;
	bool schedule = false;

	if (app == NULL || chan == NULL) {
		return;
	}

	key = k_spin_lock(&app->event_lock);

	if (chan == &mbs_contact_store_change_chan || chan == &mbs_contact_advert_chan) {
		app->event_flags |= NODES_EVENT_FLAG_SNAPSHOT;
		schedule = true;
	} else if (chan == &mbs_contact_path_response_chan) {
		const mbs_contact_response_path_event *event = zbus_chan_const_msg(chan);

		if (event != NULL) {
			size_t path_len = event->has_out_path
						  ? MIN((size_t)event->out_path_len,
							(size_t)MBS_CONTACT_OUTPATH_MAX_LEN)
						  : 0U;

			memset(&app->discover_event, 0, sizeof(app->discover_event));
			memcpy(app->discover_event.prefix, event->key_prefix,
			       sizeof(app->discover_event.prefix));
			app->discover_event.path_len = (uint8_t)path_len;
			memcpy(app->discover_event.path, event->out_path, path_len);
			app->event_flags |= NODES_EVENT_FLAG_DISCOVER;
			schedule = true;
		}
	} else if (chan == &mbs_contact_trace_path_response_chan) {
		const mbs_contact_response_trace_path_event *event = zbus_chan_const_msg(chan);

		if (event != NULL) {
			memset(&app->trace_event, 0, sizeof(app->trace_event));
			memcpy(app->trace_event.prefix, event->key_prefix,
			       sizeof(app->trace_event.prefix));
			app->trace_event.state = event->state;
			app->event_flags |= NODES_EVENT_FLAG_TRACE;
			schedule = true;
		}
	} else if (chan == &mbs_contact_telemetry_response_chan) {
		const mbs_contact_response_telemetry_event *event = zbus_chan_const_msg(chan);

		if (event != NULL) {
			size_t payload_len = MIN((size_t)event->payload_len,
						 (size_t)MBS_CONTACT_TELEMETRY_PAYLOAD_MAX_LEN);
			memset(&app->telemetry_event, 0, sizeof(app->telemetry_event));
			memcpy(app->telemetry_event.prefix, event->key_prefix,
			       sizeof(app->telemetry_event.prefix));
			app->telemetry_event.payload_len = (uint8_t)payload_len;
			memcpy(app->telemetry_event.payload, event->payload, payload_len);
			app->event_flags |= NODES_EVENT_FLAG_TELEMETRY;
			schedule = true;
		}
	}

	k_spin_unlock(&app->event_lock, key);

	if (schedule) {
		(void)k_work_reschedule(&app->event_work, K_MSEC(NODES_EVENT_WORK_DELAY_MS));
	}
}

static void nodes_node_event_listener_cb(const struct zbus_channel *chan)
{
	struct nodes_app *app;

	atomic_inc(&nodes_event_inflight);
	app = (struct nodes_app *)atomic_ptr_get(&nodes_active_app);
	if (app == NULL) {
		goto out;
	}

	nodes_queue_node_event(app, chan);

out:
	nodes_event_listener_done();
}

ZBUS_LISTENER_DEFINE_WITH_ENABLE(mbs_desktop_nodes_event_listener, nodes_node_event_listener_cb,
				 false);
ZBUS_CHAN_ADD_OBS(mbs_contact_store_change_chan, mbs_desktop_nodes_event_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_contact_advert_chan, mbs_desktop_nodes_event_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_contact_path_response_chan, mbs_desktop_nodes_event_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_contact_trace_path_response_chan, mbs_desktop_nodes_event_listener, 2);
ZBUS_CHAN_ADD_OBS(mbs_contact_telemetry_response_chan, mbs_desktop_nodes_event_listener, 2);

static const struct zui_screen_ops nodes_list_ops = {
	.draw = nodes_list_draw,
	.input = nodes_list_input,
	.enter = nodes_list_enter,
	.exit = nodes_list_exit,
};

static const struct zui_screen_ops nodes_menu_ops = {
	.draw = nodes_menu_draw,
	.input = nodes_menu_input,
	.enter = nodes_menu_enter,
	.exit = nodes_menu_exit,
};

static const struct zui_screen_ops nodes_alias_ops = {
	.draw = nodes_alias_draw,
	.input = nodes_alias_input,
	.enter = nodes_alias_enter,
	.exit = nodes_alias_exit,
};

static const struct zui_screen_ops nodes_detail_ops = {
	.draw = nodes_detail_draw,
	.input = nodes_detail_input,
	.enter = nodes_detail_enter,
	.exit = nodes_detail_exit,
};

static const struct zui_screen_ops nodes_modal_ops = {
	.draw = nodes_modal_draw,
	.input = nodes_modal_input,
	.enter = nodes_modal_enter,
	.exit = nodes_modal_exit,
};

static int nodes_register_screen(struct nodes_app *app, uint32_t id, struct zui_screen *screen)
{
	return zui_router_register_screen(app->router, id, screen);
}

static int nodes_app_create(struct nodes_app *app, struct zui_host *host)
{
	int ret;

	memset(app, 0, sizeof(*app));
	app->host = host;
	app->current_screen = NODES_SCREEN_LIST;
	app->detail_kind = NODES_DETAIL_NONE;
	app->modal_kind = NODES_MODAL_NONE;
	k_sem_init(&app->exit_sem, 0, 1);
	k_work_init_delayable(&app->snapshot_work, nodes_snapshot_work);
	k_work_init_delayable(&app->request_timeout_work, nodes_request_timeout_work);
	k_work_init_delayable(&app->event_work, nodes_event_work);
	app->alias_buf[0] = '\0';

	app->empty_item = (struct zui_list_item){
		.id = NODES_LIST_ITEM_EMPTY,
		.label = DESKTOP_TEXT_NODES_NO_NODES,
		.detail = DESKTOP_TEXT_COMMON_EMPTY,
		.icon = &I_client_10px,
	};
	nodes_update_menu(app);

	app->router = zui_router_create();
	app->list = zui_sublist_create(&(struct zui_sublist_config){
		.title = DESKTOP_TEXT_NODES_TITLE,
		.count = nodes_list_count,
		.get_item = nodes_list_item,
		.selected = nodes_list_selected,
		.user_data = app,
	});
	app->menu = zui_form_create(&(struct zui_form_config){
		.title = DESKTOP_TEXT_NODES_NODE,
		.items = app->menu_items,
		.item_count = app->menu_item_count,
		.activated = nodes_menu_activated,
		.changed = nodes_menu_changed,
		.user_data = app,
	});
	app->alias_editor = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = DESKTOP_TEXT_NODES_MENU_ALIAS,
		.buffer = app->alias_buf,
		.buffer_size = sizeof(app->alias_buf),
		.clear_default_text = false,
		.submitted = nodes_alias_submitted,
		.user_data = app,
	});
	app->detail_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = DESKTOP_TEXT_NODES_TITLE,
		.text = DESKTOP_TEXT_COMMON_EMPTY,
		.font = ZUI_FONT_PRIMARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});
	app->modal = zui_modal_create(&(struct zui_modal_config){
		.title = DESKTOP_TEXT_NODES_TITLE,
		.text = DESKTOP_TEXT_COMMON_EMPTY,
		.center_button = DESKTOP_TEXT_COMMON_OK,
		.result = nodes_modal_result,
		.user_data = app,
	});
	app->list_screen = zui_screen_create(&nodes_list_ops, app);
	app->menu_screen = zui_screen_create(&nodes_menu_ops, app);
	app->alias_screen = zui_screen_create(&nodes_alias_ops, app);
	app->detail_screen = zui_screen_create(&nodes_detail_ops, app);
	app->modal_screen = zui_screen_create(&nodes_modal_ops, app);
	if (app->router == NULL || app->list == NULL || app->menu == NULL ||
	    app->alias_editor == NULL || app->detail_view == NULL || app->modal == NULL ||
	    app->list_screen == NULL || app->menu_screen == NULL || app->alias_screen == NULL ||
	    app->detail_screen == NULL || app->modal_screen == NULL) {
		return -ENOMEM;
	}

	ret = nodes_register_screen(app, NODES_SCREEN_LIST, app->list_screen);
	if (ret != 0) {
		return ret;
	}
	ret = nodes_register_screen(app, NODES_SCREEN_MENU, app->menu_screen);
	if (ret != 0) {
		return ret;
	}
	ret = nodes_register_screen(app, NODES_SCREEN_ALIAS, app->alias_screen);
	if (ret != 0) {
		return ret;
	}
	ret = nodes_register_screen(app, NODES_SCREEN_DETAIL, app->detail_screen);
	if (ret != 0) {
		return ret;
	}
	ret = nodes_register_screen(app, NODES_SCREEN_MODAL, app->modal_screen);
	if (ret != 0) {
		return ret;
	}
	ret = zui_host_attach_router(host, ZUI_LAYER_FULLSCREEN, app->router);
	if (ret != 0) {
		return ret;
	}
	atomic_ptr_set(&nodes_active_app, (atomic_ptr_val_t)app);
	(void)zbus_obs_set_enable(&mbs_desktop_nodes_event_listener, true);
	nodes_switch(app, NODES_SCREEN_LIST);
	(void)k_work_schedule(&app->snapshot_work, K_MSEC(NODES_SNAPSHOT_DELAY_MS));
	return 0;
}

static void nodes_app_destroy(struct nodes_app *app)
{
	struct k_work_sync sync;

	if (app == NULL) {
		return;
	}

	(void)zbus_obs_set_enable(&mbs_desktop_nodes_event_listener, false);
	if ((struct nodes_app *)atomic_ptr_get(&nodes_active_app) == app) {
		atomic_ptr_set(&nodes_active_app, (atomic_ptr_val_t)NULL);
	}
	nodes_event_wait_idle();
	(void)k_work_cancel_delayable_sync(&app->event_work, &sync);
	(void)k_work_cancel_delayable_sync(&app->snapshot_work, &sync);
	(void)k_work_cancel_delayable_sync(&app->request_timeout_work, &sync);
	if (app->host != NULL) {
		(void)zui_host_detach_router(app->host, ZUI_LAYER_FULLSCREEN);
	}
	if (app->router != NULL) {
		(void)zui_router_unregister_screen(app->router, NODES_SCREEN_LIST);
		(void)zui_router_unregister_screen(app->router, NODES_SCREEN_MENU);
		(void)zui_router_unregister_screen(app->router, NODES_SCREEN_ALIAS);
		(void)zui_router_unregister_screen(app->router, NODES_SCREEN_DETAIL);
		(void)zui_router_unregister_screen(app->router, NODES_SCREEN_MODAL);
	}
	zui_screen_destroy(app->modal_screen);
	zui_screen_destroy(app->detail_screen);
	zui_screen_destroy(app->alias_screen);
	zui_screen_destroy(app->menu_screen);
	zui_screen_destroy(app->list_screen);
	zui_modal_destroy(app->modal);
	zui_text_view_destroy(app->detail_view);
	zui_text_editor_destroy(app->alias_editor);
	zui_form_destroy(app->menu);
	zui_sublist_destroy(app->list);
	zui_router_destroy(app->router);
}

static void nodes_app_main(void *args)
{
	struct mbs_desktop_app_args *app_args = args;
	struct nodes_app *app;
	int ret;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	app = k_calloc(1U, sizeof(*app));
	if (app == NULL) {
		return;
	}

	ret = nodes_app_create(app, app_args->host);
	if (ret != 0) {
		LOG_WRN("Failed to start Nodes app shell: %d", ret);
		nodes_app_destroy(app);
		k_free(app);
		return;
	}

	while (!app->exit_requested) {
		(void)k_sem_take(&app->exit_sem, K_FOREVER);
	}
	nodes_app_destroy(app);
	k_free(app);
}

MBS_DESKTOP_APP_DEFINE(MBS_DESKTOP_APP_ID_NODES,
			   MBS_DESKTOP_APP_NAME_NODES,
			   nodes_app_main,
			   2048,
			   &A_nodes_14x14,
			   MBS_DESKTOP_APP_MENU_INDEX_NODES);
