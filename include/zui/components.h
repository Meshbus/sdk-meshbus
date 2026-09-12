/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZUI high-level component API
 */

#ifndef MESHBUS_INCLUDE_ZUI_COMPONENTS_H_
#define MESHBUS_INCLUDE_ZUI_COMPONENTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zui/assets.h>
#include <zui/input.h>
#include <zui/zui_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_draw_ctx;
struct zui_screen;

struct zui_list;
struct zui_sublist;
struct zui_form;
struct zui_text_editor;
struct zui_number_editor;
struct zui_hex_editor;
struct zui_text_view;
struct zui_file_picker;
struct zui_file_picker_entry;
struct zui_modal;
struct zui_progress;
struct zui_actions;
struct zui_composite;
struct zui_element;
struct zui_blank;
struct k_work_q;

#define ZUI_TEXT_LINE_SPACING_DEFAULT 1U

/**
 * @defgroup zui_components ZUI components
 * @{
 *
 * Components are heap-allocated opaque objects. `*_create()` returns an owned
 * component handle, and the caller must release it with the matching
 * `*_destroy()` function. `*_get_screen()` returns a borrowed screen handle
 * owned by the component; it remains valid until the component is destroyed.
 *
 * Config structs are copied by value. String, item-array, icon, byte-buffer,
 * callback, and user-data pointers referenced by a config remain caller-owned
 * and must stay valid until the component is updated or destroyed unless an API
 * explicitly documents an internal copy.
 *
 * Component APIs return 0 on success and negative errno values on failure.
 */

typedef bool (*zui_file_picker_filter_cb)(const char *path, void *user_data);
struct zui_file_picker_probe_result {
	/** Optional borrowed icon used for this entry. */
	const struct zui_icon *icon;
	/** Optional borrowed MONO bitmap used when icon is NULL. */
	const uint8_t *icon_data;
	uint16_t icon_width;
	uint16_t icon_height;
};

typedef bool (*zui_file_picker_probe_cb)(const char *path, char *name, size_t name_size,
					 struct zui_file_picker_probe_result *result,
					 void *user_data);
typedef int (*zui_file_picker_load_cb)(struct zui_file_picker *picker, const char *path,
				       struct zui_file_picker_entry *entries, size_t capacity,
				       size_t *count, void *user_data);
typedef void (*zui_file_picker_done_cb)(struct zui_file_picker *picker, const char *path,
					const struct zui_input_event *event, void *user_data);
typedef bool (*zui_text_editor_validate_cb)(const char *text, char *error, size_t error_size,
					    void *user_data);
typedef void (*zui_list_selected_cb)(struct zui_list *list, uint32_t id, size_t index,
				     const struct zui_input_event *event, void *user_data);
typedef void (*zui_sublist_selected_cb)(struct zui_sublist *list, uint32_t id, size_t index,
					const struct zui_input_event *event, void *user_data);
typedef void (*zui_form_changed_cb)(struct zui_form *form, uint32_t id, size_t option_index,
				    void *user_data);
typedef void (*zui_form_activated_cb)(struct zui_form *form, uint32_t id,
				      const struct zui_input_event *event, void *user_data);
typedef void (*zui_text_editor_changed_cb)(struct zui_text_editor *editor, const char *text,
					   void *user_data);
typedef void (*zui_text_editor_submitted_cb)(struct zui_text_editor *editor, const char *text,
					     void *user_data);
typedef void (*zui_number_editor_submitted_cb)(struct zui_number_editor *editor, int64_t value,
					       void *user_data);
typedef void (*zui_hex_editor_changed_cb)(struct zui_hex_editor *editor, const uint8_t *bytes,
					  size_t byte_count, void *user_data);
typedef void (*zui_hex_editor_submitted_cb)(struct zui_hex_editor *editor, const uint8_t *bytes,
					    size_t byte_count, void *user_data);
typedef void (*zui_actions_selected_cb)(struct zui_actions *actions, uint32_t id,
					const struct zui_input_event *event, void *user_data);
typedef void (*zui_composite_draw_cb)(struct zui_composite *composite,
				      struct zui_draw_ctx *ctx, void *user_data);
typedef bool (*zui_composite_input_cb)(struct zui_composite *composite,
				       const struct zui_input_event *event, void *user_data);

enum zui_form_value_align {
	ZUI_FORM_VALUE_ALIGN_CENTER = 0,
	ZUI_FORM_VALUE_ALIGN_RIGHT,
};

enum zui_move {
	ZUI_MOVE_PREVIOUS,
	ZUI_MOVE_NEXT,
	ZUI_MOVE_FIRST,
	ZUI_MOVE_LAST,
};

struct zui_list_item {
	uint32_t id;
	/** Borrowed strings and icons; keep valid while the list can display this item. */
	const char *label;
	const char *detail;
	const struct zui_icon *icon;
	struct zui_icon_anim *icon_anim;
};

typedef size_t (*zui_list_count_cb)(void *user_data);
typedef int (*zui_list_item_cb)(size_t index, struct zui_list_item *item, void *user_data);

struct zui_list_config {
	/** Borrowed item array. Mutually exclusive with count/get_item. */
	const struct zui_list_item *items;
	size_t item_count;
	/**
	 * Copy items into component-owned RAM.
	 *
	 * Leave false to borrow the item array and save RAM. Set true when the
	 * caller needs zui_list_set_item(), or when the item array cannot remain
	 * valid for the list lifetime.
	 */
	bool copy_items;
	/** Dynamic item count provider. Use with get_item. */
	zui_list_count_cb count;
	/** Dynamic item provider. The component copies each returned item. */
	zui_list_item_cb get_item;
	zui_list_selected_cb selected;
	void *user_data;
};

struct zui_sublist_config {
	const char *title;
	/** Borrowed item array. Mutually exclusive with count/get_item. */
	const struct zui_list_item *items;
	size_t item_count;
	/**
	 * Copy items into component-owned RAM.
	 *
	 * Leave false to borrow the item array and save RAM. Set true when the
	 * caller needs zui_sublist_set_item(), or when the item array cannot
	 * remain valid for the sublist lifetime.
	 */
	bool copy_items;
	zui_list_count_cb count;
	zui_list_item_cb get_item;
	zui_sublist_selected_cb selected;
	void *user_data;
};

struct zui_form_item {
	uint32_t id;
	const char *label;
	const char *value_text;
	const char *const *options;
	size_t option_count;
	size_t option_index;
	enum zui_form_value_align value_align;
	void *user_data;
};

struct zui_form_config {
	const char *title;
	const struct zui_form_item *items;
	size_t item_count;
	zui_form_changed_cb changed;
	zui_form_activated_cb activated;
	void *user_data;
};

/**
 * Text editor configuration.
 *
 * A short BACK removes one character, while a long BACK is left unhandled so
 * the owning screen can leave the editor without submitting. Keypad '*' uses
 * the same delete and leave behavior through direct handling or host fallback.
 */
struct zui_text_editor_config {
	const char *title;
	/** Caller-owned mutable text buffer edited in place. */
	char *buffer;
	size_t buffer_size;
	size_t min_length;
	bool clear_on_enter;
	bool clear_default_text;
	zui_text_editor_validate_cb validate;
	zui_text_editor_changed_cb changed;
	zui_text_editor_submitted_cb submitted;
	void *user_data;
};

/**
 * Number editor configuration.
 *
 * A short BACK or keypad '*' removes one digit, while a long BACK is left
 * unhandled so the owning screen can cancel editing. A short SELECT activates
 * the focused on-screen key, and a long SELECT submits the current value.
 */
struct zui_number_editor_config {
	const char *title;
	int64_t value;
	int64_t min_value;
	int64_t max_value;
	/** Maximum decimal digits accepted by the on-screen keypad. 0 selects the default. */
	size_t max_digits;
	bool unsigned_only;
	/** Preserve and render leading zeroes for unsigned values when max_digits is set. */
	bool keep_leading_zeros;
	zui_number_editor_submitted_cb submitted;
	void *user_data;
};

/**
 * Hex editor configuration.
 *
 * A short BACK clears the selected byte unless the alternate input view is
 * active, where it returns to the keyboard. A long BACK is left unhandled so
 * the owning screen can leave the editor without submitting.
 */
struct zui_hex_editor_config {
	const char *title;
	/** Caller-owned mutable byte buffer edited in place. */
	uint8_t *bytes;
	/** Total byte capacity of bytes. */
	size_t byte_count;
	/** Initial valid payload length; may include zero-valued bytes. */
	size_t payload_size;
	zui_hex_editor_changed_cb changed;
	zui_hex_editor_submitted_cb submitted;
	void *user_data;
};

enum zui_text_view_mode {
	ZUI_TEXT_VIEW_MODE_TEXT,
	ZUI_TEXT_VIEW_MODE_HEX,
};

struct zui_text_view_config {
	const char *title;
	const char *text;
	enum zui_font font;
	enum zui_text_view_mode mode;
	/** Extra pixels inserted between text lines. 0 selects the default. */
	uint8_t text_line_spacing;
	bool focus_end;
};

struct zui_file_picker_entry {
	const char *path;
	bool is_dir;
	const struct zui_icon *icon;
};

struct zui_file_picker_config {
	const char *title;
	/** Root path boundary. Open and parent navigation cannot escape this normalized path. */
	const char *base_path;
	const char *extension;
	bool skip_assets;
	bool hide_dot_files;
	bool hide_extension;
	const struct zui_icon *file_icon;
	/** Optional loader. Fill entries with caller-owned paths valid until the callback returns. */
	zui_file_picker_load_cb load;
	zui_file_picker_filter_cb filter;
	zui_file_picker_probe_cb probe;
	zui_file_picker_done_cb selected;
	zui_file_picker_done_cb long_selected;
	void *user_data;
	/** Optional queue for asynchronous loading. NULL loads synchronously in caller context. */
	struct k_work_q *work_q;
};

enum zui_modal_result {
	ZUI_MODAL_RESULT_LEFT,
	ZUI_MODAL_RESULT_CENTER,
	ZUI_MODAL_RESULT_RIGHT,
	ZUI_MODAL_RESULT_UP,
	ZUI_MODAL_RESULT_DOWN,
};

typedef void (*zui_modal_result_cb)(struct zui_modal *modal, enum zui_modal_result result,
				    const struct zui_input_event *event, void *user_data);

struct zui_modal_config {
	const char *title;
	const char *text;
	const struct zui_icon *icon;
	struct zui_text_placement title_placement;
	struct zui_text_placement text_placement;
	/** Extra pixels inserted between modal text lines. 0 selects the default. */
	uint8_t text_line_spacing;
	struct zui_icon_placement icon_placement;
	const char *up_button;
	const char *left_button;
	const char *center_button;
	const char *right_button;
	const char *down_button;
	zui_modal_result_cb result;
	void *user_data;
};

struct zui_progress_config {
	const char *text;
	float value;
};

struct zui_action_item {
	uint32_t id;
	const char *label;
	const struct zui_icon *icon;
	bool is_control;
};

struct zui_actions_config {
	const char *title;
	/** Borrowed item array. */
	const struct zui_action_item *items;
	size_t item_count;
	/**
	 * Copy items into component-owned RAM.
	 *
	 * Leave false to borrow the item array and save RAM. Set true when the
	 * item array cannot remain valid for the actions lifetime.
	 */
	bool copy_items;
	zui_actions_selected_cb selected;
	void *user_data;
};

struct zui_composite_config {
	zui_composite_draw_cb draw;
	zui_composite_input_cb input;
	void *user_data;
};

enum zui_element_button {
	ZUI_ELEMENT_BUTTON_LEFT,
	ZUI_ELEMENT_BUTTON_CENTER,
	ZUI_ELEMENT_BUTTON_RIGHT,
};

typedef void (*zui_element_button_cb)(enum zui_element_button button,
				      struct zui_element *element,
				      const struct zui_input_event *event, void *user_data);

struct zui_list *zui_list_create(const struct zui_list_config *config);
void zui_list_destroy(struct zui_list *list);
struct zui_screen *zui_list_get_screen(struct zui_list *list);
int zui_list_update(struct zui_list *list, const struct zui_list_config *config);
int zui_list_reload(struct zui_list *list);
size_t zui_list_count(const struct zui_list *list);
size_t zui_list_selected(const struct zui_list *list);
int zui_list_select(struct zui_list *list, size_t index);
int zui_list_move(struct zui_list *list, enum zui_move move);
int zui_list_activate(struct zui_list *list, const struct zui_input_event *event);
int zui_list_set_item(struct zui_list *list, size_t index, const struct zui_list_item *item);

struct zui_sublist *zui_sublist_create(const struct zui_sublist_config *config);
void zui_sublist_destroy(struct zui_sublist *list);
struct zui_screen *zui_sublist_get_screen(struct zui_sublist *list);
int zui_sublist_update(struct zui_sublist *list, const struct zui_sublist_config *config);
int zui_sublist_reload(struct zui_sublist *list);
size_t zui_sublist_count(const struct zui_sublist *list);
size_t zui_sublist_selected(const struct zui_sublist *list);
int zui_sublist_select(struct zui_sublist *list, size_t index);
int zui_sublist_move(struct zui_sublist *list, enum zui_move move);
int zui_sublist_activate(struct zui_sublist *list, const struct zui_input_event *event);
int zui_sublist_set_item(struct zui_sublist *list, size_t index,
			 const struct zui_list_item *item);

struct zui_form *zui_form_create(const struct zui_form_config *config);
void zui_form_destroy(struct zui_form *form);
struct zui_screen *zui_form_get_screen(struct zui_form *form);
int zui_form_update(struct zui_form *form, const struct zui_form_config *config);
size_t zui_form_count(const struct zui_form *form);
size_t zui_form_selected(const struct zui_form *form);
int zui_form_select(struct zui_form *form, size_t index);
int zui_form_move(struct zui_form *form, enum zui_move move);
size_t zui_form_option(const struct zui_form *form, uint32_t id);
int zui_form_set_option(struct zui_form *form, uint32_t id, size_t option_index);
int zui_form_move_option(struct zui_form *form, uint32_t id, enum zui_move move);
const char *zui_form_value_text(const struct zui_form *form, uint32_t id);
int zui_form_set_value_text(struct zui_form *form, uint32_t id, const char *value_text);
void *zui_form_item_user_data(const struct zui_form *form, uint32_t id);
int zui_form_activate(struct zui_form *form, const struct zui_input_event *event);
int zui_form_set_item(struct zui_form *form, size_t index, const struct zui_form_item *item);

struct zui_text_editor *zui_text_editor_create(const struct zui_text_editor_config *config);
void zui_text_editor_destroy(struct zui_text_editor *editor);
struct zui_screen *zui_text_editor_get_screen(struct zui_text_editor *editor);
int zui_text_editor_update(struct zui_text_editor *editor,
			   const struct zui_text_editor_config *config);
const char *zui_text_editor_text(const struct zui_text_editor *editor);
int zui_text_editor_set_text(struct zui_text_editor *editor, const char *text);
int zui_text_editor_set_validator(struct zui_text_editor *editor,
				  zui_text_editor_validate_cb validate, void *user_data);
zui_text_editor_validate_cb zui_text_editor_get_validator(const struct zui_text_editor *editor,
							  void **user_data);
int zui_text_editor_submit(struct zui_text_editor *editor);
int zui_text_editor_validate_file(struct zui_text_editor *editor, const char *base_path,
				  const char *extension, const char *current_name);

struct zui_number_editor *zui_number_editor_create(const struct zui_number_editor_config *config);
void zui_number_editor_destroy(struct zui_number_editor *editor);
struct zui_screen *zui_number_editor_get_screen(struct zui_number_editor *editor);
int zui_number_editor_update(struct zui_number_editor *editor,
			     const struct zui_number_editor_config *config);
int64_t zui_number_editor_value(const struct zui_number_editor *editor);
int zui_number_editor_set_value(struct zui_number_editor *editor, int64_t value);
int zui_number_editor_step(struct zui_number_editor *editor, int64_t delta);
int zui_number_editor_submit(struct zui_number_editor *editor);

struct zui_hex_editor *zui_hex_editor_create(const struct zui_hex_editor_config *config);
void zui_hex_editor_destroy(struct zui_hex_editor *editor);
struct zui_screen *zui_hex_editor_get_screen(struct zui_hex_editor *editor);
int zui_hex_editor_update(struct zui_hex_editor *editor,
			  const struct zui_hex_editor_config *config);
size_t zui_hex_editor_size(const struct zui_hex_editor *editor);
size_t zui_hex_editor_payload_size(const struct zui_hex_editor *editor);
const uint8_t *zui_hex_editor_data(const struct zui_hex_editor *editor);
int zui_hex_editor_set_data(struct zui_hex_editor *editor, const uint8_t *bytes, size_t byte_count);
int zui_hex_editor_submit(struct zui_hex_editor *editor);

struct zui_text_view *zui_text_view_create(const struct zui_text_view_config *config);
void zui_text_view_destroy(struct zui_text_view *view);
struct zui_screen *zui_text_view_get_screen(struct zui_text_view *view);
int zui_text_view_update(struct zui_text_view *view, const struct zui_text_view_config *config);
size_t zui_text_view_scroll(const struct zui_text_view *view);
int zui_text_view_set_scroll(struct zui_text_view *view, size_t line);
int zui_text_view_scroll_by(struct zui_text_view *view, int32_t lines);

struct zui_file_picker *zui_file_picker_create(const struct zui_file_picker_config *config);
void zui_file_picker_destroy(struct zui_file_picker *selector);
struct zui_screen *zui_file_picker_get_screen(struct zui_file_picker *selector);
int zui_file_picker_update(struct zui_file_picker *selector,
			   const struct zui_file_picker_config *config);
const char *zui_file_picker_path(const struct zui_file_picker *selector);
int zui_file_picker_open(struct zui_file_picker *selector, const char *path);
int zui_file_picker_stop(struct zui_file_picker *selector);
int zui_file_picker_move(struct zui_file_picker *selector, enum zui_move move);
int zui_file_picker_choose(struct zui_file_picker *selector, const struct zui_input_event *event);
int zui_file_picker_set_work_q(struct zui_file_picker *selector, struct k_work_q *work_q);

struct zui_modal *zui_modal_create(const struct zui_modal_config *config);
void zui_modal_destroy(struct zui_modal *modal);
struct zui_screen *zui_modal_get_screen(struct zui_modal *modal);
int zui_modal_update(struct zui_modal *modal, const struct zui_modal_config *config);
int zui_modal_submit(struct zui_modal *modal, enum zui_modal_result result,
		       const struct zui_input_event *event);

struct zui_progress *zui_progress_create(const struct zui_progress_config *config);
void zui_progress_destroy(struct zui_progress *progress);
struct zui_screen *zui_progress_get_screen(struct zui_progress *progress);
int zui_progress_update(struct zui_progress *progress, const struct zui_progress_config *config);
float zui_progress_value(const struct zui_progress *progress);
int zui_progress_set(struct zui_progress *progress, float value, const char *text);

struct zui_actions *zui_actions_create(const struct zui_actions_config *config);
void zui_actions_destroy(struct zui_actions *actions);
struct zui_screen *zui_actions_get_screen(struct zui_actions *actions);
int zui_actions_update(struct zui_actions *actions, const struct zui_actions_config *config);
size_t zui_actions_selected(const struct zui_actions *actions);
int zui_actions_select(struct zui_actions *actions, size_t index);
int zui_actions_move(struct zui_actions *actions, enum zui_move move);
int zui_actions_activate(struct zui_actions *actions, const struct zui_input_event *event);

struct zui_composite *zui_composite_create(const struct zui_composite_config *config);
void zui_composite_destroy(struct zui_composite *composite);
struct zui_screen *zui_composite_get_screen(struct zui_composite *composite);
int zui_composite_update(struct zui_composite *composite,
			 const struct zui_composite_config *config);
int zui_composite_request_redraw(struct zui_composite *composite);

struct zui_element *zui_element_create(void);
void zui_element_destroy(struct zui_element *element);
struct zui_screen *zui_element_get_screen(struct zui_element *element);
int zui_element_reset(struct zui_element *element);
int zui_element_add_string(struct zui_element *element, struct zui_point pos,
			   enum zui_align horizontal, enum zui_align vertical,
			   enum zui_font font, const char *text);
int zui_element_add_multiline_string(struct zui_element *element, const struct zui_rect *rect,
				     enum zui_align horizontal, enum zui_align vertical,
				     enum zui_font font, const char *text);
int zui_element_add_text_box(struct zui_element *element, const struct zui_rect *rect,
			     enum zui_align horizontal, enum zui_align vertical,
			     const char *text, bool strip_to_dots);
int zui_element_add_text_scroll(struct zui_element *element, const struct zui_rect *rect,
				enum zui_font font, const char *text);
int zui_element_add_button(struct zui_element *element, enum zui_element_button button,
			   const char *text, zui_element_button_cb callback, void *user_data);
int zui_element_add_icon(struct zui_element *element, struct zui_point pos,
				const struct zui_icon *icon);
int zui_element_add_rect(struct zui_element *element, const struct zui_rect *rect,
				uint16_t radius, bool fill);
int zui_element_add_circle(struct zui_element *element, struct zui_point center,
			   uint16_t radius, bool fill);
int zui_element_add_line(struct zui_element *element, struct zui_point start,
				struct zui_point end);

struct zui_blank *zui_blank_create(void);
void zui_blank_destroy(struct zui_blank *blank);
struct zui_screen *zui_blank_get_screen(struct zui_blank *blank);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* MESHBUS_INCLUDE_ZUI_COMPONENTS_H_ */
