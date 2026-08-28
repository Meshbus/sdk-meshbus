// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SAMPLE_PD_NODE      DT_NODELABEL(peripheral_power)
#define SAMPLE_BUTTONS_NODE DT_NODELABEL(buttons)
#define SAMPLE_ENCODER_NODE DT_NODELABEL(encoder)
#define SAMPLE_LONG_PRESS_TIMEOUT K_MSEC(650)
#define SAMPLE_SHELL_SCREEN_ID 1U
#define SAMPLE_MAX_PAGES 15U

#if DT_NODE_HAS_STATUS(SAMPLE_PD_NODE, okay) && defined(CONFIG_PM_DEVICE_RUNTIME)
#define SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY 80

static int zui_component_sample_power_bootstrap_init(void)
{
	const struct device *pd = DEVICE_DT_GET(SAMPLE_PD_NODE);
	int rc;

	if (!device_is_ready(pd)) {
		return -ENODEV;
	}

	rc = pm_device_runtime_get(pd);
	if (rc < 0 && rc != -ENOTSUP && rc != -ENOSYS) {
		return rc;
	}

	return 0;
}

SYS_INIT(zui_component_sample_power_bootstrap_init, POST_KERNEL, SAMPLE_PD_BOOTSTRAP_INIT_PRIORITY);
#endif

struct sample_page {
	uint32_t id;
	const char *name;
	struct zui_screen *screen;
};

struct sample_state {
	struct k_mutex lock;
	struct k_work work;
	struct k_work redraw_work;
	struct k_work_delayable long_press_work;
	struct zui_draw_ctx *draw;
	struct zui_sublist *root_menu;
	struct zui_progress *progress;
	struct zui_screen *list_screen;
	struct sample_page pages[SAMPLE_MAX_PAGES];
	struct zui_list_item menu_items[SAMPLE_MAX_PAGES];
	size_t page_count;
	size_t page;
	struct zui_input_event event;
	struct zui_input_event pending_button;
	char progress_text[16];
	uint8_t progress_percent;
	bool ready;
	bool showing_root;
	bool pending_button_active;
	bool pending_button_long_fired;
};

static struct sample_state sample;
static char text_buffer[256] = "FoBE";
static uint8_t hex_bytes[] = {
	0x12, 0x34, 0xab, 0xcd, 0x00, 0x01, 0x02, 0x03,
	0x10, 0x20, 0x30, 0x40,
};
static int64_t submitted_value;
static char callback_line[32];
static struct zui_icon_anim *list_anim;

static struct zui_list_item list_items[] = {
	{.id = 1, .label = "Radio", .detail = "ready"},
	{.id = 2, .label = "GNSS", .detail = "off"},
	{.id = 3, .label = "Settings", .detail = "open"},
	{.id = 4, .label = "Files", .detail = "3"},
};

static struct zui_list_item submenu_items[] = {
	{.id = 101, .label = "Bluetooth", .detail = "on"},
	{.id = 102, .label = "Display", .detail = "auto"},
	{.id = 103, .label = "Clock", .detail = "12:48"},
	{.id = 104, .label = "Radio packet monitor", .detail = "rx"},
	{.id = 105, .label = "MeshCore channel settings", .detail = "cfg"},
	{.id = 106, .label = "System", .detail = "info"},
};

static const char *const form_options[] = {"off", "on", "auto"};
static const struct zui_form_item form_items[] = {
	{.id = 1,
	 .label = "Mode",
	 .options = form_options,
	 .option_count = ARRAY_SIZE(form_options)},
	{.id = 2, .label = "Name", .value_text = "tracker"},
	{.id = 3,
	 .label = "Power",
	 .options = form_options,
	 .option_count = ARRAY_SIZE(form_options),
	 .option_index = 1},
};

static struct zui_action_item action_items[] = {
	{.id = 10, .label = "Open"},
	{.id = 20, .label = "Edit", .is_control = true},
	{.id = 30, .label = "Delete"},
	{.id = 40, .label = "Save", .is_control = true},
};

static void sample_resume_input_device(const struct device *dev, const char *name)
{
	int rc;

	if (!device_is_ready(dev)) {
		printk("%s input device is not ready\n", name);
		return;
	}

	rc = pm_device_runtime_get(dev);
	printk("%s input runtime get ret=%d\n", name, rc);
}

static void sample_resume_input_devices(void)
{
#if DT_NODE_HAS_STATUS(SAMPLE_BUTTONS_NODE, okay)
	sample_resume_input_device(DEVICE_DT_GET(SAMPLE_BUTTONS_NODE), "buttons");
#endif
#if DT_NODE_HAS_STATUS(SAMPLE_ENCODER_NODE, okay)
	sample_resume_input_device(DEVICE_DT_GET(SAMPLE_ENCODER_NODE), "encoder");
#endif
}

static void sample_note(const char *kind, uint32_t id)
{
	(void)snprintk(callback_line, sizeof(callback_line), "%s %u", kind, id);
	printk("callback %s\n", callback_line);
}

static void sample_list_selected(struct zui_list *list, uint32_t id, size_t index,
				 const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	sample_note("list", id);
}

static void sample_submenu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				    const struct zui_input_event *event, void *user_data)
{
	const char *action = event != NULL ? zui_input_action_name(event->action) : "?";

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "submenu %u %s", id, action);
	printk("callback %s\n", callback_line);
}

static void sample_form_changed(struct zui_form *form, uint32_t id, size_t option_index,
				void *user_data)
{
	ARG_UNUSED(form);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "form %u=%u", id, option_index);
	printk("callback %s\n", callback_line);
}

static void sample_form_activated(struct zui_form *form, uint32_t id,
				  const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(form);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	sample_note("form", id);
}

static bool sample_text_validate(const char *text, char *error, size_t error_size, void *user_data)
{
	ARG_UNUSED(error);
	ARG_UNUSED(error_size);
	ARG_UNUSED(user_data);

	return text[0] != '\0';
}

static void sample_text_submitted(struct zui_text_editor *editor, const char *text,
				  void *user_data)
{
	ARG_UNUSED(editor);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "text %s", text);
	printk("callback %s\n", callback_line);
}

static void sample_value_submitted(struct zui_number_editor *editor, int64_t value,
				   void *user_data)
{
	ARG_UNUSED(editor);
	ARG_UNUSED(user_data);

	submitted_value = value;
	printk("callback value %lld\n", (long long)value);
}

static void sample_hex_submitted(struct zui_hex_editor *editor, const uint8_t *bytes,
				 size_t byte_count, void *user_data)
{
	ARG_UNUSED(editor);
	ARG_UNUSED(bytes);
	ARG_UNUSED(byte_count);
	ARG_UNUSED(user_data);

	printk("callback hex submit\n");
}

static int sample_file_load(struct zui_file_picker *picker, const char *path,
			    struct zui_file_picker_entry *entries, size_t capacity, size_t *count,
			    void *user_data)
{
	ARG_UNUSED(picker);
	ARG_UNUSED(user_data);

	if (capacity < 4U) {
		return -ENOMEM;
	}

	if (strcmp(path, "/apps") == 0) {
		entries[0] = (struct zui_file_picker_entry){.path = "settings.txt"};
		entries[1] = (struct zui_file_picker_entry){.path = "assets", .is_dir = true};
		entries[2] = (struct zui_file_picker_entry){.path = "plugin.log"};
		entries[3] = (struct zui_file_picker_entry){.path = "firmware.bin"};
		*count = 4U;
		return 0;
	}

	entries[0] = (struct zui_file_picker_entry){.path = "apps", .is_dir = true};
	entries[1] = (struct zui_file_picker_entry){.path = "notes.txt"};
	entries[2] = (struct zui_file_picker_entry){.path = "radio.log"};
	entries[3] = (struct zui_file_picker_entry){.path = "firmware.bin"};
	*count = 4U;
	return 0;
}

static bool sample_file_filter(const char *path, void *user_data)
{
	ARG_UNUSED(user_data);

	return strcmp(path, "/firmware.bin") != 0;
}

static bool sample_file_probe(const char *path, char *name, size_t name_size,
			      struct zui_file_picker_probe_result *result, void *user_data)
{
	ARG_UNUSED(result);
	ARG_UNUSED(user_data);

	if (strcmp(path, "/radio.log") == 0) {
		(void)snprintk(name, name_size, "Radio Log");
	}

	return true;
}

static void sample_file_picked(struct zui_file_picker *picker, const char *path,
			       const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(picker);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "file %s", path);
	printk("callback %s\n", callback_line);
}

static void sample_modal_result(struct zui_modal *modal, enum zui_modal_result result,
				const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(modal);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	printk("callback modal result=%d\n", result);
}

static void sample_modal_run_emul(struct zui_modal *modal)
{
	static const struct {
		const char *name;
		struct zui_input_event event;
	} events[] = {
		{
			.name = "LEFT",
			.event = {
				.sequence = 100,
				.code = ZUI_INPUT_CODE_LEFT,
				.action = ZUI_INPUT_ACTION_CLICK,
			},
		},
		{
			.name = "RIGHT",
			.event = {
				.sequence = 101,
				.code = ZUI_INPUT_CODE_RIGHT,
				.action = ZUI_INPUT_ACTION_CLICK,
			},
		},
		{
			.name = "OK",
			.event = {
				.sequence = 102,
				.code = ZUI_INPUT_CODE_SELECT,
				.action = ZUI_INPUT_ACTION_CLICK,
			},
		},
		{
			.name = "UP",
			.event = {
				.sequence = 103,
				.code = ZUI_INPUT_CODE_UP,
				.action = ZUI_INPUT_ACTION_CLICK,
			},
		},
		{
			.name = "DOWN",
			.event = {
				.sequence = 104,
				.code = ZUI_INPUT_CODE_DOWN,
				.action = ZUI_INPUT_ACTION_CLICK,
			},
		},
	};
	struct zui_screen *screen;

	if (modal == NULL) {
		return;
	}

	screen = zui_modal_get_screen(modal);
	for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
		int rc = zui_screen_submit_input(screen, &events[i].event);

		printk("modal emul %s ret=%d\n", events[i].name, rc);
	}
}

static void sample_action_selected(struct zui_actions *actions, uint32_t id,
				   const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(actions);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	sample_note("action", id);
}

static void sample_composite_draw(struct zui_composite *composite, struct zui_draw_ctx *draw,
				  void *user_data)
{
	ARG_UNUSED(composite);
	ARG_UNUSED(user_data);

	zui_draw_reset(draw);
	zui_draw_set_font(draw, ZUI_FONT_SECONDARY);
	zui_draw_text(draw, (struct zui_point){.x = 2, .y = 8}, "Composite");
	zui_draw_round_rect(draw, &(struct zui_rect){.x = 8, .y = 18, .width = 112, .height = 36},
			    4);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 38}, ZUI_ALIGN_CENTER,
			      ZUI_ALIGN_CENTER,
			      callback_line[0] != '\0' ? callback_line : "custom");
}

static bool sample_composite_input(struct zui_composite *composite,
				   const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(composite);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "in %s",
		       zui_input_code_name(event->code));
	return true;
}

static void sample_element_button(enum zui_element_button button, struct zui_element *element,
				 const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(element);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	(void)snprintk(callback_line, sizeof(callback_line), "element button %d", button);
	printk("callback %s\n", callback_line);
}

static struct zui_screen *sample_current_screen(void)
{
	if (sample.showing_root) {
		return sample.root_menu == NULL ? NULL : zui_sublist_get_screen(sample.root_menu);
	}

	if (sample.page_count == 0U) {
		return NULL;
	}

	return sample.pages[sample.page].screen;
}

static void sample_add_page(const char *name, struct zui_screen *screen)
{
	size_t index;

	if (sample.page_count >= ARRAY_SIZE(sample.pages)) {
		printk("too many component pages, dropping %s\n", name);
		return;
	}

	index = sample.page_count++;
	sample.pages[index] = (struct sample_page){
		.id = index + 1U,
		.name = name,
		.screen = screen,
	};
	sample.menu_items[index] = (struct zui_list_item){
		.id = index + 1U,
		.label = name,
	};
}

static void sample_enter_root(void)
{
	struct zui_screen *root_screen;

	if (sample.root_menu == NULL || sample.showing_root) {
		return;
	}

	root_screen = zui_sublist_get_screen(sample.root_menu);
	(void)zui_screen_exit(sample.pages[sample.page].screen);
	sample.showing_root = true;
	(void)zui_screen_enter(root_screen);
	printk("component menu\n");
}

static void sample_enter_page(size_t page)
{
	struct zui_screen *root_screen;

	if (page >= sample.page_count) {
		return;
	}

	if (sample.showing_root) {
		root_screen = zui_sublist_get_screen(sample.root_menu);
		(void)zui_screen_exit(root_screen);
	} else {
		(void)zui_screen_exit(sample.pages[sample.page].screen);
	}

	sample.page = page;
	sample.showing_root = false;
	(void)zui_screen_enter(sample.pages[sample.page].screen);
	printk("component -> %s\n", sample.pages[sample.page].name);
}

static void sample_root_selected(struct zui_sublist *list, uint32_t id, size_t index,
				 const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(list);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	if (index < sample.page_count && sample.pages[index].id == id) {
		sample_enter_page(index);
		return;
	}

	for (size_t i = 0; i < sample.page_count; i++) {
		if (sample.pages[i].id == id) {
			sample_enter_page(i);
			return;
		}
	}
}

static void sample_draw_current(void)
{
	struct zui_screen *screen;
	int rc;

	if (sample.draw == NULL || sample.page_count == 0U) {
		return;
	}

	screen = sample_current_screen();
	if (screen == NULL) {
		return;
	}

	rc = zui_screen_draw(screen, sample.draw);
	if (!sample.showing_root) {
		zui_draw_set_font(sample.draw, ZUI_FONT_SECONDARY);
		zui_draw_text_aligned(sample.draw, (struct zui_point){.x = 126, .y = 8},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM,
				      sample.pages[sample.page].name);
		zui_draw_text_aligned(sample.draw, (struct zui_point){.x = 126, .y = 63},
				      ZUI_ALIGN_RIGHT, ZUI_ALIGN_BOTTOM, "Back:menu");
	}
	(void)zui_draw_present(sample.draw);

	printk("draw %s ret=%d submitted=%lld\n",
	       sample.showing_root ? "menu" : sample.pages[sample.page].name, rc,
	       (long long)submitted_value);
}

static void sample_work_handler(struct k_work *work)
{
	struct zui_input_event event;
	struct zui_screen *screen;

	ARG_UNUSED(work);

	k_mutex_lock(&sample.lock, K_FOREVER);
	event = sample.event;
	screen = sample_current_screen();
	if (!sample.showing_root && event.code == ZUI_INPUT_CODE_BACK &&
	    event.action == ZUI_INPUT_ACTION_CLICK) {
		sample_enter_root();
	} else if (screen != NULL) {
		(void)zui_screen_submit_input(screen, &event);
	} else {
		printk("no active component screen\n");
	}
	sample_draw_current();
	k_mutex_unlock(&sample.lock);
}

static void sample_submit_event(const struct zui_input_event *event)
{
	if (event == NULL) {
		return;
	}

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample.event = *event;
	k_mutex_unlock(&sample.lock);
	(void)k_work_submit(&sample.work);
}

static bool sample_supports_long_press(enum zui_input_code code)
{
	return code == ZUI_INPUT_CODE_SELECT || code == ZUI_INPUT_CODE_LEFT ||
	       code == ZUI_INPUT_CODE_RIGHT;
}

static void sample_long_press_work_handler(struct k_work *work)
{
	struct zui_input_event event;
	bool submit = false;

	ARG_UNUSED(work);

	k_mutex_lock(&sample.lock, K_FOREVER);
	if (sample.pending_button_active && sample_supports_long_press(sample.pending_button.code)) {
		sample.pending_button_long_fired = true;
		event = sample.pending_button;
		event.action = ZUI_INPUT_ACTION_LONG_PRESS;
		submit = true;
	}
	k_mutex_unlock(&sample.lock);

	if (submit) {
		sample_submit_event(&event);
	}
}

static void sample_redraw_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample_draw_current();
	k_mutex_unlock(&sample.lock);
}

static void sample_draw_current_if_requested(void)
{
	struct zui_screen *screen;

	if (!sample.ready || sample.page_count == 0U) {
		return;
	}

	screen = sample_current_screen();
	if (screen == NULL || !zui_screen_redraw_is_requested(screen)) {
		return;
	}

	sample_draw_current();
}

static void sample_tick_progress(void)
{
	if (!sample.ready || sample.progress == NULL || sample.page_count == 0U ||
	    sample_current_screen() != zui_progress_get_screen(sample.progress)) {
		return;
	}

	sample.progress_percent = (sample.progress_percent + 2U) % 101U;
	(void)snprintk(sample.progress_text, sizeof(sample.progress_text), "%u%%",
		       sample.progress_percent);
	(void)zui_progress_set(sample.progress, (float)sample.progress_percent / 100.0f,
				sample.progress_text);
}

static void sample_handle_button_event(const struct zui_input_event *event)
{
	struct zui_input_event click_event;
	bool submit_click = false;

	if (event->action == ZUI_INPUT_ACTION_PRESS) {
		if (sample_supports_long_press(event->code)) {
			k_mutex_lock(&sample.lock, K_FOREVER);
			sample.pending_button = *event;
			sample.pending_button_active = true;
			sample.pending_button_long_fired = false;
			k_mutex_unlock(&sample.lock);
			(void)k_work_reschedule(&sample.long_press_work, SAMPLE_LONG_PRESS_TIMEOUT);
			return;
		}

		click_event = *event;
		click_event.action = ZUI_INPUT_ACTION_CLICK;
		sample_submit_event(&click_event);
		return;
	}

	if (event->action != ZUI_INPUT_ACTION_RELEASE) {
		sample_submit_event(event);
		return;
	}

	k_mutex_lock(&sample.lock, K_FOREVER);
	if (sample.pending_button_active && sample.pending_button.code == event->code) {
		click_event = sample.pending_button;
		click_event.action = ZUI_INPUT_ACTION_CLICK;
		click_event.value = event->value;
		submit_click = !sample.pending_button_long_fired;
		sample.pending_button_active = false;
		sample.pending_button_long_fired = false;
	}
	k_mutex_unlock(&sample.lock);
	(void)k_work_cancel_delayable(&sample.long_press_work);

	if (submit_click) {
		sample_submit_event(&click_event);
	}
}

static bool sample_shell_input(const struct zui_input_event *event, void *user_data)
{
	ARG_UNUSED(user_data);

	if (event == NULL || !sample.ready) {
		return false;
	}

	sample_handle_button_event(event);
	return true;
}

static const struct zui_screen_ops sample_shell_screen_ops = {
	.input = sample_shell_input,
};

static void sample_list_anim_updated(struct zui_icon_anim *anim, void *user_data)
{
	ARG_UNUSED(anim);
	ARG_UNUSED(user_data);

	if (!sample.ready || sample.page_count == 0U ||
	    sample_current_screen() != sample.list_screen) {
		return;
	}

	(void)k_work_submit(&sample.redraw_work);
}

static void sample_input_cb(struct input_event *evt, void *user_data)
{
	struct zui_input_event event = {0};

	ARG_UNUSED(user_data);

	if (!sample.ready || zui_input_from_zephyr(evt, &event) != 0) {
		return;
	}

	sample_handle_button_event(&event);
}

INPUT_CALLBACK_DEFINE(NULL, sample_input_cb, NULL);

int main(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display = NULL;
#endif
	struct zui_list *list;
	struct zui_sublist *submenu;
	struct zui_form *form;
	struct zui_text_editor *text_editor;
	struct zui_number_editor *number_editor;
	struct zui_hex_editor *hex_editor;
	struct zui_text_view *text_view;
	struct zui_file_picker *file_picker;
	struct zui_modal *modal;
	struct zui_host *host;
	struct zui_router *shell_router;
	struct zui_screen *shell_screen;
	struct zui_toast_config toast = {
		.title = "Toast",
		.text = "Operation complete",
		.timeout_ms = 1200U,
	};
	uint32_t toast_id;
	struct zui_progress *progress;
	struct zui_actions *actions;
	struct zui_composite *composite;
	struct zui_element *element;

	printk("ZUI component sample start\n");
	k_mutex_init(&sample.lock);
	k_work_init(&sample.work, sample_work_handler);
	k_work_init(&sample.redraw_work, sample_redraw_work_handler);
	k_work_init_delayable(&sample.long_press_work, sample_long_press_work_handler);
	sample_resume_input_devices();

	if (display == NULL || !device_is_ready(display)) {
		printk("display not ready\n");
		return 0;
	}

	sample.draw = zui_draw_ctx_create(display);
	if (sample.draw == NULL) {
		printk("draw ctx create failed\n");
		return 0;
	}

	list_items[0].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						       ZUI_ASSET_ICON_STATUS_UNKNOWN);
	list_items[1].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						       ZUI_ASSET_ICON_HASHMARK);
	list_items[2].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						       ZUI_ASSET_ICON_KEY_SAVE);
	list_items[3].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
						       ZUI_ASSET_ICON_FILE_DOCUMENT);
	submenu_items[0].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_HASHMARK);
	submenu_items[1].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_KEY_SAVE);
	submenu_items[2].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_FILE_DOCUMENT);
	submenu_items[3].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_STATUS_UNKNOWN);
	submenu_items[4].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_BUTTON_SELECT);
	submenu_items[5].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							  ZUI_ASSET_ICON_FILE_DIR);
	list_anim = zui_icon_anim_create(list_items[2].icon);
	if (list_anim != NULL) {
		zui_icon_anim_set_frame_rate(list_anim, 12U);
		zui_icon_anim_set_update_callback(list_anim, sample_list_anim_updated, NULL);
		list_items[2].icon_anim = list_anim;
		printk("list anim frames=%u fps=10\n", zui_icon_frame_count(list_items[2].icon));
	}
	action_items[0].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							 ZUI_ASSET_ICON_FILE_DOCUMENT);
	action_items[1].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							 ZUI_ASSET_ICON_KEY_SAVE);
	action_items[2].icon = zui_asset_pack_icon_by_id(zui_asset_pack_default(),
							 ZUI_ASSET_ICON_KEY_BACKSPACE);
	action_items[3].icon =
		zui_asset_pack_icon_by_id(zui_asset_pack_default(), ZUI_ASSET_ICON_KEY_SAVE);

	list = zui_list_create(&(struct zui_list_config){
		.items = list_items,
		.item_count = ARRAY_SIZE(list_items),
		.selected = sample_list_selected,
	});
	if (list != NULL) {
		(void)zui_list_select(list, 2U);
		sample.list_screen = zui_list_get_screen(list);
	}
	submenu = zui_sublist_create(&(struct zui_sublist_config){
		.title = "Submenu",
		.items = submenu_items,
		.item_count = ARRAY_SIZE(submenu_items),
		.selected = sample_submenu_selected,
	});
	form = zui_form_create(&(struct zui_form_config){
		.title = "Form",
		.items = form_items,
		.item_count = ARRAY_SIZE(form_items),
		.changed = sample_form_changed,
		.activated = sample_form_activated,
	});
	text_editor = zui_text_editor_create(&(struct zui_text_editor_config){
		.title = "Text Editor",
		.buffer = text_buffer,
		.buffer_size = sizeof(text_buffer),
		.min_length = 2,
		.validate = sample_text_validate,
		.submitted = sample_text_submitted,
	});
	number_editor = zui_number_editor_create(&(struct zui_number_editor_config){
		.title = "Number",
		.value = 42,
		.min_value = -99,
		.max_value = 99,
		.submitted = sample_value_submitted,
	});
	hex_editor = zui_hex_editor_create(&(struct zui_hex_editor_config){
		.title = "Hex",
		.bytes = hex_bytes,
		.byte_count = ARRAY_SIZE(hex_bytes),
		.payload_size = ARRAY_SIZE(hex_bytes),
		.submitted = sample_hex_submitted,
	});
	text_view = zui_text_view_create(&(struct zui_text_view_config){
		.title = "Text View",
		.text = "Line 01: ZUI text view scroll test\n"
			"Line 02: short\n"
			"Line 03: this line is intentionally long and should wrap visually\n"
			"Line 04: wheel right moves down\n"
			"Line 05: wheel left moves up\n"
			"Line 06: up and down keys do the same\n"
			"Line 07: scrollbar should track the text\n"
			"Line 08: repeated clicks scroll steadily\n"
			"Line 09: focus remains inside the view\n"
			"Line 10: bottom content stays reachable\n"
			"Line 11: more wrapped content for validation\n"
			"Line 12: end",
		.font = ZUI_FONT_SECONDARY,
	});
	file_picker = zui_file_picker_create(&(struct zui_file_picker_config){
		.title = "Files",
		.base_path = "/",
		.extension = "txt,log,bin",
		.skip_assets = true,
		.hide_dot_files = true,
		.hide_extension = true,
		.load = sample_file_load,
		.filter = sample_file_filter,
		.probe = sample_file_probe,
		.selected = sample_file_picked,
		.long_selected = sample_file_picked,
	});
	modal = zui_modal_create(&(struct zui_modal_config){
		.title = "Modal",
		.text = "Continue with this operation?",
		.up_button = "Up",
		.left_button = "No",
		.center_button = "OK",
		.right_button = "Yes",
		.down_button = "Down",
		.result = sample_modal_result,
	});
	host = zui_host_create(NULL);
	shell_router = zui_router_create();
	shell_screen = zui_screen_create(&sample_shell_screen_ops, NULL);
	if (host != NULL && shell_router != NULL && shell_screen != NULL) {
		(void)zui_router_register_screen(shell_router, SAMPLE_SHELL_SCREEN_ID, shell_screen);
		(void)zui_router_switch(shell_router, SAMPLE_SHELL_SCREEN_ID);
		(void)zui_host_attach_router(host, ZUI_LAYER_DESKTOP, shell_router);
	}
	progress = zui_progress_create(&(struct zui_progress_config){
		.text = "Loading",
		.value = 0.62f,
	});
	sample.progress = progress;
	actions = zui_actions_create(&(struct zui_actions_config){
		.title = "Actions",
		.items = action_items,
		.item_count = ARRAY_SIZE(action_items),
		.selected = sample_action_selected,
	});
	composite = zui_composite_create(&(struct zui_composite_config){
		.draw = sample_composite_draw,
		.input = sample_composite_input,
	});
	element = zui_element_create();
	if (element != NULL) {
		(void)zui_element_add_string(element, (struct zui_point){.x = 2, .y = 9},
						    ZUI_ALIGN_LEFT, ZUI_ALIGN_BOTTOM,
						    ZUI_FONT_SECONDARY, "Component Element");
		(void)zui_element_add_text_box(
			element, &(struct zui_rect){.x = 4, .y = 12, .width = 74, .height = 16},
			ZUI_ALIGN_CENTER, ZUI_ALIGN_CENTER, "center aligned clipped text", true);
		(void)zui_element_add_text_scroll(
			element, &(struct zui_rect){.x = 4, .y = 30, .width = 82, .height = 22},
			ZUI_FONT_SECONDARY,
			"\ecCentered\n"
			"Line two wraps in the scroll area\n"
			"\erRight\n"
			"\e#Bold title");
		(void)zui_element_add_rect(
			element, &(struct zui_rect){.x = 94, .y = 14, .width = 24, .height = 18},
			2U, false);
		(void)zui_element_add_circle(element, (struct zui_point){.x = 106, .y = 23},
						    6U, false);
		(void)zui_element_add_button(element, ZUI_ELEMENT_BUTTON_CENTER, "OK",
					     sample_element_button, NULL);
	}

	if (list == NULL || submenu == NULL || form == NULL || text_editor == NULL ||
	    number_editor == NULL || hex_editor == NULL || text_view == NULL ||
	    file_picker == NULL || modal == NULL || host == NULL || shell_router == NULL ||
	    shell_screen == NULL || progress == NULL || actions == NULL ||
	    composite == NULL || element == NULL) {
		printk("component allocation failed\n");
		return 0;
	}

	sample_modal_run_emul(modal);
	toast_id = zui_toast_show(host, &toast);
	printk("toast id=%u visible=%d\n", toast_id, zui_toast_is_visible(host, toast_id));
	(void)zui_toast_dismiss(host, toast_id);

	sample_add_page("list", zui_list_get_screen(list));
	sample_add_page("submenu", zui_sublist_get_screen(submenu));
	sample_add_page("form", zui_form_get_screen(form));
	sample_add_page("text", zui_text_editor_get_screen(text_editor));
	sample_add_page("number", zui_number_editor_get_screen(number_editor));
	sample_add_page("hex", zui_hex_editor_get_screen(hex_editor));
	sample_add_page("view", zui_text_view_get_screen(text_view));
	sample_add_page("files", zui_file_picker_get_screen(file_picker));
	sample_add_page("modal", zui_modal_get_screen(modal));
	sample_add_page("progress", zui_progress_get_screen(progress));
	sample_add_page("actions", zui_actions_get_screen(actions));
	sample_add_page("custom", zui_composite_get_screen(composite));
	sample_add_page("element", zui_element_get_screen(element));

	sample.root_menu = zui_sublist_create(&(struct zui_sublist_config){
		.title = "Components",
		.items = sample.menu_items,
		.item_count = sample.page_count,
		.selected = sample_root_selected,
	});
	if (sample.root_menu == NULL) {
		printk("component menu allocation failed\n");
		return 0;
	}

	k_mutex_lock(&sample.lock, K_FOREVER);
	sample.ready = true;
	sample.showing_root = true;
	(void)zui_screen_enter(zui_sublist_get_screen(sample.root_menu));
	sample_draw_current();
	k_mutex_unlock(&sample.lock);

	printk("Select a component from the menu; Back returns to the menu\n");
	while (true) {
		k_mutex_lock(&sample.lock, K_FOREVER);
		sample_tick_progress();
		sample_draw_current_if_requested();
		k_mutex_unlock(&sample.lock);
		k_sleep(K_MSEC(25));
	}

	return 0;
}
