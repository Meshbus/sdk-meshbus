/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zui/zui.h>
#include <ArduboyTones.h>
#include <meshbus_arduboy/runtime.hpp>
#include <meshbus_arduboy/compat.hpp>
#include "../upstream/ArduboyRem.h"
#include "../upstream/pics/font.h"
extern "C" void meshbus_arduboy_audio_begin();
extern "C" bool meshbus_arduboy_audio_enabled();
extern "C" void meshbus_arduboy_audio_set_enabled(bool);
extern "C" void meshbus_arduboy_audio_save();
constexpr uint16_t default_frame_micros = 1000000U / 67U;
constexpr size_t visible_framebuffer_size = WIDTH * HEIGHT / 8U;
byte ArduboyCoreRem::flicker = 0;
uint8_t ArduboyBaseRem::sBuffer[((HEIGHT + 8) * WIDTH) / 8] = {};
bool ArduboyAudioRem::audio_enabled = false;
static void clear_for_next_frame() {
    uint8_t alternate = (ArduboyCoreRem::flicker & 1U) ? 0x55U : 0xaaU;
    for (size_t i=0;i<sizeof(ArduboyBaseRem::sBuffer);++i) {
        ArduboyBaseRem::sBuffer[i]=alternate;alternate=static_cast<uint8_t>(~alternate);
    }
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
	if (image && meshbus_arduboy_framebuffer()) {
        memcpy(meshbus_arduboy_framebuffer(), image, visible_framebuffer_size);
        meshbus_arduboy_display(false);
    }
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
	return meshbus_arduboy_buttons();
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

// Rem draws into its extended canvas; display() publishes it to the runtime.
// Never mix runtime-targeted helpers with sBuffer: publication would erase them.
void ArduboyBaseRem::drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t color)
{
	meshbus::arduboy::fill_rect_to(sBuffer, x, y, 1U, h, color);
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

	meshbus::arduboy::fill_rect_to(sBuffer, x, y, static_cast<uint8_t>(x_end - x), 1U, color);
}

void ArduboyBaseRem::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
			      uint8_t color)
{
	meshbus::arduboy::fill_rect_to(sBuffer, x, y, w, h, color);
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
	meshbus::arduboy::draw_bitmap_to(sBuffer, x, y, bitmap, w, h, WHITE);
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
	meshbus::arduboy::draw_xy_bitmap_to(sBuffer, x, y, bitmap, w, h, color);
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
	meshbus_arduboy_random_seed(meshbus_arduboy_micros());
}

uint16_t ArduboyBaseRem::rawADC(uint8_t adc_bits)
{
	ARG_UNUSED(adc_bits);
	return static_cast<uint16_t>(meshbus_arduboy_micros());
}

void ArduboyBaseRem::setFrameRate(uint16_t rate)
{
	eachFrameMicros = rate == 0U ? default_frame_micros : rate;
}

void ArduboyBaseRem::nextFrame()
{
	uint16_t now = static_cast<uint16_t>(meshbus_arduboy_micros());
    uint16_t elapsed = static_cast<uint16_t>(now - nextFrameStart);
    uint16_t period = eachFrameMicros ? eachFrameMicros : default_frame_micros;
    if (elapsed < period) { meshbus_arduboy_delay((period - elapsed + 999U) / 1000U); }
    nextFrameStart = static_cast<uint16_t>(meshbus_arduboy_micros());
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
	meshbus_arduboy_audio_begin();
    audio_enabled = meshbus_arduboy_audio_enabled();
}

void ArduboyAudioRem::on()
{
	audio_enabled = true;
    meshbus_arduboy_audio_set_enabled(true);
}

void ArduboyAudioRem::off()
{
	audio_enabled = false;
    meshbus_arduboy_audio_set_enabled(false);
}

void ArduboyAudioRem::saveOnOff()
{
	meshbus_arduboy_audio_save();
}

bool ArduboyAudioRem::enabled()
{
	return meshbus_arduboy_audio_enabled();
}

extern "C" void __cxa_pure_virtual()
{
}

void setup();
void loop();
enum class EState : uint8_t;
extern EState currentState;
extern uint8_t speed, gameTimer, gear;
extern uint16_t lastMilli;
extern int16_t playerX;
// ArduboyRem retains its AVR board button masks, distinct from SDK defaults.
static uint8_t rem_buttons(uint32_t actions) {
    uint8_t value=0;
    if(actions & ZUI_ACTION_PRIMARY) value|=A_BUTTON;
    if(actions & ZUI_ACTION_SECONDARY) value|=B_BUTTON;
    if(actions & ZUI_ACTION_UP) value|=UP_BUTTON;
    if(actions & ZUI_ACTION_DOWN) value|=DOWN_BUTTON;
    if(actions & ZUI_ACTION_LEFT) value|=LEFT_BUTTON;
    if(actions & ZUI_ACTION_RIGHT) value|=RIGHT_BUTTON;
    return value;
}
static const meshbus::arduboy::RuntimeHooks rem_hooks{nullptr,nullptr,rem_buttons};
static void setup_race() {
    setup();
    lastMilli = static_cast<uint16_t>(meshbus_arduboy_millis());
}
static void loop_race() {
    static uint32_t report_at;
    static int previous = -1;
    loop();
    uint32_t now = meshbus_arduboy_millis();
    int state = static_cast<int>(currentState);
    if (previous != state || now - report_at >= 2000) {
        previous = state;report_at = now;
        printk("[ard_drivin] state=%d speed=%u gear=%u x=%d timer=%u\n",
            state,speed,gear,playerX,gameTimer);
    }
}
extern "C" void ard_drivin_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"ard_drivin",setup_race,loop_race};
    // Rem's void nextFrame owns its microsecond-period wait; shared runner
    // consumes input each loop and guarantees a scheduling opportunity.
    config.fps=67;config.frame_gated=false;config.hooks=&rem_hooks;
    int rc=meshbus::arduboy::run_sketch(args,config);
    printk("[ard_drivin] complete rc=%d\n",rc);
}
LL_EXTENSION_SYMBOL(ard_drivin_app_main);
