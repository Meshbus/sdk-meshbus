/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <desktop/desktop.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zui/zui.h>

#define SNAKE_APP_SCREEN 1U

#define SNAKE_GRID_W 16
#define SNAKE_GRID_H 8
#define SNAKE_CELL_PX 6
#define SNAKE_BOARD_X 16
#define SNAKE_BOARD_Y 13
#define SNAKE_MAX_LEN (SNAKE_GRID_W * SNAKE_GRID_H)

enum snake_dir {
	SNAKE_DIR_UP,
	SNAKE_DIR_DOWN,
	SNAKE_DIR_LEFT,
	SNAKE_DIR_RIGHT,
};

struct snake_cell {
	uint8_t x;
	uint8_t y;
};

struct snake_app {
	struct zui_host *host;
	struct zui_router *router;
	struct zui_screen *screen;
	struct k_sem tick_sem;
	struct k_sem exit_sem;
	struct k_mutex lock;
	struct snake_cell body[SNAKE_MAX_LEN];
	struct snake_cell food;
	enum snake_dir dir;
	enum snake_dir pending_dir;
	uint32_t rng;
	uint16_t score;
	uint8_t len;
	bool running;
	bool game_over;
};

static bool snake_cells_equal(struct snake_cell a, struct snake_cell b)
{
	return a.x == b.x && a.y == b.y;
}

static bool snake_occupied(const struct snake_app *app, struct snake_cell cell)
{
	for (uint8_t i = 0U; i < app->len; i++) {
		if (snake_cells_equal(app->body[i], cell)) {
			return true;
		}
	}

	return false;
}

static uint32_t snake_rand(struct snake_app *app)
{
	app->rng = app->rng * 1103515245U + 12345U;
	return app->rng;
}

static void snake_place_food(struct snake_app *app)
{
	for (uint16_t attempt = 0U; attempt < SNAKE_MAX_LEN; attempt++) {
		uint32_t value = snake_rand(app);
		struct snake_cell food = {
			.x = (uint8_t)(value % SNAKE_GRID_W),
			.y = (uint8_t)((value / SNAKE_GRID_W) % SNAKE_GRID_H),
		};

		if (!snake_occupied(app, food)) {
			app->food = food;
			return;
		}
	}

	app->food = (struct snake_cell){.x = 0U, .y = 0U};
}

static void snake_reset_locked(struct snake_app *app)
{
	app->len = 4U;
	app->body[0] = (struct snake_cell){.x = 7U, .y = 4U};
	app->body[1] = (struct snake_cell){.x = 6U, .y = 4U};
	app->body[2] = (struct snake_cell){.x = 5U, .y = 4U};
	app->body[3] = (struct snake_cell){.x = 4U, .y = 4U};
	app->dir = SNAKE_DIR_RIGHT;
	app->pending_dir = SNAKE_DIR_RIGHT;
	app->rng = 0x5a17c0deU;
	app->score = 0U;
	app->running = true;
	app->game_over = false;
	snake_place_food(app);
}

static bool snake_dir_opposes(enum snake_dir a, enum snake_dir b)
{
	return ((a == SNAKE_DIR_UP && b == SNAKE_DIR_DOWN) ||
		(a == SNAKE_DIR_DOWN && b == SNAKE_DIR_UP) ||
		(a == SNAKE_DIR_LEFT && b == SNAKE_DIR_RIGHT) ||
		(a == SNAKE_DIR_RIGHT && b == SNAKE_DIR_LEFT));
}

static void snake_step_locked(struct snake_app *app)
{
	struct snake_cell next = app->body[0];
	bool grow;

	if (!app->running || app->game_over) {
		return;
	}

	if (!snake_dir_opposes(app->dir, app->pending_dir)) {
		app->dir = app->pending_dir;
	}

	switch (app->dir) {
	case SNAKE_DIR_UP:
		if (next.y == 0U) {
			app->game_over = true;
			app->running = false;
			return;
		}
		next.y--;
		break;
	case SNAKE_DIR_DOWN:
		next.y++;
		break;
	case SNAKE_DIR_LEFT:
		if (next.x == 0U) {
			app->game_over = true;
			app->running = false;
			return;
		}
		next.x--;
		break;
	case SNAKE_DIR_RIGHT:
		next.x++;
		break;
	}

	if (next.x >= SNAKE_GRID_W || next.y >= SNAKE_GRID_H) {
		app->game_over = true;
		app->running = false;
		return;
	}

	grow = snake_cells_equal(next, app->food);
	for (uint8_t i = 0U; i < app->len - (grow ? 0U : 1U); i++) {
		if (snake_cells_equal(next, app->body[i])) {
			app->game_over = true;
			app->running = false;
			return;
		}
	}

	if (grow && app->len < SNAKE_MAX_LEN) {
		app->len++;
		app->score++;
	}
	for (uint8_t i = app->len - 1U; i > 0U; i--) {
		app->body[i] = app->body[i - 1U];
	}
	app->body[0] = next;

	if (grow) {
		snake_place_food(app);
	}
}

static void snake_draw_cell(struct zui_draw_ctx *draw, struct snake_cell cell, bool filled)
{
	struct zui_rect rect = {
		.x = SNAKE_BOARD_X + 1 + (int16_t)cell.x * SNAKE_CELL_PX,
		.y = SNAKE_BOARD_Y + 1 + (int16_t)cell.y * SNAKE_CELL_PX,
		.width = SNAKE_CELL_PX - 1,
		.height = SNAKE_CELL_PX - 1,
	};

	if (filled) {
		zui_draw_box(draw, &rect);
	} else {
		zui_draw_rect(draw, &rect);
	}
}

static void snake_format_score(char *buf, size_t buf_size, uint16_t score)
{
	static const char prefix[] = "Snake  ";
	char digits[6];
	size_t pos = 0U;
	size_t digit_count = 0U;

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	while (prefix[pos] != '\0' && pos + 1U < buf_size) {
		buf[pos] = prefix[pos];
		pos++;
	}

	do {
		digits[digit_count++] = (char)('0' + (score % 10U));
		score /= 10U;
	} while (score > 0U && digit_count < ARRAY_SIZE(digits));

	while (digit_count > 0U && pos + 1U < buf_size) {
		buf[pos++] = digits[--digit_count];
	}

	buf[pos] = '\0';
}

static void snake_app_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct snake_app *app = user_data;
	struct snake_cell body[SNAKE_MAX_LEN];
	struct snake_cell food;
	uint16_t score = 0U;
	uint8_t len = 0U;
	bool running = false;
	bool game_over = false;
	char score_text[24];

	if (app == NULL) {
		return;
	}

	k_mutex_lock(&app->lock, K_FOREVER);
	len = app->len;
	for (uint8_t i = 0U; i < len; i++) {
		body[i] = app->body[i];
	}
	food = app->food;
	score = app->score;
	running = app->running;
	game_over = app->game_over;
	k_mutex_unlock(&app->lock);

	zui_draw_reset(draw);
	snake_format_score(score_text, sizeof(score_text), score);
	zui_draw_text(draw, (struct zui_point){.x = 1, .y = 0}, score_text);
	zui_draw_text_aligned(draw, (struct zui_point){.x = 127, .y = 0},
			      ZUI_ALIGN_RIGHT, ZUI_ALIGN_TOP,
			      game_over ? "OK" : (running ? "RUN" : "PAUSE"));

	zui_draw_rect(draw, &(struct zui_rect){
		.x = SNAKE_BOARD_X,
		.y = SNAKE_BOARD_Y,
		.width = SNAKE_GRID_W * SNAKE_CELL_PX + 1,
		.height = SNAKE_GRID_H * SNAKE_CELL_PX + 1,
	});
	snake_draw_cell(draw, food, false);
	for (uint8_t i = 0U; i < len; i++) {
		snake_draw_cell(draw, body[i], true);
	}

	if (game_over) {
		zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 60},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, "Game Over");
	} else if (!running) {
		zui_draw_text_aligned(draw, (struct zui_point){.x = 64, .y = 60},
				      ZUI_ALIGN_CENTER, ZUI_ALIGN_BOTTOM, "Paused");
	}
}

static bool snake_app_input(const struct zui_input_event *event, void *user_data)
{
	struct snake_app *app = user_data;
	bool changed = false;

	if (event == NULL || app == NULL) {
		return false;
	}
	if (event->action != ZUI_INPUT_ACTION_CLICK) {
		return event->action == ZUI_INPUT_ACTION_PRESS ||
		       event->action == ZUI_INPUT_ACTION_RELEASE;
	}

	k_mutex_lock(&app->lock, K_FOREVER);
	switch (event->code) {
	case ZUI_INPUT_CODE_BACK:
		k_sem_give(&app->exit_sem);
		changed = true;
		break;
	case ZUI_INPUT_CODE_SELECT:
		if (app->game_over) {
			snake_reset_locked(app);
		} else {
			app->running = !app->running;
		}
		changed = true;
		break;
	case ZUI_INPUT_CODE_UP:
		app->pending_dir = SNAKE_DIR_UP;
		changed = true;
		break;
	case ZUI_INPUT_CODE_DOWN:
		app->pending_dir = SNAKE_DIR_DOWN;
		changed = true;
		break;
	case ZUI_INPUT_CODE_LEFT:
		app->pending_dir = SNAKE_DIR_LEFT;
		changed = true;
		break;
	case ZUI_INPUT_CODE_RIGHT:
		app->pending_dir = SNAKE_DIR_RIGHT;
		changed = true;
		break;
	default:
		break;
	}
	k_mutex_unlock(&app->lock);

	if (changed) {
		k_sem_give(&app->tick_sem);
		if (app->host != NULL) {
			(void)zui_host_request_redraw(app->host);
		}
	}

	return changed;
}

static const struct zui_screen_ops snake_app_ops = {
	.draw = snake_app_draw,
	.input = snake_app_input,
};

void snake_app_main(void *args)
{
	struct mbs_desktop_app_args *app_args = args;
	struct snake_app app = {0};
	int rc;

	if (app_args == NULL || app_args->host == NULL) {
		return;
	}

	printk("[snake-app] start\n");
	app.host = app_args->host;
	k_sem_init(&app.tick_sem, 0, 1);
	k_sem_init(&app.exit_sem, 0, 1);
	k_mutex_init(&app.lock);
	snake_reset_locked(&app);

	app.router = zui_router_create();
	app.screen = zui_screen_create(&snake_app_ops, &app);
	if (app.router == NULL || app.screen == NULL) {
		goto out;
	}

	rc = zui_router_register_screen(app.router, SNAKE_APP_SCREEN, app.screen);
	if (rc != 0 || zui_router_switch(app.router, SNAKE_APP_SCREEN) != 0 ||
	    zui_host_attach_router(app.host, ZUI_LAYER_FULLSCREEN, app.router) != 0) {
		goto out;
	}
	(void)zui_host_send_layer_to_front(app.host, ZUI_LAYER_FULLSCREEN);
	(void)zui_host_set_layer_enabled(app.host, ZUI_LAYER_FULLSCREEN, true);
	(void)zui_host_request_redraw(app.host);

	while (k_sem_take(&app.exit_sem, K_NO_WAIT) != 0) {
		(void)k_sem_take(&app.tick_sem, K_MSEC(220));
		k_mutex_lock(&app.lock, K_FOREVER);
		snake_step_locked(&app);
		k_mutex_unlock(&app.lock);
		(void)zui_host_request_redraw(app.host);
	}

out:
	if (app.host != NULL) {
		(void)zui_host_detach_router(app.host, ZUI_LAYER_FULLSCREEN);
		(void)zui_host_request_redraw(app.host);
	}
	if (app.router != NULL && app.screen != NULL) {
		(void)zui_router_unregister_screen(app.router, SNAKE_APP_SCREEN);
	}
	if (app.screen != NULL) {
		zui_screen_destroy(app.screen);
	}
	if (app.router != NULL) {
		zui_router_destroy(app.router);
	}
	printk("[snake-app] exit\n");
}

LL_EXTENSION_SYMBOL(snake_app_main);
