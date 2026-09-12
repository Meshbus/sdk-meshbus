/*
 * Hopper Meshbus MBA port layer.
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
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>

#include <meshbus_arduboy/llext_game.hpp>

#include <Arduboy.h>

void setup();
void loop();

bool ArduboyAudio::audio_enabled = true;

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t fps = 60U;
constexpr size_t framebuffer_size = WIDTH * HEIGHT / 8U;
constexpr size_t eeprom_size = 1024U;
constexpr const char *save_id = "hopper";
constexpr size_t max_score_notes = 32U;
constexpr size_t score_buffer_count = 3U;

struct HopperApp {
	meshbus::arduboy::llext_game::FullscreenState<HopperApp> runtime;
	uint8_t framebuffer[framebuffer_size];
	uint8_t eeprom_data[eeprom_size];
	uint8_t buttons;
};

struct ScoreBuffer {
	indicator_buzzer_note notes[max_score_notes];
	indicator_buzzer_melody melody;
};

HopperApp *current_app;
ScoreBuffer score_buffers[score_buffer_count];
uint8_t next_score_buffer;
uint32_t rng_state = 0x484f5052U; /* HOPR */
uint32_t micros_counter;

uint16_t midi_note_to_freq(uint8_t note)
{
	static const uint8_t base_freqs[] = {8, 9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 15};
	uint16_t freq = base_freqs[note % 12U];

	for (uint8_t octave = note / 12U; octave > 0U && freq < 30000U; octave--) {
		freq = static_cast<uint16_t>(freq * 2U);
	}
	return freq;
}

ScoreBuffer *score_buffer_next()
{
	ScoreBuffer *buffer = &score_buffers[next_score_buffer];

	next_score_buffer++;
	if (next_score_buffer >= score_buffer_count) {
		next_score_buffer = 0U;
	}
	return buffer;
}

bool score_read_delay(const byte *score, size_t *offset, uint16_t *delay)
{
	uint16_t msb = score[*offset];

	if (msb == 0xf0U || msb == 0xe0U || (msb & 0xf0U) == 0x90U || (msb & 0xf0U) == 0x80U) {
		*delay = 20U;
		return false;
	}

	uint16_t lsb = score[*offset + 1U];

	*offset += 2U;
	*delay = static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
	if (*delay == 0U) {
		*delay = 20U;
	}
	return true;
}

void tick(HopperApp *app)
{
	app->buttons = meshbus::arduboy::buttons_from_zui(app->runtime.actions.down, true);
	loop();
}

uint8_t *framebuffer(HopperApp *app)
{
	return app->framebuffer;
}

uint8_t *eeprom_data(HopperApp *app)
{
	return app->eeprom_data;
}

void app_setup(HopperApp *app)
{
	ARG_UNUSED(app);
	meshbus_arduboy_random_seed(sys_rand32_get());
	setup();
}

void teardown(HopperApp *app)
{
	meshbus_arduboy_score_stop();
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<HopperApp>,
	.input = meshbus::arduboy::llext_game::input<HopperApp>,
	.event = meshbus::arduboy::llext_game::event<HopperApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<HopperApp> game_config = {
	.log_tag = "hopper-app",
	.screen_id = screen_id,
	.fps = fps,
	.width = WIDTH,
	.height = HEIGHT,
	.stride = WIDTH,
	.format = ZUI_BITMAP_FORMAT_MONO_VLSB,
	.exit_action_mask = ZUI_ACTION_CANCEL,
	.framebuffer = framebuffer,
	.eeprom = {
		.save_id = save_id,
		.data = eeprom_data,
		.size = eeprom_size,
	},
	.setup = app_setup,
	.tick = tick,
	.idle = nullptr,
	.teardown = teardown,
};

} // namespace

extern "C" uint8_t *meshbus_arduboy_framebuffer()
{
	return current_app == nullptr ? nullptr : current_app->framebuffer;
}

extern "C" uint8_t meshbus_arduboy_buttons()
{
	return current_app == nullptr ? 0U : current_app->buttons;
}

extern "C" void meshbus_arduboy_score_play(const uint16_t *score)
{
	indicator_buzzer_note notes[max_score_notes];
	size_t note_count = 0U;
	size_t offset = 0U;

	if (score == nullptr || !ArduboyAudio::enabled() || !mbs_indicator_buzzer_is_ready()) {
		return;
	}

	while (note_count < max_score_notes) {
		uint16_t cmd = score[offset++];

		if (cmd == 0xf0U || cmd == 0xe0U) {
			break;
		}

		if ((cmd & 0xf0U) == 0x90U) {
			uint8_t note = static_cast<uint8_t>(score[offset++]);
			uint16_t duration;

			(void)score_read_delay(score, &offset, &duration);
			notes[note_count++] = {
				.freq_hz = midi_note_to_freq(note),
				.duration_ms = duration,
			};
		} else if ((cmd & 0xf0U) == 0x80U) {
			uint16_t delay;

			(void)score_read_delay(score, &offset, &delay);
		}
	}

	if (note_count == 0U) {
		return;
	}

	ScoreBuffer *buffer = score_buffer_next();
	memcpy(buffer->notes, notes, note_count * sizeof(buffer->notes[0]));
	buffer->melody.notes = buffer->notes;
	buffer->melody.length = static_cast<uint8_t>(note_count);
	(void)mbs_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &buffer->melody);
}

extern "C" void meshbus_arduboy_score_stop()
{
	mbs_indicator_buzzer_stop();
}

extern "C" uint32_t meshbus_arduboy_micros()
{
	micros_counter += 16667U;
	return micros_counter;
}

extern "C" uint32_t meshbus_arduboy_millis()
{
	return meshbus_arduboy_micros() / 1000U;
}

int hopper_sprintf(char *buffer, const char *format, int value)
{
	ARG_UNUSED(format);

	buffer[0] = '\'';
	buffer[1] = static_cast<char>('0' + ((value / 10) % 10));
	buffer[2] = static_cast<char>('0' + (value % 10));
	buffer[3] = '"';
	buffer[4] = '\0';
	return 4;
}

extern "C" void meshbus_arduboy_random_seed(uint32_t seed)
{
	if (seed != 0U) {
		rng_state = seed;
	}
}

extern "C" int meshbus_arduboy_rand()
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return static_cast<int>(rng_state & 0x7fffffffU);
}

extern "C" long meshbus_arduboy_random(long max)
{
	if (max <= 0) {
		return 0;
	}
	return static_cast<long>(static_cast<uint32_t>(meshbus_arduboy_rand()) %
				 static_cast<uint32_t>(max));
}

extern "C" long meshbus_arduboy_random_range(long min, long max)
{
	if (max <= min) {
		return min;
	}
	return min + meshbus_arduboy_random(max - min);
}

long hopper_sqrt(long value)
{
	long root = 0;

	while ((root + 1) * (root + 1) <= value) {
		root++;
	}
	return root;
}

uint8_t hopper_atan2_turn128(int y, int x)
{
	if (x > 0) {
		if (y > 0) {
			return 32U;
		}
		if (y < 0) {
			return 224U;
		}
		return 0U;
	}
	if (x < 0) {
		if (y > 0) {
			return 96U;
		}
		if (y < 0) {
			return 160U;
		}
		return 128U;
	}
	if (y > 0) {
		return 64U;
	}
	if (y < 0) {
		return 192U;
	}
	return 0U;
}

int16_t hopper_sin_turn256(uint8_t turn)
{
	uint8_t phase = turn & 0x3fU;

	if ((turn & 0x40U) != 0U) {
		phase = 64U - phase;
	}

	int32_t value = static_cast<int32_t>(phase) * static_cast<int32_t>(64U - phase);

	value /= 16;
	if ((turn & 0x80U) != 0U) {
		value = -value;
	}
	return static_cast<int16_t>(value);
}

int16_t hopper_cos_turn256(uint8_t turn)
{
	return hopper_sin_turn256(static_cast<uint8_t>(turn + 64U));
}

size_t hopper_strnlen(const char *str, size_t max_len)
{
	size_t len = 0U;

	while (len < max_len && str[len] != '\0') {
		len++;
	}
	return len;
}

extern "C" void hopper_app_main(void *args)
{
	HopperApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(hopper_app_main);

#define sqrt hopper_sqrt
#include "../upstream/MyArduboy.cpp"
#include "../upstream/hopper.ino"
#define counter hopper_logo_counter
#define signalOn hopper_logo_signal_on
#include "../upstream/logo.cpp"
#undef signalOn
#undef counter
#define state hopper_title_state
#define toDraw hopper_title_to_draw
#define strnlen hopper_strnlen
#define sprintf hopper_sprintf
#include "../upstream/title.cpp"
#undef sprintf
#undef strnlen
#undef toDraw
#undef state
#define state hopper_game_state
#define toDraw hopper_game_to_draw
#define counter hopper_game_counter
#include "../upstream/game.cpp"
#undef counter
#undef toDraw
#undef state
#undef sqrt
