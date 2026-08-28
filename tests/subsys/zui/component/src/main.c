// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/zui/zui.h>

struct callback_state {
	uint32_t id;
	size_t index;
	size_t option;
	size_t byte_count;
	int64_t value;
	enum zui_modal_result modal_result;
	size_t count;
	bool draw_called;
	bool input_called;
	char text[32];
	uint8_t bytes[16];
};

static const struct zui_input_event select_click = {
	.sequence = 1,
	.code = ZUI_INPUT_CODE_SELECT,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event select_press = {
	.sequence = 11,
	.code = ZUI_INPUT_CODE_SELECT,
	.action = ZUI_INPUT_ACTION_PRESS,
};

static const struct zui_input_event select_release = {
	.sequence = 12,
	.code = ZUI_INPUT_CODE_SELECT,
	.action = ZUI_INPUT_ACTION_RELEASE,
};

static const struct zui_input_event long_select = {
	.sequence = 2,
	.code = ZUI_INPUT_CODE_SELECT,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
};

static const struct zui_input_event up_click = {
	.sequence = 3,
	.code = ZUI_INPUT_CODE_UP,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event down_click = {
	.sequence = 4,
	.code = ZUI_INPUT_CODE_DOWN,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event down_press = {
	.sequence = 13,
	.code = ZUI_INPUT_CODE_DOWN,
	.action = ZUI_INPUT_ACTION_PRESS,
};

static const struct zui_input_event left_click = {
	.sequence = 5,
	.code = ZUI_INPUT_CODE_LEFT,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event right_click = {
	.sequence = 6,
	.code = ZUI_INPUT_CODE_RIGHT,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event long_left = {
	.sequence = 7,
	.code = ZUI_INPUT_CODE_LEFT,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
};

static const struct zui_input_event long_right = {
	.sequence = 8,
	.code = ZUI_INPUT_CODE_RIGHT,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
};

static const struct zui_input_event back_click = {
	.sequence = 9,
	.code = ZUI_INPUT_CODE_BACK,
	.action = ZUI_INPUT_ACTION_CLICK,
};

static const struct zui_input_event long_back = {
	.sequence = 10,
	.code = ZUI_INPUT_CODE_BACK,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
};

static const struct zui_input_event keypad_2 = {
	.sequence = 20,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '2',
};

static const struct zui_input_event __maybe_unused keypad_0 = {
	.sequence = 26,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '0',
};

static const struct zui_input_event __maybe_unused keypad_1 = {
	.sequence = 29,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '1',
};

static const struct zui_input_event __maybe_unused keypad_3 = {
	.sequence = 21,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '3',
};

static const struct zui_input_event keypad_4 = {
	.sequence = 22,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '4',
};

static const struct zui_input_event __maybe_unused keypad_6 = {
	.sequence = 27,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '6',
};

static const struct zui_input_event __maybe_unused keypad_9 = {
	.sequence = 28,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '9',
};

static const struct zui_input_event keypad_star = {
	.sequence = 23,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '*',
};

static const struct zui_input_event keypad_star_long = {
	.sequence = 30,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
	.value = '*',
};

static const struct zui_input_event keypad_dot = {
	.sequence = 24,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_CLICK,
	.value = '.',
};

static const struct zui_input_event __maybe_unused keypad_dot_long = {
	.sequence = 25,
	.code = ZUI_INPUT_CODE_KEYPAD,
	.action = ZUI_INPUT_ACTION_LONG_PRESS,
	.value = '.',
};

static void list_selected(struct zui_list *list, uint32_t id, size_t index,
			  const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(event);

	state->id = id;
	state->index = index;
	state->count++;
}

static void sublist_selected(struct zui_sublist *list, uint32_t id, size_t index,
			     const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(event);

	state->id = id;
	state->index = index;
	state->count++;
}

static void form_changed(struct zui_form *form, uint32_t id, size_t option_index, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(form);

	state->id = id;
	state->option = option_index;
	state->count++;
}

static void form_activated(struct zui_form *form, uint32_t id, const struct zui_input_event *event,
			   void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(form);
	ARG_UNUSED(event);

	state->id = id;
	state->count++;
}

static void text_changed(struct zui_text_editor *editor, const char *text, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(editor);

	(void)snprintk(state->text, sizeof(state->text), "%s", text);
	state->count++;
}

static void text_submitted(struct zui_text_editor *editor, const char *text, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(editor);

	(void)snprintk(state->text, sizeof(state->text), "%s", text);
	state->count++;
}

static bool text_validate(const char *text, char *error, size_t error_size, void *user_data)
{
	ARG_UNUSED(error);
	ARG_UNUSED(error_size);
	ARG_UNUSED(user_data);

	return strcmp(text, "bad") != 0;
}

static void value_submitted(struct zui_number_editor *editor, int64_t value, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(editor);

	state->value = value;
	state->count++;
}

static void hex_submitted(struct zui_hex_editor *editor, const uint8_t *bytes, size_t byte_count,
			  void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(editor);

	memcpy(state->bytes, bytes, MIN(byte_count, sizeof(state->bytes)));
	state->byte_count = byte_count;
	state->count++;
}

static int file_load(struct zui_file_picker *picker, const char *path,
		     struct zui_file_picker_entry *entries, size_t capacity, size_t *count,
		     void *user_data)
{
	ARG_UNUSED(picker);
	ARG_UNUSED(user_data);

	zassert_true(capacity >= 9U);
	if (strcmp(path, "/apps") == 0) {
		entries[0] = (struct zui_file_picker_entry){.path = "app.txt"};
		entries[1] = (struct zui_file_picker_entry){.path = "assets", .is_dir = true};
		entries[2] = (struct zui_file_picker_entry){.path = "hidden.bin"};
		*count = 3U;
		return 0;
	}

	entries[0] = (struct zui_file_picker_entry){.path = "apps", .is_dir = true};
	entries[1] = (struct zui_file_picker_entry){.path = ".hidden.txt"};
	entries[2] = (struct zui_file_picker_entry){.path = "note.txt"};
	entries[3] = (struct zui_file_picker_entry){.path = "alpha.txt"};
	entries[4] = (struct zui_file_picker_entry){.path = "beta.txt"};
	entries[5] = (struct zui_file_picker_entry){.path = "gamma.txt"};
	entries[6] = (struct zui_file_picker_entry){.path = "delta.txt"};
	entries[7] = (struct zui_file_picker_entry){.path = "omega.txt"};
	entries[8] = (struct zui_file_picker_entry){.path = "firmware.bin"};
	*count = 9U;
	return 0;
}

static bool file_filter(const char *path, void *user_data)
{
	ARG_UNUSED(user_data);

	return strcmp(path, "/firmware.bin") != 0;
}

static bool file_probe(const char *path, char *name, size_t name_size,
		       struct zui_file_picker_probe_result *result, void *user_data)
{
	ARG_UNUSED(result);
	ARG_UNUSED(user_data);

	if (strcmp(path, "/omega.txt") == 0) {
		return false;
	}
	if (strcmp(path, "/delta.txt") == 0) {
		(void)snprintk(name, name_size, "Delta");
	}

	return true;
}

static void file_picked(struct zui_file_picker *picker, const char *path,
			const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(picker);
	ARG_UNUSED(event);

	(void)snprintk(state->text, sizeof(state->text), "%s", path);
	state->count++;
}

static void modal_result_cb(struct zui_modal *modal, enum zui_modal_result result,
			    const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(modal);
	ARG_UNUSED(event);

	state->modal_result = result;
	state->count++;
}

static void action_selected(struct zui_actions *actions, uint32_t id,
			    const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(actions);
	ARG_UNUSED(event);

	state->id = id;
	state->count++;
}

static void element_button(enum zui_element_button button, struct zui_element *element,
			  const struct zui_input_event *event, void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(element);
	ARG_UNUSED(event);

	state->id = button;
	state->count++;
}

static void composite_draw(struct zui_composite *composite, struct zui_draw_ctx *ctx,
			   void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(composite);
	ARG_UNUSED(ctx);

	state->draw_called = true;
}

static bool composite_input(struct zui_composite *composite, const struct zui_input_event *event,
			    void *user_data)
{
	struct callback_state *state = user_data;

	ARG_UNUSED(composite);
	ARG_UNUSED(event);

	state->input_called = true;
	return true;
}

static void assert_screen_draws(struct zui_screen *screen)
{
	struct zui_draw_ctx *draw = zui_draw_ctx_create(NULL);

	zassert_not_null(screen);
	zassert_not_null(draw);
	zassert_ok(zui_screen_draw(screen, draw));
	zui_draw_ctx_destroy(draw);
}

ZTEST(zui_component, test_list_and_form)
{
	struct callback_state state = {0};
	const struct zui_list_item list_items[] = {
		{.id = 10, .label = "Alpha", .detail = "A"},
		{.id = 20, .label = "Beta", .detail = "B"},
		{.id = 30, .label = "Gamma", .detail = "C"},
	};
	const struct zui_list_item replacement_item = {
		.id = 40,
		.label = "Delta",
		.detail = "D",
	};
	const char *const options[] = {"off", "on", "auto"};
	const struct zui_form_item form_items[] = {
		{.id = 1, .label = "Mode", .options = options, .option_count = ARRAY_SIZE(options)},
		{.id = 2, .label = "Name", .value_text = "node", .user_data = &state},
	};
	struct zui_list *list = zui_list_create(&(struct zui_list_config){
		.items = list_items,
		.item_count = ARRAY_SIZE(list_items),
		.selected = list_selected,
		.user_data = &state,
	});
	struct zui_sublist *sublist = zui_sublist_create(&(struct zui_sublist_config){
		.title = "Sublist",
		.items = list_items,
		.item_count = ARRAY_SIZE(list_items),
		.selected = sublist_selected,
		.user_data = &state,
	});
	struct zui_list *owned_list = zui_list_create(&(struct zui_list_config){
		.items = list_items,
		.item_count = ARRAY_SIZE(list_items),
		.copy_items = true,
		.selected = list_selected,
		.user_data = &state,
	});
	struct zui_sublist *owned_sublist = zui_sublist_create(&(struct zui_sublist_config){
		.title = "Owned",
		.items = list_items,
		.item_count = ARRAY_SIZE(list_items),
		.copy_items = true,
		.selected = sublist_selected,
		.user_data = &state,
	});
	struct zui_form *form = zui_form_create(&(struct zui_form_config){
		.title = "Form",
		.items = form_items,
		.item_count = ARRAY_SIZE(form_items),
		.changed = form_changed,
		.activated = form_activated,
		.user_data = &state,
	});

	zassert_not_null(list);
	zassert_equal(zui_list_count(list), 3U);
	zassert_ok(zui_list_move(list, ZUI_MOVE_NEXT));
	zassert_equal(zui_list_selected(list), 1U);
	zassert_ok(zui_list_activate(list, &select_click));
	zassert_equal(state.id, 20U);
	state.count = 0U;
	zassert_equal(zui_screen_submit_input(zui_list_get_screen(list), &down_press), 0);
	zassert_equal(zui_list_selected(list), 1U);
	zassert_equal(zui_screen_submit_input(zui_list_get_screen(list), &select_press), 0);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_list_get_screen(list), &select_release), 0);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_list_get_screen(list), &select_click), 1);
	zassert_equal(state.count, 1U);
	zassert_equal(zui_list_set_item(list, 0U, &replacement_item), -EACCES);
	assert_screen_draws(zui_list_get_screen(list));

	zassert_not_null(owned_list);
	zassert_ok(zui_list_set_item(owned_list, 0U, &replacement_item));
	zassert_ok(zui_list_select(owned_list, 0U));
	zassert_ok(zui_list_activate(owned_list, &select_click));
	zassert_equal(state.id, 40U);

	zassert_not_null(sublist);
	zassert_equal(zui_sublist_count(sublist), 3U);
	zassert_ok(zui_sublist_move(sublist, ZUI_MOVE_LAST));
	zassert_equal(zui_sublist_selected(sublist), 2U);
	zassert_ok(zui_sublist_activate(sublist, &select_click));
	zassert_equal(state.id, 30U);
	state.count = 0U;
	zassert_equal(zui_screen_submit_input(zui_sublist_get_screen(sublist), &down_press), 0);
	zassert_equal(zui_sublist_selected(sublist), 2U);
	zassert_equal(zui_screen_submit_input(zui_sublist_get_screen(sublist), &select_press), 0);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_sublist_get_screen(sublist), &long_select), 1);
	zassert_equal(state.count, 1U);
	zassert_equal(zui_sublist_set_item(sublist, 0U, &replacement_item), -EACCES);
	assert_screen_draws(zui_sublist_get_screen(sublist));

	zassert_not_null(owned_sublist);
	zassert_ok(zui_sublist_set_item(owned_sublist, 0U, &replacement_item));
	zassert_ok(zui_sublist_select(owned_sublist, 0U));
	zassert_ok(zui_sublist_activate(owned_sublist, &select_click));
	zassert_equal(state.id, 40U);

	zassert_not_null(form);
	zassert_equal(zui_form_count(form), 2U);
	zassert_ok(zui_form_move_option(form, 1, ZUI_MOVE_NEXT));
	zassert_equal(zui_form_option(form, 1), 1U);
	zassert_str_equal(zui_form_value_text(form, 1), "on");
	zassert_equal(zui_form_item_user_data(form, 2), &state);
	zassert_ok(zui_form_select(form, 1));
	zassert_ok(zui_form_activate(form, &select_click));
	zassert_equal(state.id, 2U);
	state.count = 0U;
	zassert_equal(zui_screen_submit_input(zui_form_get_screen(form), &down_press), 0);
	zassert_equal(zui_form_selected(form), 1U);
	zassert_equal(zui_screen_submit_input(zui_form_get_screen(form), &select_press), 0);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_form_get_screen(form), &long_select), 1);
	zassert_equal(state.id, 2U);
	zassert_equal(state.count, 1U);
	assert_screen_draws(zui_form_get_screen(form));

	zui_form_destroy(form);
	zui_sublist_destroy(owned_sublist);
	zui_list_destroy(owned_list);
	zui_sublist_destroy(sublist);
	zui_list_destroy(list);
}

ZTEST(zui_component, test_editors_and_text_view)
{
	struct callback_state state = {0};
	char buffer[256] = "init";
	uint8_t bytes[12] = {0x01, 0x02, 0x03, 0x04};
	const uint8_t next_bytes[12] = {0xaa, 0xbb, 0xcc, 0xdd, 0, 1, 2, 3, 4, 5, 6, 7};
	const uint8_t zeros[12] = {0};
	const char *view_text = "one\n"
				"two\n"
				"three wraps because this line is deliberately longer than the view\n"
				"four\n"
				"five\n"
				"six\n"
				"seven\n"
				"eight\n"
				"nine\n"
				"ten\n"
				"eleven";
	struct zui_text_editor *text = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = "Text",
		.buffer = buffer,
		.buffer_size = sizeof(buffer),
		.min_length = 2,
		.validate = text_validate,
		.changed = text_changed,
		.submitted = text_submitted,
		.user_data = &state,
	});
	struct zui_number_editor *number = zui_number_editor_create(&(struct zui_number_editor_config){
		.title = "Number",
		.value = 5,
		.min_value = -10,
		.max_value = 10,
		.submitted = value_submitted,
		.user_data = &state,
	});
	struct zui_hex_editor *hex = zui_hex_editor_create(&(struct zui_hex_editor_config){
		.title = "Hex",
		.bytes = bytes,
		.byte_count = ARRAY_SIZE(bytes),
		.payload_size = 4U,
		.submitted = hex_submitted,
		.user_data = &state,
	});
	struct zui_text_view *view = zui_text_view_create(&(struct zui_text_view_config){
		.title = "View",
		.text = view_text,
		.font = ZUI_FONT_SECONDARY,
		.mode = ZUI_TEXT_VIEW_MODE_TEXT,
	});

	zassert_not_null(text);
	zassert_ok(zui_text_editor_set_text(text, "ok"));
	zassert_ok(zui_text_editor_submit(text));
	zassert_str_equal(state.text, "ok");
	zassert_ok(zui_text_editor_set_text(text, "bad"));
	zassert_equal(zui_text_editor_submit(text), -EINVAL);
	zassert_equal(zui_text_editor_validate_file(text, NULL, NULL, "bad"), -EALREADY);
	assert_screen_draws(zui_text_editor_get_screen(text));

	zassert_ok(zui_text_editor_set_text(text, ""));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "Q");
	zassert_equal(zui_text_editor_submit(text), -EINVAL);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "Qq");
	zassert_ok(zui_text_editor_submit(text));

	zassert_ok(zui_text_editor_set_text(text, ""));
	for (size_t i = 0U; i < sizeof(buffer) - 1U; i++) {
		zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text),
						      &select_click),
			      1);
	}
	zassert_equal(strlen(zui_text_editor_text(text)), sizeof(buffer) - 1U);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(strlen(zui_text_editor_text(text)), sizeof(buffer) - 1U);
	zassert_ok(zui_text_editor_submit(text));

	zassert_ok(zui_text_editor_set_text(text, "abcd"));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &long_left), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "qabcd");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &long_right), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "qabcdq");

	zassert_ok(zui_text_editor_set_text(text, ""));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "Q");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "Qq");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &long_select), 1);
	zassert_str_equal(zui_text_editor_text(text), "QqQ");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &left_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &left_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "QqqQ");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &right_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "Qq.qQ");

	zassert_ok(zui_text_editor_update(text, &(struct zui_text_editor_config){
		.title = "Text",
		.buffer = buffer,
		.buffer_size = sizeof(buffer),
		.min_length = 2,
		.validate = text_validate,
		.changed = text_changed,
		.submitted = text_submitted,
		.user_data = &state,
	}));
	zassert_ok(zui_text_editor_set_text(text, ""));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 0);
	zassert_str_equal(zui_text_editor_text(text), "");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 0);
	zassert_str_equal(zui_text_editor_text(text), "");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 0);
	zassert_str_equal(zui_text_editor_text(text), "");
#if defined(CONFIG_ZUI_TEXT_EDITOR_KEYPAD_MULTI_TAP) && defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "A");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "B");
	k_sleep(K_MSEC(CONFIG_ZUI_KEYPAD_MULTI_TAP_TIMEOUT_MS + 10));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "Ba");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_3), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_3), 1);
	zassert_str_equal(zui_text_editor_text(text), "Bae");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_star), 1);
	zassert_str_equal(zui_text_editor_text(text), "Ba");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot_long),
		      1);
	zassert_str_equal(state.text, "Ba");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "Baa");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_4), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &right_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2in");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &left_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_star), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_star), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_3), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2good");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &right_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &left_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2good");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_1), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_0), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_1), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home .");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_1), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ,");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_9), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ,");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_9), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ,");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 0);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ,");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &right_click), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &select_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "BaaA2home ,z");

	char full[sizeof(buffer)];

	memset(full, 'x', sizeof(full) - 1U);
	full[sizeof(full) - 1U] = '\0';
	zassert_ok(zui_text_editor_set_text(text, full));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 1);
	zassert_equal(strlen(zui_text_editor_text(text)), sizeof(buffer) - 1U);
#elif defined(CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE)
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 1);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_1), 1);
	zassert_str_equal(zui_text_editor_text(text), ".");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_1), 1);
	zassert_str_equal(zui_text_editor_text(text), ",");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_star), 1);
	zassert_str_equal(zui_text_editor_text(text), "");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_4), 1);
	zassert_str_equal(zui_text_editor_text(text), "");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "Go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_6), 1);
	zassert_str_equal(zui_text_editor_text(text), "Go");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &right_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "In");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_0), 1);
	zassert_str_equal(zui_text_editor_text(text), "In ");
#else
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_dot), 0);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_2), 0);
	zassert_str_equal(zui_text_editor_text(text), "");
#endif

	zassert_ok(zui_text_editor_set_text(text, "abc"));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &back_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "ab");
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &long_back), 0);
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &keypad_star_long), 0);
	zassert_str_equal(zui_text_editor_text(text), "ab");
	zassert_ok(zui_text_editor_set_text(text, ""));
	zassert_equal(zui_screen_submit_input(zui_text_editor_get_screen(text), &back_click), 1);
	zassert_str_equal(zui_text_editor_text(text), "");

	zassert_not_null(number);
	zassert_ok(zui_number_editor_step(number, 20));
	zassert_equal(zui_number_editor_value(number), 10);
	zassert_ok(zui_number_editor_submit(number));
	zassert_equal(state.value, 10);
	zassert_ok(zui_number_editor_set_value(number, 5));
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &down_click),
		      1);
	for (uint32_t i = 0U; i < 5U; i++) {
		zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number),
						      &right_click),
			      1);
	}
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &select_click),
		      1);
	zassert_equal(zui_number_editor_value(number), -5);
	zassert_ok(zui_number_editor_submit(number));
	zassert_equal(state.value, -5);
	assert_screen_draws(zui_number_editor_get_screen(number));

	zassert_ok(zui_number_editor_set_value(number, 0));
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_4), 1);
	zassert_equal(zui_number_editor_value(number), 4);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_star),
		      1);
	zassert_equal(zui_number_editor_value(number), 4);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_2), 1);
	zassert_equal(zui_number_editor_value(number), 2);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_dot),
		      1);
	zassert_equal(state.value, 2);

	zassert_not_null(hex);
	zassert_ok(zui_hex_editor_set_data(hex, next_bytes, ARRAY_SIZE(next_bytes)));
	zassert_mem_equal(zui_hex_editor_data(hex), next_bytes, ARRAY_SIZE(next_bytes));
	zassert_equal(zui_hex_editor_payload_size(hex), ARRAY_SIZE(next_bytes));
	zassert_ok(zui_hex_editor_submit(hex));
	zassert_mem_equal(state.bytes, next_bytes, ARRAY_SIZE(next_bytes));
	zassert_equal(state.byte_count, ARRAY_SIZE(next_bytes));
	zassert_ok(zui_hex_editor_set_data(hex, zeros, ARRAY_SIZE(zeros)));
	zassert_equal(zui_hex_editor_payload_size(hex), ARRAY_SIZE(zeros));
	zassert_ok(zui_hex_editor_submit(hex));
	zassert_mem_equal(state.bytes, zeros, ARRAY_SIZE(zeros));
	zassert_equal(state.byte_count, ARRAY_SIZE(zeros));
	zassert_ok(zui_hex_editor_update(hex, &(struct zui_hex_editor_config){
		.title = "Hex",
		.bytes = bytes,
		.byte_count = ARRAY_SIZE(bytes),
		.payload_size = 0U,
		.submitted = hex_submitted,
		.user_data = &state,
	}));
	zassert_equal(zui_hex_editor_payload_size(hex), 0U);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &select_click), 1);
	zassert_equal(zui_hex_editor_payload_size(hex), 1U);
	zassert_equal(zui_hex_editor_data(hex)[0], 0x00);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &right_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &right_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &select_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[0], 0x0a);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[1], 0x10);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &right_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &down_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[1], 0x1f);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &back_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[1], 0x1f);
	zassert_equal(zui_hex_editor_payload_size(hex), 2U);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &long_back), 0);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &keypad_star_long), 0);
	zassert_equal(zui_hex_editor_data(hex)[1], 0x1f);
	zassert_equal(zui_hex_editor_payload_size(hex), 2U);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &back_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[1], 0x00);
	zassert_equal(zui_hex_editor_payload_size(hex), 1U);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &long_right), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[ARRAY_SIZE(bytes) - 1U], 0x01);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &long_left), 1);
	zassert_equal(zui_screen_submit_input(zui_hex_editor_get_screen(hex), &up_click), 1);
	zassert_equal(zui_hex_editor_data(hex)[0], 0x1a);
	assert_screen_draws(zui_hex_editor_get_screen(hex));

	zassert_not_null(view);
	zassert_ok(zui_text_view_scroll_by(view, 2));
	zassert_equal(zui_text_view_scroll(view), 2U);
	zassert_ok(zui_text_view_scroll_by(view, -10));
	zassert_equal(zui_text_view_scroll(view), 0U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &right_click), 1);
	zassert_equal(zui_text_view_scroll(view), 1U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &right_click), 1);
	zassert_equal(zui_text_view_scroll(view), 2U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &left_click), 1);
	zassert_equal(zui_text_view_scroll(view), 1U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &left_click), 1);
	zassert_equal(zui_text_view_scroll(view), 0U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &down_click), 1);
	zassert_equal(zui_text_view_scroll(view), 1U);
	zassert_equal(zui_screen_submit_input(zui_text_view_get_screen(view), &up_click), 1);
	zassert_equal(zui_text_view_scroll(view), 0U);
	assert_screen_draws(zui_text_view_get_screen(view));

	zui_text_view_destroy(view);
	zui_hex_editor_destroy(hex);
	zui_number_editor_destroy(number);
	zui_text_editor_destroy(text);
}

ZTEST(zui_component, test_number_editor_input_contract)
{
	struct callback_state state = {0};
	struct zui_number_editor *number =
		zui_number_editor_create(&(struct zui_number_editor_config){
			.title = "Unsigned Number",
			.value = 123,
			.min_value = 0,
			.max_value = 1000,
			.max_digits = 4U,
			.unsigned_only = true,
			.submitted = value_submitted,
			.user_data = &state,
		});

	zassert_not_null(number);

	for (uint32_t i = 0U; i < 5U; i++) {
		zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number),
						      &right_click),
			      1);
	}
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &select_click),
		      1);
	zassert_equal(zui_number_editor_value(number), 12);
	zassert_equal(state.count, 0U);

	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &back_click), 1);
	zassert_equal(zui_number_editor_value(number), 1);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_2), 1);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_3), 1);
	zassert_equal(zui_number_editor_value(number), 123);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &keypad_star),
		      1);
	zassert_equal(zui_number_editor_value(number), 12);

	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &long_back), 0);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number),
					      &keypad_star_long),
		      0);
	zassert_equal(zui_number_editor_value(number), 12);

	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &long_select), 1);
	zassert_equal(state.value, 12);
	zassert_equal(state.count, 1U);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number),
					      &keypad_dot_long),
		      1);
	zassert_equal(state.value, 12);
	zassert_equal(state.count, 2U);

	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &down_click), 1);
	zassert_equal(zui_screen_submit_input(zui_number_editor_get_screen(number), &select_click),
		      1);
	zassert_equal(state.value, 12);
	zassert_equal(state.count, 3U);

	zui_number_editor_destroy(number);
}

ZTEST(zui_component, test_file_modal_progress)
{
	struct callback_state state = {0};
	char long_path[180];
	struct zui_file_picker *selector = zui_file_picker_create(&(struct zui_file_picker_config){
		.title = "Files",
		.base_path = "/",
		.extension = "txt,bin",
		.skip_assets = true,
		.hide_dot_files = true,
		.hide_extension = true,
		.load = file_load,
		.filter = file_filter,
		.probe = file_probe,
		.selected = file_picked,
		.long_selected = file_picked,
		.user_data = &state,
	});
	struct zui_modal *modal = zui_modal_create(&(struct zui_modal_config){
		.title = "Question",
		.text = "Continue?",
		.up_button = "Up",
		.left_button = "No",
		.center_button = "OK",
		.right_button = "Yes",
		.down_button = "Down",
		.result = modal_result_cb,
		.user_data = &state,
	});
	struct zui_progress *progress = zui_progress_create(&(struct zui_progress_config){
		.text = "Load",
		.value = 0.25f,
	});

	memset(long_path, 'a', sizeof(long_path));
	long_path[0] = '/';
	long_path[sizeof(long_path) - 1U] = '\0';

	zassert_not_null(selector);
	zassert_str_equal(zui_file_picker_path(selector), "/");
	zassert_equal(zui_file_picker_open(selector, "/apps/../note.txt"), -EINVAL);
	zassert_str_equal(zui_file_picker_path(selector), "/");
	zassert_equal(zui_file_picker_open(selector, long_path), -ENAMETOOLONG);
	zassert_str_equal(zui_file_picker_path(selector), "/");
	zassert_ok(zui_file_picker_open(selector, "//apps//"));
	zassert_str_equal(zui_file_picker_path(selector), "/apps");
	zassert_ok(zui_file_picker_open(selector, "/apps/app.txt"));
	zassert_str_equal(zui_file_picker_path(selector), "/apps");
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(state.text, "/apps/app.txt");
	state.count = 0U;
	state.text[0] = '\0';
	zassert_ok(zui_file_picker_open(selector, "/"));
	zassert_ok(zui_file_picker_choose(selector, &long_select));
	zassert_str_equal(zui_file_picker_path(selector), "/");
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_file_picker_get_screen(selector), &down_press),
		      0);
	zassert_equal(zui_screen_submit_input(zui_file_picker_get_screen(selector), &select_press),
		      0);
	zassert_equal(state.count, 0U);
	zassert_ok(zui_file_picker_move(selector, ZUI_MOVE_NEXT));
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(state.text, "/note.txt");
	assert_screen_draws(zui_file_picker_get_screen(selector));

	zassert_ok(zui_file_picker_open(selector, "/"));
	for (uint32_t i = 0U; i < 4U; i++) {
		const struct zui_input_event click_down = {
			.sequence = 10U + i,
			.code = ZUI_INPUT_CODE_DOWN,
			.action = ZUI_INPUT_ACTION_CLICK,
		};

		zassert_equal(zui_screen_submit_input(zui_file_picker_get_screen(selector),
						      &click_down),
			      1);
	}
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(state.text, "/gamma.txt");

	zassert_ok(zui_file_picker_open(selector, "/"));
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(zui_file_picker_path(selector), "/apps");
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(state.text, "/apps/app.txt");
	zassert_ok(zui_file_picker_open(selector, "/apps"));
	zassert_equal(zui_screen_submit_input(zui_file_picker_get_screen(selector), &back_click),
		      1);
	zassert_str_equal(zui_file_picker_path(selector), "/");
	zassert_ok(zui_file_picker_choose(selector, &select_click));
	zassert_str_equal(zui_file_picker_path(selector), "/apps");

	zassert_not_null(modal);
	zassert_equal(zui_screen_submit_input(zui_modal_get_screen(modal), &select_click), 1);
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_CENTER);
	zassert_equal(zui_screen_submit_input(zui_modal_get_screen(modal), &left_click), 1);
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_LEFT);
	zassert_equal(zui_screen_submit_input(zui_modal_get_screen(modal), &right_click), 1);
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_RIGHT);
	zassert_equal(zui_screen_submit_input(zui_modal_get_screen(modal), &up_click), 1);
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_UP);
	zassert_equal(zui_screen_submit_input(zui_modal_get_screen(modal), &down_click), 1);
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_DOWN);
	zassert_ok(zui_modal_submit(modal, ZUI_MODAL_RESULT_DOWN, &select_click));
	zassert_equal(state.modal_result, ZUI_MODAL_RESULT_DOWN);
	assert_screen_draws(zui_modal_get_screen(modal));

	zassert_not_null(progress);
	zassert_true(zui_progress_value(progress) > 0.24f && zui_progress_value(progress) < 0.26f);
	zassert_ok(zui_progress_set(progress, 2.0f, "Done"));
	zassert_true(zui_progress_value(progress) > 0.99f);
	zassert_ok(zui_screen_enter(zui_progress_get_screen(progress)));
	zassert_equal(zui_screen_submit_input(zui_progress_get_screen(progress), &select_click), 1);
	assert_screen_draws(zui_progress_get_screen(progress));
	zassert_ok(zui_screen_exit(zui_progress_get_screen(progress)));

	zui_progress_destroy(progress);
	zui_modal_destroy(modal);
	zui_file_picker_destroy(selector);
}

ZTEST(zui_component, test_actions_composite_and_blank)
{
	struct callback_state state = {0};
	const struct zui_action_item action_items[] = {
		{.id = 100, .label = "Open"},
		{.id = 200, .label = "Edit", .is_control = true},
	};
	struct zui_actions *actions = zui_actions_create(&(struct zui_actions_config){
		.title = "Actions",
		.items = action_items,
		.item_count = ARRAY_SIZE(action_items),
		.selected = action_selected,
		.user_data = &state,
	});
	struct zui_composite *composite = zui_composite_create(&(struct zui_composite_config){
		.draw = composite_draw,
		.input = composite_input,
		.user_data = &state,
	});
	struct zui_element *element = zui_element_create();
	struct zui_blank *blank = zui_blank_create();

	zassert_not_null(actions);
	zassert_ok(zui_actions_move(actions, ZUI_MOVE_NEXT));
	zassert_equal(zui_actions_selected(actions), 1U);
	zassert_equal(zui_screen_submit_input(zui_actions_get_screen(actions), &down_press), 0);
	zassert_equal(zui_actions_selected(actions), 1U);
	zassert_equal(zui_screen_submit_input(zui_actions_get_screen(actions), &select_press), 1);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_actions_get_screen(actions), &select_release),
		      1);
	zassert_equal(state.count, 0U);
	zassert_equal(zui_screen_submit_input(zui_actions_get_screen(actions), &select_click), 1);
	zassert_equal(state.id, 200U);
	zassert_equal(state.count, 1U);
	assert_screen_draws(zui_actions_get_screen(actions));

	zassert_not_null(composite);
	assert_screen_draws(zui_composite_get_screen(composite));
	zassert_true(state.draw_called);
	zassert_equal(zui_screen_submit_input(zui_composite_get_screen(composite), &long_select),
		      1);
	zassert_true(state.input_called);
	zassert_ok(zui_composite_request_redraw(composite));

	zassert_not_null(element);
	zassert_ok(zui_element_add_string(element, (struct zui_point){.x = 2, .y = 10},
						 ZUI_ALIGN_LEFT, ZUI_ALIGN_BOTTOM,
						 ZUI_FONT_SECONDARY, "Element"));
	zassert_ok(zui_element_add_multiline_string(
		element, &(struct zui_rect){.x = 2, .y = 12, .width = 80, .height = 24},
		ZUI_ALIGN_LEFT, ZUI_ALIGN_TOP, ZUI_FONT_SECONDARY, "one\ntwo"));
	zassert_ok(zui_element_add_text_box(
		element, &(struct zui_rect){.x = 2, .y = 36, .width = 56, .height = 16},
		ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, "boxed text that clips", true));
	zassert_ok(zui_element_add_text_scroll(
		element, &(struct zui_rect){.x = 62, .y = 12, .width = 52, .height = 30},
		ZUI_FONT_SECONDARY, "\ecCentered\nLine two wraps here\n\erRight"));
	zassert_ok(zui_element_add_rect(
		element, &(struct zui_rect){.x = 90, .y = 12, .width = 20, .height = 12}, 1U,
		false));
	zassert_ok(zui_element_add_line(element, (struct zui_point){.x = 0, .y = 0},
					       (struct zui_point){.x = 10, .y = 10}));
	zassert_ok(zui_element_add_button(element, ZUI_ELEMENT_BUTTON_CENTER, "OK",
					  element_button, &state));
	assert_screen_draws(zui_element_get_screen(element));
	zassert_equal(
		zui_screen_submit_input(zui_element_get_screen(element), &select_press), 0);
	zassert_not_equal(state.id, ZUI_ELEMENT_BUTTON_CENTER);
	zassert_equal(
		zui_screen_submit_input(zui_element_get_screen(element), &select_click), 1);
	zassert_equal(state.id, ZUI_ELEMENT_BUTTON_CENTER);
	zassert_equal(zui_screen_submit_input(zui_element_get_screen(element), &down_click),
		      1);

	zassert_not_null(blank);
	assert_screen_draws(zui_blank_get_screen(blank));

	zui_blank_destroy(blank);
	zui_element_destroy(element);
	zui_composite_destroy(composite);
	zui_actions_destroy(actions);
}

ZTEST_SUITE(zui_component, NULL, NULL, NULL, NULL, NULL);
