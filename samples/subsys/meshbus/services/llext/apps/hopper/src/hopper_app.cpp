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


// These pinned upstream files use byte only for score words and parameters.
// Keep the historic 260-ms low word without changing Arduino byte globally.
#define byte meshbus::arduboy::LegacyScoreWordV1

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

#undef byte
static const meshbus::arduboy::LegacyScoreSpanV1 scores[] = {
    {sound1, sizeof(sound1)/sizeof(sound1[0])},
    {sound2, sizeof(sound2)/sizeof(sound2[0])},
    {soundStart, sizeof(soundStart)/sizeof(soundStart[0])},
    {soundJump, sizeof(soundJump)/sizeof(soundJump[0])},
    {soundClear, sizeof(soundClear)/sizeof(soundClear[0])},
    {soundOver, sizeof(soundOver)/sizeof(soundOver[0])},
};
static void port_loop() {
    static int previous = -1;
    loop();
    if (previous != mode) {
        previous = mode;
        printk("[hopper] mode=%u audio=%d playing=%u\n", unsigned(mode),
            meshbus_arduboy_audio_status(), meshbus_arduboy_tone_playing());
    }
}
static void port_setup() {
    meshbus::arduboy::register_legacy_scores_v1(scores, sizeof(scores)/sizeof(scores[0]));
    setup();
}
extern "C" void hopper_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"hopper", port_setup, port_loop};
#ifdef ARDUBOY_TEST_SAVE_ID
    config.save_id = ARDUBOY_TEST_SAVE_ID;
#endif
    config.migrate = meshbus::arduboy::legacy_saves::hopper;
    int rc = meshbus::arduboy::run_sketch(args, config);
    printk("[hopper] complete rc=%d audio=%d playing=%u\n", rc,
           meshbus_arduboy_audio_status(), meshbus_arduboy_tone_playing());
}
LL_EXTENSION_SYMBOL(hopper_app_main);
