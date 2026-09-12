/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zui/zui.h>

#ifdef __cplusplus
extern "C" {
#endif

struct desktop_app_context {
	struct zui_host *host;
	struct zui_router *router;
	struct k_sem exit_sem;
	bool exit_requested;
};

void desktop_app_context_init(struct desktop_app_context *ctx, struct zui_host *host);
void desktop_app_context_cleanup(struct desktop_app_context *ctx);
void desktop_app_request_redraw(struct desktop_app_context *ctx);
void desktop_app_exit(struct desktop_app_context *ctx);
void desktop_app_wait(struct desktop_app_context *ctx);
void desktop_app_switch(struct desktop_app_context *ctx, uint32_t screen_id);

bool desktop_app_input_is_click(const struct zui_input_event *event);
bool desktop_app_input_is_long_press(const struct zui_input_event *event);
bool desktop_app_input_should_consume_edge(const struct zui_input_event *event);

#ifdef __cplusplus
}
#endif
