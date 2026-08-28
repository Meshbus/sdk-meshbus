/*
 * Hollow Meshbus MBA port layer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/meshbus/indicator.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/zui/zui.h>

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
constexpr const char *save_id = "hollow";
constexpr size_t max_score_notes = 32U;
constexpr size_t score_buffer_count = 3U;

struct HollowApp {
	meshbus::arduboy::llext_game::FullscreenState<HollowApp> runtime;
	uint8_t framebuffer[framebuffer_size];
	uint8_t eeprom_data[eeprom_size];
	uint8_t buttons;
};

struct ScoreBuffer {
	indicator_buzzer_note notes[max_score_notes];
	indicator_buzzer_melody melody;
};

HollowApp *current_app;
ScoreBuffer score_buffers[score_buffer_count];
uint8_t next_score_buffer;
uint32_t rng_state = 0x484c4c57U; /* HLLW */
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

void tick(HollowApp *app)
{
	app->buttons = meshbus::arduboy::buttons_from_zui(app->runtime.actions.down, true);
	loop();
}

uint8_t *framebuffer(HollowApp *app)
{
	return app->framebuffer;
}

uint8_t *eeprom_data(HollowApp *app)
{
	return app->eeprom_data;
}

void app_setup(HollowApp *app)
{
	ARG_UNUSED(app);
	meshbus_arduboy_random_seed(sys_rand32_get());
	setup();
}

void teardown(HollowApp *app)
{
	meshbus_arduboy_score_stop();
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<HollowApp>,
	.input = meshbus::arduboy::llext_game::input<HollowApp>,
	.event = meshbus::arduboy::llext_game::event<HollowApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<HollowApp> game_config = {
	.log_tag = "hollow-app",
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

	if (score == nullptr || !ArduboyAudio::enabled() || !meshbus_indicator_buzzer_is_ready()) {
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
	(void)meshbus_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &buffer->melody);
}

extern "C" void meshbus_arduboy_score_stop()
{
	meshbus_indicator_buzzer_stop();
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

int hollow_sprintf(char *buffer, const char *format, int value)
{
	if (format != nullptr && format[0] == '%' && format[1] == '5' && format[2] == 'd' &&
	    format[3] == '\0') {
		uint16_t v = static_cast<uint16_t>(value);

		for (size_t i = 0U; i < 5U; i++) {
			buffer[i] = ' ';
		}
		buffer[5] = '\0';
		for (int pos = 4; pos >= 0; pos--) {
			buffer[pos] = static_cast<char>('0' + (v % 10U));
			v /= 10U;
			if (v == 0U) {
				break;
			}
		}
		return 5;
	}

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
	/* Hollow uses rand() directly and expects AVR's 15-bit RAND_MAX. */
	return static_cast<int>(rng_state & 0x7fffU);
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

long hollow_sqrt(long value)
{
	long root = 0;

	while ((root + 1) * (root + 1) <= value) {
		root++;
	}
	return root;
}

uint8_t hollow_atan2_turn128(int y, int x)
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

int16_t hollow_sin_turn256(uint8_t turn)
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

int16_t hollow_cos_turn256(uint8_t turn)
{
	return hollow_sin_turn256(static_cast<uint8_t>(turn + 64U));
}

size_t hollow_strnlen(const char *str, size_t max_len)
{
	size_t len = 0U;

	while (len < max_len && str[len] != '\0') {
		len++;
	}
	return len;
}

extern "C" void hollow_app_main(void *args)
{
	HollowApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(hollow_app_main);

int hollow_cave_gap(uint16_t phase, uint8_t max_gap)
{
	phase &= 0x3ffU;
	uint16_t distance = phase <= 512U ? phase : static_cast<uint16_t>(1024U - phase);
	uint32_t factor = (static_cast<uint32_t>(distance) * (1024U - distance)) / 256U;

	return static_cast<int>((static_cast<uint32_t>(max_gap) * factor + 512U) / 1024U);
}

struct HollowCosExpr {
	uint16_t phase;
};

struct HollowGapHalfExpr {
	uint16_t phase;
};

struct HollowGapFactorExpr {
	uint16_t phase;
};

static inline HollowGapHalfExpr operator/(HollowCosExpr value, double)
{
	return HollowGapHalfExpr{value.phase};
}

static inline HollowGapFactorExpr operator-(double, HollowGapHalfExpr value)
{
	return HollowGapFactorExpr{value.phase};
}

static inline int operator*(HollowGapFactorExpr value, uint8_t max_gap)
{
	return hollow_cave_gap(value.phase, max_gap);
}

#include "../upstream/MyArduboy.cpp"
#include "../upstream/hollow.ino"
#define counter hollow_logo_counter
#define signalOn hollow_logo_signal_on
#include "../upstream/logo.cpp"
#undef signalOn
#undef counter
#define state hollow_title_state
#define toDraw hollow_title_to_draw
#define strnlen hollow_strnlen
#define sprintf hollow_sprintf
#include "../upstream/title.cpp"
#undef sprintf
#undef strnlen
#undef toDraw
#undef state
#define state hollow_game_state
#define toDraw hollow_game_to_draw
#define counter hollow_game_counter
#define cos(expr) HollowCosExpr{static_cast<uint16_t>(cavePhase)}
#include "../upstream/game.cpp"
#undef cos
#undef counter
#undef toDraw
#undef state
