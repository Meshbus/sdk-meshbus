/*
 * Arduboy3D Meshbus MBA port layer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <desktop/desktop.h>
#include <indicator/indicator.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

#include <ArduboyTones.h>
#include <meshbus_arduboy/llext_game.hpp>

#include <Arduboy2.h>

void setup();
void loop();
extern Arduboy2Base arduboy;

bool Arduboy2Audio::enabled_state_ = true;

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t fps = 30U;
constexpr uint16_t tick_millis = 34U;
constexpr size_t framebuffer_size = WIDTH * HEIGHT / 8U;
constexpr size_t max_tone_notes = 48U;
constexpr size_t tone_buffer_count = 3U;

struct Arduboy3dApp {
	meshbus::arduboy::llext_game::FullscreenState<Arduboy3dApp> runtime;
	uint8_t framebuffer[framebuffer_size];
	uint8_t buttons;
	uint32_t time_ms;
	uint32_t rng_state;
};

struct ToneBuffer {
	indicator_buzzer_note notes[max_tone_notes];
	indicator_buzzer_melody melody;
};

Arduboy3dApp *current_app;
ToneBuffer tone_buffers[tone_buffer_count];
uint8_t next_tone_buffer;

uint16_t tone_frequency(uint16_t freq)
{
	if (freq == TONES_END || freq == TONES_REPEAT) {
		return freq;
	}
	return static_cast<uint16_t>(freq & ~TONE_HIGH_VOLUME);
}

bool tone_audio_enabled(bool (*enabled_cb)())
{
	return enabled_cb == nullptr || enabled_cb();
}

ToneBuffer *tone_buffer_next()
{
	ToneBuffer *buffer = &tone_buffers[next_tone_buffer];

	next_tone_buffer++;
	if (next_tone_buffer >= tone_buffer_count) {
		next_tone_buffer = 0U;
	}
	return buffer;
}

void tone_play_notes(const indicator_buzzer_note *notes, size_t note_count)
{
	if (notes == nullptr || note_count == 0U || !mbs_indicator_buzzer_is_ready()) {
		return;
	}

	ToneBuffer *buffer = tone_buffer_next();

	note_count = MIN(note_count, max_tone_notes);
	memcpy(buffer->notes, notes, note_count * sizeof(buffer->notes[0]));
	buffer->melody.notes = buffer->notes;
	buffer->melody.length = static_cast<uint8_t>(note_count);
	(void)mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &buffer->melody);
}

void tick(Arduboy3dApp *app)
{
	app->buttons = meshbus::arduboy::buttons_from_zui(app->runtime.actions.down, true);
	app->time_ms += tick_millis;
	arduboy.pollButtons();
	loop();
}

uint8_t *framebuffer(Arduboy3dApp *app)
{
	return app->framebuffer;
}

void app_setup(Arduboy3dApp *app)
{
	current_app = app;
	app->time_ms = 0U;
	app->buttons = 0U;
	app->rng_state = sys_rand32_get();
	if (app->rng_state == 0U) {
		app->rng_state = 0x33444241U; /* 3DBA */
	}
	memset(app->framebuffer, 0, sizeof(app->framebuffer));
	Arduboy2Audio::on();
	setup();
}

void teardown(Arduboy3dApp *app)
{
	meshbus_arduboy_tone_stop();
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<Arduboy3dApp>,
	.input = meshbus::arduboy::llext_game::input<Arduboy3dApp>,
	.event = meshbus::arduboy::llext_game::event<Arduboy3dApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<Arduboy3dApp> game_config = {
	.log_tag = "arduboy3d-app",
	.screen_id = screen_id,
	.fps = fps,
	.width = WIDTH,
	.height = HEIGHT,
	.stride = WIDTH,
	.format = ZUI_BITMAP_FORMAT_MONO_VLSB,
	.exit_action_mask = ZUI_ACTION_CANCEL,
	.framebuffer = framebuffer,
	.setup = app_setup,
	.tick = tick,
	.idle = nullptr,
	.teardown = teardown,
};

} /* namespace */

extern "C" uint8_t *meshbus_arduboy_framebuffer()
{
	return current_app == nullptr ? nullptr : current_app->framebuffer;
}

extern "C" uint8_t meshbus_arduboy_buttons()
{
	return current_app == nullptr ? 0U : current_app->buttons;
}

extern "C" uint32_t meshbus_arduboy_millis()
{
	return current_app == nullptr ? 0U : current_app->time_ms;
}

extern "C" uint32_t meshbus_arduboy_micros()
{
	return meshbus_arduboy_millis() * 1000U;
}

extern "C" void meshbus_arduboy_random_seed(uint32_t seed)
{
	if (current_app != nullptr && seed != 0U) {
		current_app->rng_state = seed;
	}
}

extern "C" long meshbus_arduboy_random(long max)
{
	if (current_app == nullptr || max <= 0) {
		return 0;
	}

	current_app->rng_state ^= current_app->rng_state << 13;
	current_app->rng_state ^= current_app->rng_state >> 17;
	current_app->rng_state ^= current_app->rng_state << 5;
	return static_cast<long>(current_app->rng_state % static_cast<uint32_t>(max));
}

extern "C" long meshbus_arduboy_random_range(long min, long max)
{
	if (max <= min) {
		return min;
	}
	return min + meshbus_arduboy_random(max - min);
}

extern "C" void meshbus_arduboy_tone_play(uint16_t freq, uint16_t duration_ms,
					bool (*enabled_cb)())
{
	if (!tone_audio_enabled(enabled_cb)) {
		return;
	}

	indicator_buzzer_note note = {
		.freq_hz = tone_frequency(freq),
		.duration_ms = static_cast<uint16_t>(duration_ms == 0U ? 20U : duration_ms),
	};

	if (note.freq_hz == TONES_END || note.freq_hz == TONES_REPEAT) {
		return;
	}
	tone_play_notes(&note, 1U);
}

extern "C" void meshbus_arduboy_tones_play_pairs(const uint16_t *pairs, size_t pair_value_count,
					       bool (*enabled_cb)())
{
	indicator_buzzer_note notes[max_tone_notes];
	size_t note_count = 0U;

	if (pairs == nullptr || pair_value_count < 2U || !tone_audio_enabled(enabled_cb)) {
		return;
	}

	for (size_t i = 0U; i + 1U < pair_value_count && note_count < max_tone_notes; i += 2U) {
		uint16_t freq = tone_frequency(pairs[i]);

		if (freq == TONES_END || freq == TONES_REPEAT) {
			break;
		}

		notes[note_count++] = {
			.freq_hz = freq,
			.duration_ms = static_cast<uint16_t>(pairs[i + 1U] == 0U ? 20U : pairs[i + 1U]),
		};
	}

	tone_play_notes(notes, note_count);
}

extern "C" void meshbus_arduboy_tones_play_score(const uint16_t *score, bool (*enabled_cb)())
{
	indicator_buzzer_note notes[max_tone_notes];
	size_t note_count = 0U;

	if (score == nullptr || !tone_audio_enabled(enabled_cb)) {
		return;
	}

	while (note_count < max_tone_notes) {
		uint16_t freq = tone_frequency(pgm_read_word(score++));

		if (freq == TONES_END || freq == TONES_REPEAT) {
			break;
		}

		uint16_t duration = pgm_read_word(score++);

		notes[note_count++] = {
			.freq_hz = freq,
			.duration_ms = static_cast<uint16_t>(duration == 0U ? 20U : duration),
		};
	}

	tone_play_notes(notes, note_count);
}

extern "C" void meshbus_arduboy_tone_stop()
{
	mbs_indicator_buzzer_stop();
}

extern "C" void arduboy3d_app_main(void *args)
{
	Arduboy3dApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(arduboy3d_app_main);

#include "../upstream/Arduboy3D.ino"
#include "../upstream/Draw.cpp"

#define ARDUBOY3D_SPRITE_FRAME_PREFIX arduboy3d_enemy
#include "SpriteFrameCountAliases.h"
#include "../upstream/Enemy.cpp"
#include "SpriteFrameCountAliasesUndef.h"

#include "../upstream/Entity.cpp"
#include "../upstream/FixedMath.cpp"

#define ARDUBOY3D_SPRITE_FRAME_PREFIX arduboy3d_font
#include "SpriteFrameCountAliases.h"
#include "../upstream/Font.cpp"
#include "SpriteFrameCountAliasesUndef.h"

#include "../upstream/Game.cpp"
#include "../upstream/Map.cpp"
#include "../upstream/MapGenerator.cpp"

#define ARDUBOY3D_SPRITE_FRAME_PREFIX arduboy3d_menu
#include "SpriteFrameCountAliases.h"
#include "../upstream/Menu.cpp"
#include "SpriteFrameCountAliasesUndef.h"

#include "../upstream/Particle.cpp"
#include "../upstream/Player.cpp"

#define ARDUBOY3D_SPRITE_FRAME_PREFIX arduboy3d_projectile
#include "SpriteFrameCountAliases.h"
#include "../upstream/Projectile.cpp"
#include "SpriteFrameCountAliasesUndef.h"

#include "../upstream/Sounds.cpp"
