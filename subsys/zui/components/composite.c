/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_composite {
	struct zui_composite_config config;
	struct zui_screen *screen;
};

static void zui_composite_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct zui_composite *composite = user_data;

	if (composite != NULL && composite->config.draw != NULL) {
		composite->config.draw(composite, draw, composite->config.user_data);
	}
}

static bool zui_composite_input(const struct zui_input_event *event, void *user_data)
{
	struct zui_composite *composite = user_data;

	if (composite == NULL || composite->config.input == NULL) {
		return false;
	}

	return composite->config.input(composite, event, composite->config.user_data);
}

static const struct zui_screen_ops zui_composite_screen_ops = {
	.draw = zui_composite_draw,
	.input = zui_composite_input,
};

struct zui_composite *zui_composite_create(const struct zui_composite_config *config)
{
	struct zui_composite *composite;

	if (config == NULL) {
		return NULL;
	}

	composite = zui_calloc(1U, sizeof(*composite));
	if (composite == NULL) {
		return NULL;
	}

	(void)zui_composite_update(composite, config);
	composite->screen = zui_screen_create(&zui_composite_screen_ops, composite);
	if (composite->screen == NULL) {
		zui_composite_destroy(composite);
		return NULL;
	}

	return composite;
}

void zui_composite_destroy(struct zui_composite *composite)
{
	if (composite == NULL) {
		return;
	}

	zui_screen_destroy(composite->screen);
	zui_free(composite);
}

struct zui_screen *zui_composite_get_screen(struct zui_composite *composite)
{
	return composite == NULL ? NULL : composite->screen;
}

int zui_composite_update(struct zui_composite *composite, const struct zui_composite_config *config)
{
	if (composite == NULL || config == NULL) {
		return -EINVAL;
	}

	composite->config = *config;
	return zui_component_request_redraw(composite->screen);
}

int zui_composite_request_redraw(struct zui_composite *composite)
{
	return composite == NULL ? -EINVAL : zui_component_request_redraw(composite->screen);
}
