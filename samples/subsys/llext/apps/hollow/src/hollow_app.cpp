/* SPDX-License-Identifier: MIT */
#include <Arduboy.h>
#include <meshbus_arduboy/runtime.hpp>
#include <meshbus_arduboy/legacy_score.hpp>
#include <meshbus_arduboy/legacy_saves.hpp>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/llext/symbol.h>
extern "C" int meshbus_arduboy_audio_status();
bool ArduboyAudio::audio_enabled = true;
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


// These pinned upstream files use byte only for score words and parameters.
// Keep the historic 260-ms low word without changing Arduino byte globally.
#define byte meshbus::arduboy::LegacyScoreWordV1

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

#undef byte
static const meshbus::arduboy::LegacyScoreSpanV1 scores[] = {
    {sound1, sizeof(sound1)/sizeof(sound1[0])},
    {sound2, sizeof(sound2)/sizeof(sound2[0])},
    {soundStart, sizeof(soundStart)/sizeof(soundStart[0])},
    {soundMove, sizeof(soundMove)/sizeof(soundMove[0])},
    {soundCrush, sizeof(soundCrush)/sizeof(soundCrush[0])},
    {soundGameOver, sizeof(soundGameOver)/sizeof(soundGameOver[0])},
};
static void port_loop() {
    static int previous = -1;
    loop();
    if (previous != mode) {
        previous = mode;
        printk("[hollow] mode=%u audio=%d playing=%u\n", unsigned(mode),
            meshbus_arduboy_audio_status(), meshbus_arduboy_tone_playing());
    }
}
static void port_setup() {
    meshbus::arduboy::register_legacy_scores_v1(scores, sizeof(scores)/sizeof(scores[0]));
    setup();
}
extern "C" void hollow_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"hollow", port_setup, port_loop};
#ifdef ARDUBOY_TEST_SAVE_ID
    config.save_id = ARDUBOY_TEST_SAVE_ID;
#endif
    config.migrate = meshbus::arduboy::legacy_saves::hollow;
    int rc = meshbus::arduboy::run_sketch(args, config);
    printk("[hollow] complete rc=%d audio=%d playing=%u\n", rc,
           meshbus_arduboy_audio_status(), meshbus_arduboy_tone_playing());
}
LL_EXTENSION_SYMBOL(hollow_app_main);
