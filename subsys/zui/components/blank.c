/*
 * Copyright (c) 2026 FoBE Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include "internal.h"

struct zui_blank {
	struct zui_screen *screen;
};

static void zui_blank_draw(struct zui_draw_ctx *draw, void *user_data)
{
	ARG_UNUSED(user_data);
	zui_draw_reset(draw);
}

static const struct zui_screen_ops zui_blank_screen_ops = {
	.draw = zui_blank_draw,
};

struct zui_blank *zui_blank_create(void)
{
	struct zui_blank *blank = zui_calloc(1U, sizeof(*blank));

	if (blank == NULL) {
		return NULL;
	}

	blank->screen = zui_screen_create(&zui_blank_screen_ops, blank);
	if (blank->screen == NULL) {
		zui_blank_destroy(blank);
		return NULL;
	}

	return blank;
}

void zui_blank_destroy(struct zui_blank *blank)
{
	if (blank == NULL) {
		return;
	}

	zui_screen_destroy(blank->screen);
	zui_free(blank);
}

struct zui_screen *zui_blank_get_screen(struct zui_blank *blank)
{
	return blank == NULL ? NULL : blank->screen;
}
