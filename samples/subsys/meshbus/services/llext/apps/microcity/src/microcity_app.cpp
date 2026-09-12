/*
 * MicroCity Meshbus MBA port layer.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <desktop/desktop.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

#include <meshbus_arduboy/llext_game.hpp>
#include <meshbus_arduboy/storage.hpp>

#include "Draw.h"
#include "Game.h"
#include "Interface.h"

static_assert(sizeof(GameState) <= UINT16_MAX, "MicroCity save payload size field is too small");

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t fps = 25U;
constexpr size_t framebuffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT / 8U;
constexpr const char *save_id = "microcity";
constexpr uint32_t save_magic = 0x3143424dU; /* MBC1 */
constexpr uint16_t save_version = 1U;

struct SaveHeader {
	uint32_t magic;
	uint16_t version;
	uint16_t payload_size;
	uint32_t payload_crc;
};

struct MicroCityApp {
	meshbus::arduboy::llext_game::FullscreenState<MicroCityApp> runtime;
	uint8_t framebuffer[framebuffer_size];
	uint32_t buttons_down;
};

MicroCityApp *current_app;
char save_path[meshbus::arduboy::save_path_capacity];

uint32_t save_crc(const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	uint32_t hash = 2166136261U;

	for (size_t i = 0U; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619U;
	}

	return hash;
}

bool read_exact(struct fs_file_t *file, void *data, size_t size)
{
	ssize_t got = fs_read(file, data, size);

	return got == static_cast<ssize_t>(size);
}

bool write_exact(struct fs_file_t *file, const void *data, size_t size)
{
	ssize_t written = fs_write(file, data, size);

	return written == static_cast<ssize_t>(size);
}

void tick(MicroCityApp *app)
{
	app->buttons_down = app->runtime.actions.down;
	memset(app->framebuffer, 0, sizeof(app->framebuffer));
	TickGame();
}

uint8_t *framebuffer(MicroCityApp *app)
{
	return app->framebuffer;
}

void app_setup(MicroCityApp *app)
{
	ARG_UNUSED(app);
	(void)meshbus::arduboy::make_save_path(save_path, sizeof(save_path), save_id);
	InitGame();
}

void teardown(MicroCityApp *app)
{
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<MicroCityApp>,
	.input = meshbus::arduboy::llext_game::input<MicroCityApp>,
	.event = meshbus::arduboy::llext_game::event<MicroCityApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<MicroCityApp> game_config = {
	.log_tag = "microcity-app",
	.screen_id = screen_id,
	.fps = fps,
	.width = DISPLAY_WIDTH,
	.height = DISPLAY_HEIGHT,
	.stride = DISPLAY_WIDTH,
	.format = ZUI_BITMAP_FORMAT_MONO_VLSB,
	.exit_action_mask = ZUI_ACTION_CANCEL,
	.framebuffer = framebuffer,
	.setup = app_setup,
	.tick = tick,
	.idle = nullptr,
	.teardown = teardown,
};

} // namespace

uint8_t GetInput()
{
	uint8_t result = 0U;
	uint32_t down = current_app == nullptr ? 0U : current_app->buttons_down;

	if ((down & ZUI_ACTION_PRIMARY) != 0U) {
		result |= INPUT_A;
	}
	if ((down & ZUI_ACTION_SECONDARY) != 0U) {
		result |= INPUT_B;
	}
	if ((down & ZUI_ACTION_UP) != 0U) {
		result |= INPUT_UP;
	}
	if ((down & ZUI_ACTION_DOWN) != 0U) {
		result |= INPUT_DOWN;
	}
	if ((down & ZUI_ACTION_LEFT) != 0U) {
		result |= INPUT_LEFT;
	}
	if ((down & ZUI_ACTION_RIGHT) != 0U) {
		result |= INPUT_RIGHT;
	}

	return result;
}

void PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
	size_t index;
	uint8_t mask;

	if (current_app == nullptr || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		return;
	}

	index = (static_cast<size_t>(y) / 8U) * DISPLAY_WIDTH + x;
	mask = static_cast<uint8_t>(BIT(y & 0x7U));
	if (colour != 0U) {
		current_app->framebuffer[index] |= mask;
	} else {
		current_app->framebuffer[index] &= static_cast<uint8_t>(~mask);
	}
}

void DrawBitmap(const uint8_t *bmp, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	if (bmp == nullptr) {
		return;
	}

	for (uint8_t py = 0U; py < h; py++) {
		for (uint8_t px = 0U; px < w; px++) {
			if (zui_bitmap_bit_get(ZUI_BITMAP_FORMAT_MONO_VLSB, bmp, w, h, px, py)) {
				PutPixel(static_cast<uint8_t>(x + px), static_cast<uint8_t>(y + py), 1U);
			}
		}
	}
}

void SaveCity()
{
	struct fs_file_t file;
	SaveHeader header = {};

	header.magic = save_magic;
	header.version = save_version;
	header.payload_size = static_cast<uint16_t>(sizeof(GameState));
	header.payload_crc = save_crc(&State, sizeof(State));

	if (save_path[0] == '\0' || meshbus::arduboy::ensure_save_dir() != 0) {
		return;
	}

	fs_file_t_init(&file);
	if (fs_open(&file, save_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) != 0) {
		return;
	}

	if (!write_exact(&file, &header, sizeof(header)) ||
	    !write_exact(&file, &State, sizeof(State))) {
		(void)fs_close(&file);
		(void)fs_unlink(save_path);
		return;
	}

	(void)fs_close(&file);
}

bool LoadCity()
{
	struct fs_file_t file;
	SaveHeader header;
	GameState loaded;
	bool ok = false;

	if (save_path[0] == '\0') {
		return false;
	}

	fs_file_t_init(&file);
	if (fs_open(&file, save_path, FS_O_READ) != 0) {
		return false;
	}

	if (read_exact(&file, &header, sizeof(header)) &&
	    header.magic == save_magic &&
	    header.version == save_version &&
	    header.payload_size == static_cast<uint16_t>(sizeof(GameState)) &&
	    read_exact(&file, &loaded, sizeof(loaded)) &&
	    header.payload_crc == save_crc(&loaded, sizeof(loaded))) {
		memcpy(&State, &loaded, sizeof(State));
		ok = true;
	}

	(void)fs_close(&file);
	return ok;
}

uint8_t *GetPowerGrid()
{
	return current_app == nullptr ? nullptr : current_app->framebuffer;
}

extern "C" void microcity_app_main(void *args)
{
	MicroCityApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(microcity_app_main);

#include "../upstream/Building.cpp"
#include "../upstream/Connectivity.cpp"
#include "../upstream/Draw.cpp"
#include "../upstream/Font.cpp"
#include "../upstream/Game.cpp"
#include "../upstream/Interface.cpp"
#include "../upstream/Simulation.cpp"
#include "../upstream/Strings.cpp"
#include "../upstream/Terrain.cpp"
