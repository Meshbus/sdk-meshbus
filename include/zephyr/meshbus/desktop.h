/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Copyright (c) 2026 FoBE Studio
 */

/**
 * @file
 * @brief Meshbus Desktop API
 *
 * This module exposes Meshbus desktop registration and event APIs.
 */

#ifndef ZEPHYR_INCLUDE_MESHBUS_DESKTOP_H_
#define ZEPHYR_INCLUDE_MESHBUS_DESKTOP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Screen IDs registered with the desktop UI router. */
enum meshbus_desktop_view_id {
	MESHBUS_DESKTOP_VIEW_DASHBOARD = 1,
	MESHBUS_DESKTOP_VIEW_MAIN_MENU = 2,
	MESHBUS_DESKTOP_VIEW_LAUNCHER = 3,
	MESHBUS_DESKTOP_VIEW_POWER_MENU = 4,
	MESHBUS_DESKTOP_VIEW_POWER_POPUP = 5,
};

/** @brief Desktop shell-level scene IDs. */
enum meshbus_desktop_scene_id {
	MESHBUS_DESKTOP_SCENE_HOME = 0,
	MESHBUS_DESKTOP_SCENE_MAIN_MENU = 1,
	MESHBUS_DESKTOP_SCENE_LAUNCHER = 2,
	MESHBUS_DESKTOP_SCENE_APP = 3,
	MESHBUS_DESKTOP_SCENE_POWER_MENU = 4,
	MESHBUS_DESKTOP_SCENE_COUNT,
};

/** @brief Desktop custom event IDs. */
enum meshbus_desktop_custom_event {
	MESHBUS_DESKTOP_EVENT_BASE = 0x1000,
	MESHBUS_DESKTOP_EVENT_OPEN_MAIN_MENU = MESHBUS_DESKTOP_EVENT_BASE + 1,
	MESHBUS_DESKTOP_EVENT_OPEN_LAUNCHER = MESHBUS_DESKTOP_EVENT_BASE + 2,
	MESHBUS_DESKTOP_EVENT_OPEN_SETTINGS = MESHBUS_DESKTOP_EVENT_BASE + 3,
	MESHBUS_DESKTOP_EVENT_APP_EXIT = MESHBUS_DESKTOP_EVENT_BASE + 4,
	MESHBUS_DESKTOP_EVENT_OPEN_POWER_MENU = MESHBUS_DESKTOP_EVENT_BASE + 5,
	MESHBUS_DESKTOP_EVENT_REFRESH_SLEEP_POLICY = MESHBUS_DESKTOP_EVENT_BASE + 0x7F,
};

/** @brief Opaque desktop app handle token. */
typedef uint32_t meshbus_desktop_app_handle_t;

#define MESHBUS_DESKTOP_APP_HANDLE_MAGIC       0xA0000000u
#define MESHBUS_DESKTOP_APP_HANDLE_VERSION     0x1u

#define MESHBUS_DESKTOP_APP_HANDLE_MAGIC_MASK  0xF0000000u
#define MESHBUS_DESKTOP_APP_HANDLE_VER_MASK    0x0F000000u
#define MESHBUS_DESKTOP_APP_HANDLE_EPOCH_MASK  0x00FF0000u
#define MESHBUS_DESKTOP_APP_HANDLE_INDEX_MASK  0x0000FFFFu

#define MESHBUS_DESKTOP_APP_HANDLE_VER_SHIFT   24u
#define MESHBUS_DESKTOP_APP_HANDLE_EPOCH_SHIFT 16u

#define MESHBUS_DESKTOP_APP_HANDLE_INVALID     ((meshbus_desktop_app_handle_t)0u)

#define MESHBUS_DESKTOP_APP_HANDLE_MAKE(_epoch_u8, _index_plus1_u16)                             \
	((meshbus_desktop_app_handle_t)(MESHBUS_DESKTOP_APP_HANDLE_MAGIC |                        \
		(((uint32_t)MESHBUS_DESKTOP_APP_HANDLE_VERSION & 0xFu)                              \
		 << MESHBUS_DESKTOP_APP_HANDLE_VER_SHIFT) |                                          \
		(((uint32_t)(_epoch_u8) & 0xFFu) << MESHBUS_DESKTOP_APP_HANDLE_EPOCH_SHIFT) |       \
		((uint32_t)(_index_plus1_u16) & 0xFFFFu)))

#define MESHBUS_DESKTOP_APP_HANDLE_GET_EPOCH(_h)                                                  \
	((uint8_t)(((uint32_t)(_h) & MESHBUS_DESKTOP_APP_HANDLE_EPOCH_MASK)                        \
		  >> MESHBUS_DESKTOP_APP_HANDLE_EPOCH_SHIFT))

#define MESHBUS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(_h)                                            \
	((uint16_t)((uint32_t)(_h) & MESHBUS_DESKTOP_APP_HANDLE_INDEX_MASK))

static inline bool meshbus_desktop_app_handle_is_valid(meshbus_desktop_app_handle_t h)
{
	if (h == MESHBUS_DESKTOP_APP_HANDLE_INVALID) {
		return false;
	}
	if (((uint32_t)h & MESHBUS_DESKTOP_APP_HANDLE_MAGIC_MASK) != MESHBUS_DESKTOP_APP_HANDLE_MAGIC) {
		return false;
	}
	if ((((uint32_t)h & MESHBUS_DESKTOP_APP_HANDLE_VER_MASK) >>
	     MESHBUS_DESKTOP_APP_HANDLE_VER_SHIFT) != MESHBUS_DESKTOP_APP_HANDLE_VERSION) {
		return false;
	}
	return MESHBUS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(h) != 0u;
}

/** @brief Encode OPEN_APP event payload from an app handle. */
#define MESHBUS_DESKTOP_EVENT_OPEN_APP(_handle) ((uint32_t)(_handle))
/** @brief Check whether custom event value encodes OPEN_APP. */
#define MESHBUS_DESKTOP_EVENT_IS_OPEN_APP(_evt)                                                   \
	meshbus_desktop_app_handle_is_valid((meshbus_desktop_app_handle_t)(_evt))
/** @brief Decode app handle from OPEN_APP custom event value. */
#define MESHBUS_DESKTOP_EVENT_OPEN_APP_HANDLE(_evt) ((meshbus_desktop_app_handle_t)(_evt))

/** @brief App-private custom event base range. */
enum meshbus_desktop_app_custom_event {
	MESHBUS_DESKTOP_APP_EVENT_BASE = 0x2000,
};

struct zui_icon;
struct zui_host;
struct zui_screen;
struct meshbus_desktop_dashboard_widget;

/** @brief Desktop app thread entry function type. */
typedef void (*meshbus_desktop_app_main_t)(void *args);

/** @brief Cleanup callback for one-shot external Desktop app sessions. */
typedef void (*meshbus_desktop_external_app_cleanup_t)(void *user_data);

/** @brief Desktop app runtime arguments passed to app entry. */
struct meshbus_desktop_app_args {
	const char *app_id;
	const char *display_name;
	struct zui_host *host;
	void *user_data;
};

/** @brief Desktop app descriptor registered via iterable section. */
struct meshbus_desktop_app_desc {
	const char *id;
	const char *display_name;
	meshbus_desktop_app_main_t app_main;
	/* Kept for compatibility; desktop app threads use the shared runtime stack. */
	k_thread_stack_t *stack;
	/* Required stack size declaration used for validation/compat checks. */
	size_t stack_size;
	const struct zui_icon *menu_icon;
	uint16_t menu_index;
};

/** @brief One-shot external Desktop app launch descriptor. */
struct meshbus_desktop_external_app_desc {
	const char *id;
	const char *display_name;
	meshbus_desktop_app_main_t app_main;
	size_t stack_size;
	void *user_data;
	meshbus_desktop_external_app_cleanup_t cleanup;
};

/** @brief Get app handle by stable registry index. */
meshbus_desktop_app_handle_t meshbus_desktop_app_registry_get_handle(size_t index);

/** @brief Resolve app descriptor by app handle. */
int meshbus_desktop_app_registry_resolve_handle(meshbus_desktop_app_handle_t handle,
						const struct meshbus_desktop_app_desc **desc_out);

/** @brief Resolve app handle from app ID string. */
meshbus_desktop_app_handle_t meshbus_desktop_app_registry_handle_from_id(const char *id);

/** @brief Get app descriptor by app ID string. */
const struct meshbus_desktop_app_desc *meshbus_desktop_app_registry_get_by_id(const char *id);

/**
 * @brief Start a one-shot external desktop app asynchronously.
 *
 * The external app is not added to the static menu/launcher registry.
 * Start is accepted only while desktop launcher scene is active.
 *
 * @param desc External app launch descriptor. The Desktop runtime copies
 *             `id` and `display_name`, stores `user_data`, and calls
 *             `cleanup(user_data)` after the app entry returns.
 *
 * @retval 0 Launch flow request accepted (asynchronous).
 * @retval -EINVAL Invalid @p id, @p entry or @p stack_size.
 * @retval -ENODEV Desktop service is not initialized.
 * @retval -EBUSY Desktop launcher scene is not currently active or an app is
 *		 already active.
 * @retval -EEXIST @p id conflicts with an existing static/external app.
 * @retval -ENOMEM Failed to allocate external app runtime resources.
 * @retval -ENOTSUP Desktop launcher/external app runtime support is disabled
 *		    (`CONFIG_MESHBUS_DESKTOP_LAUNCHER=n`).
 */
int meshbus_desktop_external_app_start(const struct meshbus_desktop_external_app_desc *desc);

/** @brief Return true until a one-shot external Desktop app thread is fully reclaimed. */
bool meshbus_desktop_external_app_is_active(void);

/** @brief Get total number of statically registered desktop apps. */
size_t meshbus_desktop_app_registry_count(void);

/** @brief Get app descriptor by registry index. */
const struct meshbus_desktop_app_desc *meshbus_desktop_app_registry_get(size_t index);

#define MESHBUS_DESKTOP_APP_VIEW_COUNT_NONE 0U
#define MESHBUS_DESKTOP_APP_MENU_INDEX_NONE UINT16_MAX

#define MESHBUS_DESKTOP_APP_CONCAT_INNER(a, b) a##b
#define MESHBUS_DESKTOP_APP_CONCAT(a, b)       MESHBUS_DESKTOP_APP_CONCAT_INNER(a, b)

/**
 * @brief Define a statically registered desktop app descriptor.
 */
#define MESHBUS_DESKTOP_APP_DEFINE_INNER(_n, id_str, display_str, app_main_fn, stack_sz,         \
					 menu_icon_ptr, menu_index_u16)                            \
	static STRUCT_SECTION_ITERABLE(meshbus_desktop_app_desc,                                     \
		MESHBUS_DESKTOP_APP_CONCAT(meshbus_desktop_app_desc_, _n)) = {                      \
		.id = (id_str),                                                                    \
		.display_name = (display_str),                                                      \
		.app_main = (app_main_fn),                                                          \
		.stack = NULL,                                                                      \
		.stack_size = (size_t)(stack_sz) +                                                 \
			      (0U * sizeof(char[((size_t)(stack_sz) <=                           \
					  (size_t)CONFIG_MESHBUS_DESKTOP_APP_SHARED_STACK_SIZE) ? \
							 1 : -1])),                              \
		.menu_icon = (menu_icon_ptr),                                                       \
		.menu_index = (uint16_t)(menu_index_u16),                                           \
	}

#define MESHBUS_DESKTOP_APP_DEFINE(id_str, display_str, app_main_fn, stack_sz, menu_icon_ptr,    \
				   menu_index_u16)                                          \
	MESHBUS_DESKTOP_APP_DEFINE_INNER(__COUNTER__, id_str, display_str, app_main_fn, stack_sz,  \
					 menu_icon_ptr, menu_index_u16)

/** @brief Widget registration descriptor context object. */
struct meshbus_desktop_dashboard_widget {
	const struct meshbus_desktop_dashboard_widget_desc *desc;
};

/**
 * @brief Dashboard widget poll callback.
 *
 * Called by desktop dashboard scheduler while the widget is active.
 *
 * @param ctx Widget context.
 *
 * @return Delay in milliseconds until the next poll.
 */
typedef uint32_t (*meshbus_desktop_dashboard_widget_tick_cb)(
	struct meshbus_desktop_dashboard_widget *ctx);

/** @brief Dashboard widget descriptor registered via iterable section. */
struct meshbus_desktop_dashboard_widget_desc {
	uint16_t index;
	const char *title;
	struct zui_screen *(*screen_create)(struct meshbus_desktop_dashboard_widget *ctx);
	meshbus_desktop_dashboard_widget_tick_cb tick;
	const char *app_id;
};

/**
 * @brief Define a statically registered dashboard widget descriptor.
 */
#define MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(idx_, title_, dashboard_widget_screen_,          \
					  dashboard_widget_tick_, target_app_id_)          \
	STRUCT_SECTION_ITERABLE(meshbus_desktop_dashboard_widget_desc,                              \
				UTIL_CAT(meshbus_desktop_dashboard_widget_,                            \
					 dashboard_widget_screen_)) = {                                  \
		.index = (uint16_t)(idx_),                                                       \
		.title = (title_),                                                               \
		.tick = (dashboard_widget_tick_),                                                \
		.app_id = (target_app_id_),                                                      \
		.screen_create = (dashboard_widget_screen_),                                     \
	}

/** @brief Get total number of statically registered dashboard widgets. */
size_t meshbus_desktop_dashboard_widgets_count(void);

/** @brief Get dashboard widget context by sorted registry index. */
struct meshbus_desktop_dashboard_widget *meshbus_desktop_dashboard_widgets_get(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MESHBUS_DESKTOP_H_ */
