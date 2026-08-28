/*
 * Ard Drivin Meshbus MBA port layer.
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

#include <ArduboyTones.h>

#include "../upstream/ArduboyRem.h"
#include "../upstream/pics/font.h"

#include <meshbus_arduboy/compat.hpp>
#include <meshbus_arduboy/llext_game.hpp>

void setup();
void loop();
extern "C" void meshbus_arduboy_tone_stop();

namespace {

constexpr uint32_t screen_id = 1U;
constexpr uint16_t game_fps = 67U;
constexpr size_t visible_framebuffer_size = WIDTH * HEIGHT / 8U;
constexpr size_t eeprom_size = 1024U;
constexpr const char *save_id = "ard_drivin";
constexpr uint32_t default_frame_micros = 1000000U / game_fps;
constexpr size_t tone_buffer_count = 3U;

struct ArdDrivinApp {
	meshbus::arduboy::llext_game::FullscreenState<ArdDrivinApp> runtime;
	uint8_t eeprom_data[eeprom_size];
	uint8_t buttons;
};

struct ToneBuffer {
	indicator_buzzer_note note;
	indicator_buzzer_melody melody;
};

ArdDrivinApp *current_app;
ToneBuffer tone_buffers[tone_buffer_count];
uint8_t next_tone_buffer;
uint32_t rng_state = 0x41524444U; /* ARDD */
uint32_t time_us;

uint8_t buttons_from_zui(uint32_t down)
{
	uint8_t buttons = 0U;

	if ((down & ZUI_ACTION_PRIMARY) != 0U) {
		buttons |= A_BUTTON;
	}
	if ((down & (ZUI_ACTION_SECONDARY | ZUI_ACTION_CANCEL)) != 0U) {
		buttons |= B_BUTTON;
	}
	if ((down & ZUI_ACTION_UP) != 0U) {
		buttons |= UP_BUTTON;
	}
	if ((down & ZUI_ACTION_DOWN) != 0U) {
		buttons |= DOWN_BUTTON;
	}
	if ((down & ZUI_ACTION_LEFT) != 0U) {
		buttons |= LEFT_BUTTON;
	}
	if ((down & ZUI_ACTION_RIGHT) != 0U) {
		buttons |= RIGHT_BUTTON;
	}

	return buttons;
}

ToneBuffer *next_tone()
{
	ToneBuffer *buffer = &tone_buffers[next_tone_buffer];

	next_tone_buffer++;
	if (next_tone_buffer >= tone_buffer_count) {
		next_tone_buffer = 0U;
	}
	return buffer;
}

void clear_for_next_frame()
{
	uint8_t alternate = (ArduboyCoreRem::flicker & 1U) != 0U ? 0x55U : 0xaaU;

	for (size_t i = 0U; i < sizeof(ArduboyBaseRem::sBuffer); i++) {
		ArduboyBaseRem::sBuffer[i] = alternate;
		alternate = static_cast<uint8_t>(~alternate);
	}
}

void tick(ArdDrivinApp *app)
{
	app->buttons = buttons_from_zui(app->runtime.actions.down);
	loop();
}

uint8_t *framebuffer(ArdDrivinApp *app)
{
	ARG_UNUSED(app);
	return ArduboyBaseRem::sBuffer;
}

uint8_t *eeprom_data(ArdDrivinApp *app)
{
	return app->eeprom_data;
}

void app_setup(ArdDrivinApp *app)
{
	current_app = app;
	time_us = 0U;
	meshbus_arduboy_random_seed(sys_rand32_get());
	memset(ArduboyBaseRem::sBuffer, 0, sizeof(ArduboyBaseRem::sBuffer));
	setup();
}

void teardown(ArdDrivinApp *app)
{
	meshbus_arduboy_tone_stop();
	if (current_app == app) {
		current_app = nullptr;
	}
}

const zui_screen_ops screen_ops = {
	.draw = meshbus::arduboy::llext_game::draw<ArdDrivinApp>,
	.input = meshbus::arduboy::llext_game::input<ArdDrivinApp>,
	.event = meshbus::arduboy::llext_game::event<ArdDrivinApp>,
};

const meshbus::arduboy::llext_game::FullscreenConfig<ArdDrivinApp> game_config = {
	.log_tag = "ard-drivin-app",
	.screen_id = screen_id,
	.fps = game_fps,
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

} /* namespace */

byte ArduboyCoreRem::flicker = 0;
uint8_t ArduboyBaseRem::sBuffer[((HEIGHT + 8) * WIDTH) / 8] = {};
bool ArduboyAudioRem::audio_enabled = false;

extern "C" uint8_t *meshbus_arduboy_framebuffer()
{
	return ArduboyBaseRem::sBuffer;
}

extern "C" uint32_t meshbus_arduboy_millis()
{
	return time_us / 1000U;
}

extern "C" uint32_t meshbus_arduboy_micros()
{
	return time_us;
}

extern "C" void meshbus_arduboy_random_seed(uint32_t seed)
{
	if (seed != 0U) {
		rng_state = seed;
	}
}

extern "C" long meshbus_arduboy_random(long max)
{
	if (max <= 0) {
		return 0;
	}

	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return static_cast<long>(rng_state % static_cast<uint32_t>(max));
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
	if ((enabled_cb != nullptr && !enabled_cb()) || freq == 0U ||
	    !meshbus_indicator_buzzer_is_ready()) {
		return;
	}

	ToneBuffer *buffer = next_tone();

	buffer->note = {
		.freq_hz = freq,
		.duration_ms = static_cast<uint16_t>(duration_ms == 0U ? 20U : duration_ms),
	};
	buffer->melody.notes = &buffer->note;
	buffer->melody.length = 1U;
	(void)meshbus_indicator_buzzer_play(INDICATOR_SOURCE_SYSTEM, &buffer->melody);
}

extern "C" void meshbus_arduboy_tone_stop()
{
	meshbus_indicator_buzzer_stop();
}

ArduboyCoreRem::ArduboyCoreRem() {}

void ArduboyCoreRem::idle() {}

void ArduboyCoreRem::LCDDataMode() {}

void ArduboyCoreRem::LCDCommandMode() {}

uint8_t ArduboyCoreRem::width()
{
	return WIDTH;
}

uint8_t ArduboyCoreRem::height()
{
	return HEIGHT;
}

void ArduboyCoreRem::paint8Pixels(uint8_t pixels)
{
	ARG_UNUSED(pixels);
}

void ArduboyCoreRem::paintScreen(const uint8_t *image)
{
	if (image == nullptr) {
		return;
	}
	memcpy(ArduboyBaseRem::sBuffer, image, visible_framebuffer_size);
}

void ArduboyCoreRem::paintScreen(uint8_t image[])
{
	ARG_UNUSED(image);
	clear_for_next_frame();
}

void ArduboyCoreRem::blank()
{
	memset(ArduboyBaseRem::sBuffer, 0, sizeof(ArduboyBaseRem::sBuffer));
}

void ArduboyCoreRem::invert(bool inverse)
{
	ARG_UNUSED(inverse);
}

void ArduboyCoreRem::allPixelsOn(bool on)
{
	ARG_UNUSED(on);
}

void ArduboyCoreRem::flipVertical(bool flipped)
{
	ARG_UNUSED(flipped);
}

void ArduboyCoreRem::flipHorizontal(bool flipped)
{
	ARG_UNUSED(flipped);
}

void ArduboyCoreRem::sendLCDCommand(uint8_t command)
{
	ARG_UNUSED(command);
}

void ArduboyCoreRem::setRGBled(uint8_t red, uint8_t green, uint8_t blue)
{
	ARG_UNUSED(red);
	ARG_UNUSED(green);
	ARG_UNUSED(blue);
}

void ArduboyCoreRem::digitalWriteRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	ARG_UNUSED(red);
	ARG_UNUSED(green);
	ARG_UNUSED(blue);
}

void ArduboyCoreRem::boot() {}

uint8_t ArduboyCoreRem::buttonsState()
{
	return current_app == nullptr ? 0U : current_app->buttons;
}

ArduboyBaseRem::ArduboyBaseRem()
{
	currentButtonState = 0U;
	previousButtonState = 0U;
	eachFrameMicros = default_frame_micros;
	nextFrameStart = 0U;
}

void ArduboyBaseRem::flashlight() {}

void ArduboyBaseRem::systemButtons() {}

void ArduboyBaseRem::clear()
{
	fillScreen(BLACK);
}

void ArduboyBaseRem::display()
{
	paintScreen(sBuffer);
}

void ArduboyBaseRem::drawPixel(int16_t x, int16_t y, uint8_t color) const
{
	if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) {
		return;
	}

	size_t index = (static_cast<size_t>(y) / 8U) * WIDTH + static_cast<size_t>(x);
	uint8_t mask = static_cast<uint8_t>(_BV(y & 0x7));

	if (color != BLACK) {
		sBuffer[index] |= mask;
	} else {
		sBuffer[index] &= static_cast<uint8_t>(~mask);
	}
}

uint8_t ArduboyBaseRem::getPixel(uint8_t x, uint8_t y) const
{
	if (x >= WIDTH || y >= HEIGHT) {
		return 0U;
	}

	size_t index = (static_cast<size_t>(y) / 8U) * WIDTH + x;
	uint8_t mask = static_cast<uint8_t>(_BV(y & 0x7));

	return (sBuffer[index] & mask) != 0U ? 1U : 0U;
}

void ArduboyBaseRem::drawCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color)
{
	ARG_UNUSED(x0);
	ARG_UNUSED(y0);
	ARG_UNUSED(r);
	ARG_UNUSED(color);
}

void ArduboyBaseRem::drawCircleHelper(int16_t x0, int16_t y0, uint8_t r,
				      uint8_t cornername, uint8_t color)
{
	ARG_UNUSED(x0);
	ARG_UNUSED(y0);
	ARG_UNUSED(r);
	ARG_UNUSED(cornername);
	ARG_UNUSED(color);
}

void ArduboyBaseRem::fillCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color)
{
	ARG_UNUSED(x0);
	ARG_UNUSED(y0);
	ARG_UNUSED(r);
	ARG_UNUSED(color);
}

void ArduboyBaseRem::fillCircleHelper(int16_t x0, int16_t y0, uint8_t r,
				      uint8_t cornername, int16_t delta, uint8_t color)
{
	ARG_UNUSED(x0);
	ARG_UNUSED(y0);
	ARG_UNUSED(r);
	ARG_UNUSED(cornername);
	ARG_UNUSED(delta);
	ARG_UNUSED(color);
}

void ArduboyBaseRem::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
			      uint8_t color)
{
	if (y0 == y1) {
		if (x1 < x0) {
			int16_t tmp = x0;

			x0 = x1;
			x1 = tmp;
		}
		drawFastHLine(x0, static_cast<uint8_t>(y0), x1 - x0 + 1, color);
		return;
	}
	if (x0 == x1) {
		if (y1 < y0) {
			int16_t tmp = y0;

			y0 = y1;
			y1 = tmp;
		}
		drawFastVLine(x0, y0, static_cast<uint8_t>(y1 - y0 + 1), color);
		return;
	}

	int16_t dx = static_cast<int16_t>(abs(x1 - x0));
	int16_t sx = x0 < x1 ? 1 : -1;
	int16_t dy = static_cast<int16_t>(-abs(y1 - y0));
	int16_t sy = y0 < y1 ? 1 : -1;
	int16_t err = dx + dy;

	while (true) {
		drawPixel(x0, y0, color);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int16_t e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void ArduboyBaseRem::drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
			      uint8_t color)
{
	drawFastHLine(x, static_cast<uint8_t>(y), w, color);
	drawFastHLine(x, static_cast<uint8_t>(y + h - 1), w, color);
	drawFastVLine(x, y, h, color);
	drawFastVLine(x + w - 1, y, h, color);
}

void ArduboyBaseRem::drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t color)
{
	meshbus::arduboy::fill_rect(x, y, 1U, h, color);
}

void ArduboyBaseRem::drawFastHLine(int16_t x, uint8_t y, int16_t w, uint8_t color)
{
	if (y >= HEIGHT || w <= 0) {
		return;
	}

	int16_t x_end = x + w;

	if (x_end <= 0 || x >= WIDTH) {
		return;
	}
	if (x < 0) {
		x = 0;
	}
	if (x_end > WIDTH) {
		x_end = WIDTH;
	}

	meshbus::arduboy::fill_rect(x, y, static_cast<uint8_t>(x_end - x), 1U, color);
}

void ArduboyBaseRem::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
			      uint8_t color)
{
	meshbus::arduboy::fill_rect(x, y, w, h, color);
}

void ArduboyBaseRem::fillScreen(uint8_t color)
{
	uint8_t value = 0U;

	if (color == WHITE) {
		value = 0xffU;
	} else if (color == GRAY) {
		value = (ArduboyCoreRem::flicker & 1U) != 0U ? 0x55U : 0xaaU;
	}

	memset(sBuffer, value, sizeof(ArduboyBaseRem::sBuffer));
}

void ArduboyBaseRem::drawRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
				   uint8_t r, uint8_t color)
{
	ARG_UNUSED(r);
	drawRect(x, y, w, h, color);
}

void ArduboyBaseRem::fillRoundRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
				   uint8_t r, uint8_t color)
{
	ARG_UNUSED(r);
	fillRect(x, y, w, h, color);
}

void ArduboyBaseRem::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
				  int16_t x2, int16_t y2, uint8_t color)
{
	drawLine(x0, y0, x1, y1, color);
	drawLine(x1, y1, x2, y2, color);
	drawLine(x2, y2, x0, y0, color);
}

void ArduboyBaseRem::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
				  int16_t x2, int16_t y2, uint8_t color)
{
	ARG_UNUSED(x0);
	ARG_UNUSED(y0);
	ARG_UNUSED(x1);
	ARG_UNUSED(y1);
	ARG_UNUSED(x2);
	ARG_UNUSED(y2);
	ARG_UNUSED(color);
}

void ArduboyBaseRem::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
				uint8_t w, uint8_t h)
{
	meshbus::arduboy::draw_bitmap(x, y, bitmap, w, h, WHITE);
}

void ArduboyBaseRem::drawMaskBitmap(int8_t x, int8_t y, const uint8_t *bitmap,
				    uint8_t hflip)
{
	if (bitmap == nullptr) {
		return;
	}

	uint8_t w = pgm_read_byte(bitmap++);
	uint8_t h = pgm_read_byte(bitmap++);
	int8_t hx = static_cast<int8_t>(pgm_read_byte(bitmap++));
	int8_t hy = static_cast<int8_t>(pgm_read_byte(bitmap++));

	y = static_cast<int8_t>(y - hy);
	if (hflip == 0U) {
		x = static_cast<int8_t>(x - hx);
	} else {
		x = static_cast<int8_t>(x - (w - hx));
	}

	int8_t end_x = static_cast<int8_t>(x + w);

	if ((end_x < 0 && x < 0) || static_cast<int8_t>(y + h) < 0 || y >= HEIGHT) {
		return;
	}

	uint8_t y_offset = static_cast<uint8_t>(_BV(y & 0x7));
	const uint8_t *source = bitmap;
	int8_t signed_row = y >> 3;
	uint8_t row = static_cast<uint8_t>(signed_row);
	uint8_t rows = (h + 7U) >> 3;

	if (signed_row < 0) {
		source += static_cast<size_t>(-signed_row) * w * 2U;
		rows = static_cast<uint8_t>(rows + signed_row);
		row = 0U;
	}
	if (row + rows > (HEIGHT >> 3)) {
		rows = static_cast<uint8_t>((HEIGHT >> 3) - row);
	}

	uint8_t start_col = 0U;
	uint8_t end_col = w;

	if (x < 0) {
		start_col = static_cast<uint8_t>(-x);
	}
	if (x + w > WIDTH) {
		end_col = static_cast<uint8_t>(WIDTH - x);
	}
	if (start_col >= end_col) {
		return;
	}

	if (hflip == 0U) {
		source += static_cast<size_t>(start_col) * 2U;
	} else {
		source += static_cast<size_t>(w - end_col) * 2U;
	}

	uint8_t source_skip = static_cast<uint8_t>((w - (end_col - start_col)) * 2U);

	if (hflip == 0U) {
		int16_t dest = static_cast<int16_t>(row) * WIDTH + x + start_col;
		uint8_t dest_skip = static_cast<uint8_t>(WIDTH - (end_col - start_col));

		for (uint8_t b_row = row; b_row < row + rows;
		     b_row++, source += source_skip, dest += dest_skip) {
			for (uint8_t col = start_col; col < end_col; col++, dest++) {
				uint16_t source_mask =
					static_cast<uint16_t>(~(pgm_read_byte(source++) * y_offset));
				uint16_t source_byte =
					static_cast<uint16_t>(pgm_read_byte(source++) * y_offset);
				size_t index = static_cast<size_t>(dest);

				sBuffer[index] = static_cast<uint8_t>((sBuffer[index] &
					static_cast<uint8_t>(source_mask)) |
					static_cast<uint8_t>(source_byte));
				sBuffer[index + WIDTH] = static_cast<uint8_t>((sBuffer[index + WIDTH] &
					static_cast<uint8_t>(source_mask >> 8)) |
					static_cast<uint8_t>(source_byte >> 8));
			}
		}
	} else {
		int16_t dest = static_cast<int16_t>(row) * WIDTH + x + end_col - 1;
		uint8_t dest_skip = static_cast<uint8_t>(WIDTH + (end_col - start_col));

		for (uint8_t b_row = row; b_row < row + rows;
		     b_row++, source += source_skip, dest += dest_skip) {
			for (uint8_t col = start_col; col < end_col; col++, dest--) {
				uint16_t source_mask =
					static_cast<uint16_t>(~(pgm_read_byte(source++) * y_offset));
				uint16_t source_byte =
					static_cast<uint16_t>(pgm_read_byte(source++) * y_offset);
				size_t index = static_cast<size_t>(dest);

				sBuffer[index] = static_cast<uint8_t>((sBuffer[index] &
					static_cast<uint8_t>(source_mask)) |
					static_cast<uint8_t>(source_byte));
				sBuffer[index + WIDTH] = static_cast<uint8_t>((sBuffer[index + WIDTH] &
					static_cast<uint8_t>(source_mask >> 8)) |
					static_cast<uint8_t>(source_byte >> 8));
			}
		}
	}
}

void ArduboyBaseRem::drawGrayBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
				    uint8_t w, uint8_t h)
{
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	ARG_UNUSED(bitmap);
	ARG_UNUSED(w);
	ARG_UNUSED(h);
}

void ArduboyBaseRem::drawTurboBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
				     uint8_t w, uint8_t h)
{
	drawBitmap(x, y, bitmap, w, h);
}

void ArduboyBaseRem::drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
				      uint8_t w, uint8_t h, uint8_t color)
{
	meshbus::arduboy::draw_xy_bitmap(x, y, bitmap, w, h, color);
}

void ArduboyBaseRem::drawCompressed(int16_t sx, int16_t sy, const uint8_t *bitmap,
				    uint8_t color)
{
	ARG_UNUSED(sx);
	ARG_UNUSED(sy);
	ARG_UNUSED(bitmap);
	ARG_UNUSED(color);
}

unsigned char *ArduboyBaseRem::getBuffer()
{
	return sBuffer;
}

void ArduboyBaseRem::initRandomSeed()
{
	meshbus_arduboy_random_seed(meshbus_arduboy_micros() ^ rng_state);
}

uint16_t ArduboyBaseRem::rawADC(uint8_t adc_bits)
{
	ARG_UNUSED(adc_bits);
	return static_cast<uint16_t>(rng_state);
}

void ArduboyBaseRem::setFrameRate(uint16_t rate)
{
	eachFrameMicros = rate == 0U ? default_frame_micros : rate;
}

void ArduboyBaseRem::nextFrame()
{
	time_us += eachFrameMicros == 0U ? default_frame_micros : eachFrameMicros;
	nextFrameStart = static_cast<uint16_t>(time_us);
	flicker++;
}

int ArduboyBaseRem::cpuLoad()
{
	return 0;
}

bool ArduboyBaseRem::pressed(uint8_t buttons)
{
	return (buttonsState() & buttons) == buttons;
}

bool ArduboyBaseRem::notPressed(uint8_t buttons)
{
	return (buttonsState() & buttons) == 0U;
}

void ArduboyBaseRem::pollButtons()
{
	previousButtonState = currentButtonState;
	currentButtonState = buttonsState();
}

bool ArduboyBaseRem::justPressed(uint8_t button)
{
	return (previousButtonState & button) == 0U && (currentButtonState & button) != 0U;
}

bool ArduboyBaseRem::justReleased(uint8_t button)
{
	return (previousButtonState & button) != 0U && (currentButtonState & button) == 0U;
}

bool ArduboyBaseRem::collide(Point point, Rect rect)
{
	return point.x >= rect.x && point.x < rect.x + rect.width &&
	       point.y >= rect.y && point.y < rect.y + rect.height;
}

bool ArduboyBaseRem::collide(Rect rect1, Rect rect2)
{
	return !(rect2.x >= rect1.x + rect1.width || rect2.x + rect2.width <= rect1.x ||
		 rect2.y >= rect1.y + rect1.height || rect2.y + rect2.height <= rect1.y);
}

void ArduboyBaseRem::swap(int16_t &a, int16_t &b)
{
	int16_t tmp = a;

	a = b;
	b = tmp;
}

void ArduboyBaseRem::sysCtrlSound(uint8_t buttons, uint8_t led, uint8_t eeVal)
{
	ARG_UNUSED(buttons);
	ARG_UNUSED(led);
	ARG_UNUSED(eeVal);
}

ArduboyRem::ArduboyRem()
{
	cursor_x = 0U;
	cursor_y = 0U;
	textColor = WHITE;
	textBackground = BLACK;
	textSize = 1U;
	textWrap = false;
}

size_t ArduboyRem::write(uint8_t c)
{
	if (c == '\n') {
		cursor_y = static_cast<uint8_t>(cursor_y + textSize * 8U);
		cursor_x = 0U;
	} else if (c != '\r') {
		drawChar(cursor_x, cursor_y, c);
		cursor_x = static_cast<uint8_t>(cursor_x + textSize * 6U);
		if (textWrap && cursor_x > WIDTH - textSize * 6U) {
			write('\n');
		}
	}
	return 1U;
}

void ArduboyRem::drawChar(uint8_t x, uint8_t y, unsigned char c) const
{
	if (x >= WIDTH || y >= HEIGHT || c < 32U || c > 127U) {
		return;
	}

	y >>= 3;
	for (uint8_t i = 0U; i < 6U && x + i < WIDTH; i++) {
		drawByte(x + i, y, pgm_read_byte(font + ((c - 32U) * 6U) + i));
	}
}

void ArduboyRem::printBytePadded(uint8_t x, uint8_t y, byte num) const
{
	drawChar(x, y, static_cast<unsigned char>('0' + (num / 10U)));
	drawChar(x + 6U, y, static_cast<unsigned char>('0' + (num % 10U)));
}

void ArduboyRem::setCursor(uint8_t x, uint8_t y)
{
	cursor_x = x;
	cursor_y = y;
}

uint8_t ArduboyRem::getCursorX() const
{
	return cursor_x;
}

uint8_t ArduboyRem::getCursorY() const
{
	return cursor_y;
}

void ArduboyRem::setTextColor(uint8_t color)
{
	textColor = color;
}

void ArduboyRem::setTextBackground(uint8_t bg)
{
	textBackground = bg;
}

void ArduboyRem::setTextSize(uint8_t s)
{
	textSize = MAX(s, 1U);
}

void ArduboyRem::setTextWrap(bool w)
{
	textWrap = w;
}

void ArduboyRem::clear()
{
	ArduboyBaseRem::clear();
	cursor_x = 0U;
	cursor_y = 0U;
}

void ArduboyAudioRem::begin()
{
	audio_enabled = meshbus_arduboy_eeprom_read(EEPROM_AUDIO_ON_OFF) != 0U;
}

void ArduboyAudioRem::on()
{
	audio_enabled = true;
}

void ArduboyAudioRem::off()
{
	audio_enabled = false;
}

void ArduboyAudioRem::saveOnOff()
{
	meshbus_arduboy_eeprom_update(EEPROM_AUDIO_ON_OFF, audio_enabled ? 1U : 0U);
}

bool ArduboyAudioRem::enabled()
{
	return audio_enabled;
}

extern "C" void __cxa_pure_virtual()
{
}

extern "C" void ard_drivin_app_main(void *args)
{
	ArdDrivinApp app{};

	current_app = &app;
	meshbus::arduboy::llext_game::run(args, &app, &game_config, &screen_ops);
	if (current_app == &app) {
		current_app = nullptr;
	}
}

LL_EXTENSION_SYMBOL(ard_drivin_app_main);

#include "../upstream/main.cpp"
