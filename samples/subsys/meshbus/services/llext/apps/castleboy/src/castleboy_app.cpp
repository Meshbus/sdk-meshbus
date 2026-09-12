/*
 * CastleBoy Meshbus MBA port layer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <desktop/desktop.h>
#include <indicator/indicator.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

#include <meshbus_arduboy/llext_game.hpp>

#include <Arduboy2.h>
#include "global.h"

void setup();
void loop();
void castleboy_tones_play_pairs(const uint16_t *pairs, size_t pair_value_count,
				bool (*enabled_cb)());
void castleboy_tones_play_score(const uint16_t *score, bool (*enabled_cb)());
void castleboy_tones_stop();

bool Arduboy2Audio::enabled_state_ = true;

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t fps = 60U;
constexpr size_t framebuffer_size = WIDTH * HEIGHT / 8U;
constexpr size_t tone_buffer_count = 3U;
constexpr size_t max_tone_notes = 32U;
constexpr uint32_t duplicate_short_tone_suppress_frames = 2U;

struct CastleBoyApp {
	meshbus::arduboy::llext_game::FullscreenState<CastleBoyApp> runtime;
	uint8_t framebuffer[framebuffer_size];
	uint8_t buttons;
};

struct ToneBuffer {
	indicator_buzzer_note notes[max_tone_notes];
	indicator_buzzer_melody melody;
};

CastleBoyApp *current_app;
ToneBuffer tone_buffers[tone_buffer_count];
uint8_t next_tone_buffer;
uint16_t last_short_freq;
uint16_t last_short_duration;
uint32_t last_short_frame;
uint32_t tone_frame;

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

bool tone_duplicate_should_drop(const indicator_buzzer_note *notes, size_t note_count)
{
	if (note_count != 1U || notes[0].duration_ms > 30U) {
		return false;
	}

	bool duplicate = notes[0].freq_hz == last_short_freq &&
			 notes[0].duration_ms == last_short_duration &&
			 tone_frame - last_short_frame < duplicate_short_tone_suppress_frames;

	if (!duplicate) {
		last_short_freq = notes[0].freq_hz;
		last_short_duration = notes[0].duration_ms;
		last_short_frame = tone_frame;
	}

	return duplicate;
}

void tone_play_notes(const indicator_buzzer_note *notes, size_t note_count)
{
	ToneBuffer *buffer;

	if (notes == nullptr || note_count == 0U || !mbs_indicator_buzzer_is_ready() ||
	    tone_duplicate_should_drop(notes, note_count)) {
		return;
	}

	buffer = tone_buffer_next();
	note_count = MIN(note_count, max_tone_notes);
	memcpy(buffer->notes, notes, note_count * sizeof(buffer->notes[0]));
	buffer->melody.notes = buffer->notes;
	buffer->melody.length = static_cast<uint8_t>(note_count);

	(void)mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &buffer->melody);
}

void tick(CastleBoyApp *app)
{
	app->buttons = meshbus::arduboy::buttons_from_zui(app->runtime.actions.down);
	tone_frame++;
	loop();
}

uint8_t *framebuffer(CastleBoyApp *app)
{
	return app->framebuffer;
}

void app_setup(CastleBoyApp *app)
{
	ARG_UNUSED(app);
	setup();
}

void teardown(CastleBoyApp *app)
{
	meshbus_arduboy_tone_stop();
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<CastleBoyApp>,
	.input = meshbus::arduboy::llext_game::input<CastleBoyApp>,
	.event = meshbus::arduboy::llext_game::event<CastleBoyApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<CastleBoyApp> game_config = {
	.log_tag = "castleboy-app",
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

} // namespace

extern "C" int __heap_start;
extern "C" int *__brkval;
int __heap_start;
int *__brkval;

extern "C" uint8_t *meshbus_arduboy_framebuffer()
{
	return current_app == nullptr ? nullptr : current_app->framebuffer;
}

extern "C" uint8_t meshbus_arduboy_buttons()
{
	return current_app == nullptr ? 0U : current_app->buttons;
}

void castleboy_tones_play_pairs(const uint16_t *pairs, size_t pair_value_count,
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
			.duration_ms = pairs[i + 1U],
		};
	}

	tone_play_notes(notes, note_count);
}

void castleboy_tones_play_score(const uint16_t *score, bool (*enabled_cb)())
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

		notes[note_count++] = {
			.freq_hz = freq,
			.duration_ms = pgm_read_word(score++),
		};
	}

	tone_play_notes(notes, note_count);
}

void castleboy_tones_stop()
{
	mbs_indicator_buzzer_stop();
}

extern "C" void meshbus_arduboy_tone_play(uint16_t freq, uint16_t duration_ms,
					bool (*enabled_cb)())
{
	const uint16_t pairs[] = {freq, duration_ms};

	castleboy_tones_play_pairs(pairs, sizeof(pairs) / sizeof(pairs[0]), enabled_cb);
}

extern "C" void meshbus_arduboy_tones_play_pairs(const uint16_t *pairs,
					       size_t pair_value_count,
					       bool (*enabled_cb)())
{
	castleboy_tones_play_pairs(pairs, pair_value_count, enabled_cb);
}

extern "C" void meshbus_arduboy_tones_play_score(const uint16_t *score,
					       bool (*enabled_cb)())
{
	castleboy_tones_play_score(score, enabled_cb);
}

extern "C" void meshbus_arduboy_tone_stop()
{
	castleboy_tones_stop();
}

extern "C" void castleboy_app_main(void *args)
{
	CastleBoyApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(castleboy_app_main);

#include "../upstream/CastleBoy.ino"
#include "../upstream/global.cpp"
#include "../upstream/menu.cpp"
#include "../upstream/game.cpp"
#include "../upstream/map.cpp"
#include "../upstream/entity.cpp"
#include "../upstream/player.cpp"
