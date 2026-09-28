/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */

#include "apps/app_common.h"

#include "desktop_private.h"

#include <string.h>

void desktop_app_context_init(struct desktop_app_context *ctx, struct zui_host *host)
{
	if (ctx == NULL) {
		return;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->host = host;
	k_sem_init(&ctx->exit_sem, 0, 1);
}

void desktop_app_context_cleanup(struct desktop_app_context *ctx)
{
	if (ctx == NULL) {
		return;
	}

	if (ctx->host != NULL) {
		(void)zui_host_detach_router(ctx->host, ZUI_LAYER_FULLSCREEN);
	}
	if (ctx->router != NULL) {
		zui_router_destroy(ctx->router);
		ctx->router = NULL;
	}
}

void desktop_app_request_redraw(struct desktop_app_context *ctx)
{
	if (ctx != NULL && ctx->host != NULL) {
		zui_desktop_request_redraw(zui_host_get_user_data(ctx->host));
	}
}

void desktop_app_exit(struct desktop_app_context *ctx)
{
	if (ctx == NULL || ctx->exit_requested) {
		return;
	}

	ctx->exit_requested = true;
	k_sem_give(&ctx->exit_sem);
	desktop_app_request_redraw(ctx);
}

void desktop_app_wait(struct desktop_app_context *ctx)
{
	if (ctx != NULL) {
		(void)k_sem_take(&ctx->exit_sem, K_FOREVER);
	}
}

void desktop_app_switch(struct desktop_app_context *ctx, uint32_t screen_id)
{
	if (ctx == NULL || ctx->router == NULL) {
		return;
	}

	(void)zui_router_switch(ctx->router, screen_id);
	desktop_app_request_redraw(ctx);
}

bool desktop_app_input_is_click(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_CLICK;
}

bool desktop_app_input_is_long_press(const struct zui_input_event *event)
{
	return event != NULL && event->action == ZUI_INPUT_ACTION_LONG_PRESS;
}

bool desktop_app_input_should_consume_edge(const struct zui_input_event *event)
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
