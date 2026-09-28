/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system_private.h"

struct text_writer {
	char *buf;
	size_t len;
	size_t pos;
	bool truncated;
};

static void text_writer_init(struct text_writer *w, char *buf, size_t len)
{
	if (w == NULL || buf == NULL || len == 0U) {
		return;
	}

	w->buf = buf;
	w->len = len;
	w->pos = 0U;
	w->truncated = false;
	w->buf[0] = '\0';
}

static void text_writer_append(struct text_writer *w, const char *fmt, ...)
{
	va_list ap;
	int rc;
	size_t rem;

	if (w == NULL || w->buf == NULL || w->len == 0U || fmt == NULL ||
	    w->pos >= w->len - 1U) {
		if (w != NULL) {
			w->truncated = true;
		}
		return;
	}

	rem = w->len - w->pos;
	va_start(ap, fmt);
	rc = vsnprintk(&w->buf[w->pos], rem, fmt, ap);
	va_end(ap);
	if (rc < 0) {
		return;
	}
	if ((size_t)rc >= rem) {
		w->pos = w->len - 1U;
		w->buf[w->pos] = '\0';
		w->truncated = true;
		return;
	}

	w->pos += (size_t)rc;
}

static bool system_heap_stats_get(struct sys_memory_stats *stats)
{
	if (stats == NULL) {
		return false;
	}

	*stats = (struct sys_memory_stats){0};
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
#if (K_HEAP_MEM_POOL_SIZE > 0)
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
#else
	return false;
#endif
}

static void system_device_id_hex(char *out, size_t out_size)
{
	uint8_t id[16];
	ssize_t id_len;
	size_t pos = 0U;

	if (out == NULL || out_size == 0U) {
		return;
	}

	id_len = hwinfo_get_device_id(id, sizeof(id));
	if (id_len <= 0) {
		(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
		return;
	}

	out[0] = '\0';
	for (size_t i = 0U; i < (size_t)id_len && pos + 2U < out_size; i++) {
		pos += (size_t)snprintk(&out[pos], out_size - pos, "%02x", id[i]);
	}
}

static void system_reset_cause(char *out, size_t out_size)
{
	uint32_t cause = 0U;

	if (hwinfo_get_reset_cause(&cause) != 0 || cause == 0U) {
		(void)snprintk(out, out_size, "%s", DESKTOP_TEXT_COMMON_UNKNOWN_WORD);
		return;
	}

	(void)snprintk(out, out_size, "0x%08x", cause);
}


static void system_open_info_detail(struct system_app *app, uint32_t id)
{
	const char *title = DESKTOP_TEXT_SYSTEM_TITLE;

	switch (id) {
	case SYSTEM_INFO_IDENTITY:
		title = DESKTOP_TEXT_SYSTEM_MENU_IDENTITY;
		{
			char serial[40];

			system_device_id_hex(serial, sizeof(serial));
			(void)snprintk(app->text, sizeof(app->text),
				       DESKTOP_TEXT_SYSTEM_INFO_IDENTITY_FORMAT,
				       SYSTEM_DEVICE_NAME, SYSTEM_MANUFACTURER_NAME,
				       SYSTEM_MODEL_NAME, serial);
		}
		break;
	case SYSTEM_INFO_HARDWARE:
		title = DESKTOP_TEXT_SYSTEM_MENU_HARDWARE;
		{
#if defined(CONFIG_BT)
			char bt_addr[24];

			system_bluetooth_addr(bt_addr, sizeof(bt_addr));
			(void)snprintk(app->text, sizeof(app->text),
				       DESKTOP_TEXT_SYSTEM_INFO_HARDWARE_BLUETOOTH_FORMAT,
				       SYSTEM_SOC_NAME,
				       (unsigned int)SYSTEM_FLASH_SIZE_KB,
				       (unsigned int)SYSTEM_RAM_SIZE_KB, bt_addr);
#else
			(void)snprintk(app->text, sizeof(app->text),
				       DESKTOP_TEXT_SYSTEM_INFO_HARDWARE_FORMAT,
				       SYSTEM_SOC_NAME,
				       (unsigned int)SYSTEM_FLASH_SIZE_KB,
				       (unsigned int)SYSTEM_RAM_SIZE_KB);
#endif
		}
		break;
	case SYSTEM_INFO_FIRMWARE:
		title = DESKTOP_TEXT_SYSTEM_MENU_FIRMWARE;
		(void)snprintk(app->text, sizeof(app->text),
			       DESKTOP_TEXT_SYSTEM_INFO_FIRMWARE_FORMAT,
			       APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION),
			       KERNEL_VERSION_STRING);
		break;
	case SYSTEM_INFO_RUNTIME:
		title = DESKTOP_TEXT_SYSTEM_MENU_RUNTIME;
		{
			struct sys_memory_stats sys_stats;
			struct zui_runtime_stats zui_stats;
			char sys_heap_free[16];
			char sys_heap_alloc[16];
			char zui_heap_free[16];
			char zui_heap_alloc[16];
			char reset_cause[32];

			system_reset_cause(reset_cause, sizeof(reset_cause));
			if (system_heap_stats_get(&sys_stats)) {
				(void)snprintk(sys_heap_free, sizeof(sys_heap_free), "%u",
					       (unsigned int)sys_stats.free_bytes);
				(void)snprintk(sys_heap_alloc, sizeof(sys_heap_alloc), "%u",
					       (unsigned int)sys_stats.allocated_bytes);
			} else {
				(void)snprintk(sys_heap_free, sizeof(sys_heap_free), "%s",
					       DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
				(void)snprintk(sys_heap_alloc, sizeof(sys_heap_alloc), "%s",
					       DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
			}
			if (zui_get_runtime_stats(&zui_stats) == 0 && zui_stats.heap_stats_available) {
				(void)snprintk(zui_heap_free, sizeof(zui_heap_free), "%u",
					       (unsigned int)zui_stats.heap_free_bytes);
				(void)snprintk(zui_heap_alloc, sizeof(zui_heap_alloc), "%u",
					       (unsigned int)zui_stats.heap_allocated_bytes);
			} else {
				(void)snprintk(zui_heap_free, sizeof(zui_heap_free), "%s",
					       DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
				(void)snprintk(zui_heap_alloc, sizeof(zui_heap_alloc), "%s",
					       DESKTOP_TEXT_COMMON_NOT_AVAILABLE);
			}
			(void)snprintk(app->text, sizeof(app->text),
				       DESKTOP_TEXT_SYSTEM_INFO_RUNTIME_FORMAT,
				       (unsigned long long)k_uptime_get(),
				       (unsigned long long)k_cycle_get_64(), reset_cause,
				       sys_heap_free, sys_heap_alloc, zui_heap_free,
				       zui_heap_alloc);
		}
		break;
	case SYSTEM_INFO_DEVICES:
	default:
		title = DESKTOP_TEXT_SYSTEM_MENU_DEVICES;
		{
			struct text_writer w;
			const struct device *devlist;
			size_t devcnt;

			text_writer_init(&w, app->text, sizeof(app->text));
			devcnt = z_device_get_all_static(&devlist);
			for (size_t i = 0U; i < devcnt; i++) {
				const struct device *dev = &devlist[i];
				const char *state = device_is_ready(dev) ?
							    DESKTOP_TEXT_SYSTEM_DEVICE_STATE_READY :
							    DESKTOP_TEXT_SYSTEM_DEVICE_STATE_DISABLED;
				int usage = pm_device_runtime_usage(dev);

				if (usage >= 0) {
					text_writer_append(&w,
							   DESKTOP_TEXT_SYSTEM_DEVICE_USAGE_FORMAT,
							   dev->name, state, usage);
				} else {
					text_writer_append(&w,
							   DESKTOP_TEXT_SYSTEM_DEVICE_FORMAT,
							   dev->name, state);
				}
				if (w.truncated) {
					break;
				}
			}
		}
		break;
	}

	app->text_back_screen = SYSTEM_SCREEN_INFO_MENU;
	system_text_update(app, title);
	desktop_app_switch(&app->ctx, SYSTEM_SCREEN_TEXT);
}

void system_info_menu_selected(struct zui_sublist *list, uint32_t id, size_t index,
				      const struct zui_input_event *event, void *user_data)
{
	struct system_app *app = user_data;

	ARG_UNUSED(list);
	ARG_UNUSED(index);
	ARG_UNUSED(event);

	if (app != NULL) {
		system_open_info_detail(app, id);
	}
}
