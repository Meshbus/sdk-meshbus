/* SPDX-License-Identifier: GPL-3.0-only */
#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>
#include <meshbus_arduboy/runtime.hpp>
#include <meshbus_arduboy/compat.hpp>
#include <meshbus_arduboy/storage.hpp>
#include "Draw.h"
#include "Game.h"
#include "Interface.h"
static_assert(sizeof(GameState)<=UINT16_MAX);
namespace {
constexpr const char *save_id = "microcity";
constexpr uint32_t save_magic = 0x3143424dU; /* MBC1 */
constexpr uint16_t save_version = 1U;

struct SaveHeader {
	uint32_t magic;
	uint16_t version;
	uint16_t payload_size;
	uint32_t payload_crc;
};

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

}
uint8_t GetInput()
{
	uint8_t result = 0U;
	uint32_t down = meshbus_arduboy_buttons();

	if ((down & A_BUTTON) != 0U) {
		result |= INPUT_A;
	}
	if ((down & B_BUTTON) != 0U) {
		result |= INPUT_B;
	}
	if ((down & UP_BUTTON) != 0U) {
		result |= INPUT_UP;
	}
	if ((down & DOWN_BUTTON) != 0U) {
		result |= INPUT_DOWN;
	}
	if ((down & LEFT_BUTTON) != 0U) {
		result |= INPUT_LEFT;
	}
	if ((down & RIGHT_BUTTON) != 0U) {
		result |= INPUT_RIGHT;
	}

	return result;
}

void PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
	size_t index;
	uint8_t mask;

	if (meshbus_arduboy_framebuffer() == nullptr || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
		return;
	}

	index = (static_cast<size_t>(y) / 8U) * DISPLAY_WIDTH + x;
	mask = static_cast<uint8_t>(BIT(y & 0x7U));
	if (colour != 0U) {
		meshbus_arduboy_framebuffer()[index] |= mask;
	} else {
		meshbus_arduboy_framebuffer()[index] &= static_cast<uint8_t>(~mask);
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
    SaveHeader header{save_magic, save_version, static_cast<uint16_t>(sizeof(State)), save_crc(&State, sizeof(State))};
    int rc = meshbus::arduboy::storage_detail::commit_snapshot_parts(save_path,
        reinterpret_cast<const uint8_t *>(&header), sizeof(header),
        reinterpret_cast<const uint8_t *>(&State), sizeof(State));
    if(rc) printk("[microcity] save failed: %d\n",rc);
}

bool LoadCity()
{
	struct fs_file_t file;
	SaveHeader header;
	static GameState loaded; // Game-owner scratch, not the shared 4 KiB stack.
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
	return meshbus_arduboy_framebuffer() == nullptr ? nullptr : meshbus_arduboy_framebuffer();
}

static void setup_city() {
    (void)meshbus::arduboy::make_save_path(save_path,sizeof(save_path),save_id);
    InitGame();
}
static void loop_city() {
    memset(meshbus_arduboy_framebuffer(),0,1024);
    TickGame();
    meshbus_arduboy_display(false);
}
extern "C" void microcity_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"microcity",setup_city,loop_city};
    config.fps=25;config.frame_gated=false;config.save_id="microcity_audio";
    int rc=meshbus::arduboy::run_sketch(args,config);
    printk("[microcity] complete rc=%d\n",rc);
}
LL_EXTENSION_SYMBOL(microcity_app_main);
