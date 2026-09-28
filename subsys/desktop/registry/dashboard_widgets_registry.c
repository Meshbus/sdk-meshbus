/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dashboard_widgets_registry.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(zui_desktop_widget_registry, LOG_LEVEL_INF);

#ifndef MBS_DESKTOP_WIDGET_REGISTRY_MAX
#define MBS_DESKTOP_WIDGET_REGISTRY_MAX 16
#endif

struct dashboard_widget_entry {
	const struct mbs_desktop_dashboard_widget_desc *desc;
	struct mbs_desktop_dashboard_widget ctx;
};

static K_MUTEX_DEFINE(widget_registry_mutex);
static struct dashboard_widget_entry widgets[MBS_DESKTOP_WIDGET_REGISTRY_MAX];
static size_t widget_count;
static bool loaded;

static bool dashboard_widget_desc_valid(const struct mbs_desktop_dashboard_widget_desc *w)
{
	if (w == NULL || w->title == NULL) {
		return false;
	}

	if (w->screen_create == NULL) {
		return false;
	}

	return true;
}

static int dashboard_widget_desc_cmp(const struct mbs_desktop_dashboard_widget_desc *a,
			   const struct mbs_desktop_dashboard_widget_desc *b)
{
	if (a == b) {
		return 0;
	}

	if (a == NULL) {
		return 1;
	}

	if (b == NULL) {
		return -1;
	}

	if (a->index < b->index) {
		return -1;
	}

	if (a->index > b->index) {
		return 1;
	}

	return 0;
}

static void dashboard_widget_desc_sort_by_index(const struct mbs_desktop_dashboard_widget_desc **list, size_t n)
{
	/* Simple stable insertion sort (n is tiny; avoids pulling in libc qsort). */
	for (size_t i = 1; i < n; i++) {
		const struct mbs_desktop_dashboard_widget_desc *key = list[i];
		size_t j = i;

		while (j > 0U && dashboard_widget_desc_cmp(list[j - 1U], key) > 0) {
			list[j] = list[j - 1U];
			j--;
		}

		list[j] = key;
	}
}

static void dashboard_widgets_registry_load_locked(void)
{
	const struct mbs_desktop_dashboard_widget_desc *tmp[MBS_DESKTOP_WIDGET_REGISTRY_MAX];
	size_t tmp_count = 0U;

	widget_count = 0U;

	STRUCT_SECTION_FOREACH(mbs_desktop_dashboard_widget_desc, w) {
		if (!dashboard_widget_desc_valid(w)) {
			LOG_WRN("Invalid widget desc: %p", w);
			continue;
		}

		if (tmp_count >= ARRAY_SIZE(tmp)) {
			LOG_ERR("Widget registry full (%u)", (unsigned)ARRAY_SIZE(tmp));
			break;
		}

		tmp[tmp_count++] = w;
	}

	dashboard_widget_desc_sort_by_index(tmp, tmp_count);

	for (size_t i = 0U; i < tmp_count; i++) {
		const struct mbs_desktop_dashboard_widget_desc *w = tmp[i];

		if (i > 0U && tmp[i - 1U] != NULL && w != NULL && tmp[i - 1U]->index == w->index) {
			LOG_WRN("Skipping duplicate widget index %u", (unsigned)w->index);
			continue;
		}

		widgets[widget_count].desc = w;
		widgets[widget_count].ctx.desc = w;
		widget_count++;
	}

	loaded = true;
}

static void dashboard_widgets_registry_ensure_loaded(void)
{
	k_mutex_lock(&widget_registry_mutex, K_FOREVER);
	if (!loaded) {
		dashboard_widgets_registry_load_locked();
	}
	k_mutex_unlock(&widget_registry_mutex);
}

size_t mbs_desktop_dashboard_widgets_count(void)
{
	dashboard_widgets_registry_ensure_loaded();

	size_t count;
	k_mutex_lock(&widget_registry_mutex, K_FOREVER);
	count = widget_count;
	k_mutex_unlock(&widget_registry_mutex);
	return count;
}

struct mbs_desktop_dashboard_widget *mbs_desktop_dashboard_widgets_get(size_t index)
{
	dashboard_widgets_registry_ensure_loaded();

	struct mbs_desktop_dashboard_widget *ctx = NULL;
	k_mutex_lock(&widget_registry_mutex, K_FOREVER);
	if (index < widget_count) {
		ctx = &widgets[index].ctx;
	}
	k_mutex_unlock(&widget_registry_mutex);
	return ctx;
}
