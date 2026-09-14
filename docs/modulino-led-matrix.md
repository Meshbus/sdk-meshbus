# Arduino Modulino LED Matrix

The `arduino,modulino-led-matrix` driver controls a 12×8 Modulino LED Matrix
through Zephyr's Display API. The module keeps its Arduino firmware; Zephyr
runs on the I2C host. No changes to the Modulino firmware or west dependencies
are required.

A complete [ESP32-C6 feature sample](../samples/drivers/display/modulino_led_matrix/README.md)
provides startup API checks and continuously looping visual scenes. Its
[hardware run record](../samples/drivers/display/modulino_led_matrix/VALIDATION.md)
records actual I2C execution and the user's confirmation of grayscale output.

## Firmware contract

The implementation follows Arduino
[node_modulino_firmware at 1df8097cfc045821fa76948283a8d9b82c86f204](https://github.com/arduino/node_modulino_firmware/tree/1df8097cfc045821fa76948283a8d9b82c86f204):

- [Core/Src/main.c](https://github.com/arduino/node_modulino_firmware/blob/1df8097cfc045821fa76948283a8d9b82c86f204/Core/Src/main.c):
  identity, mode reporting, exact receive lengths and command dispatch.
- [Core/Src/matrix.c](https://github.com/arduino/node_modulino_firmware/blob/1df8097cfc045821fa76948283a8d9b82c86f204/Core/Src/matrix.c):
  vertical monochrome layout and horizontal packed grayscale layout.
- [Arduino host library](https://github.com/arduino-libraries/Modulino/blob/070444adccae47804ed00848fb05a83f3e130a8e/src/Modulino_LED_Matrix.h):
  7-bit address, mode discovery, command padding and 1 ms mode settling time.

The firmware's `NODE_LEDMATRIX = 0x72` is an STM32 HAL shifted address.
Devicetree uses the **7-bit address `0x39`**. A previously configured alternative
address can be supplied through `reg`. The reported identity remains `0x72`.

A direct four-byte I2C read must return `0x72` followed by `MON` or `GS4`.
Unrecognized identities fail with `-ENODEV`; unrecognized modes fail with
`-ENOTSUP`. Firmware without these mode reports is not supported. Successful
initialization checks the report, switches to monochrome if needed, and blanks
the panel. It does not update firmware, change its address, or access its
bootloader.

| Mode | Frame bytes | Pixel layout |
| --- | ---: | --- |
| `MON` | 12 | One byte per column; bit 0 is the top row, bit 7 the bottom row |
| `GS4` | 48 | Row major, two pixels per byte; left/even pixel in the high nibble |

Frames are sent in one I2C write with a STOP, without a register prefix. Mode
commands are zero-padded to the **current** mode's receive length: switching
MON → GS4 sends 12 bytes, and GS4 → MON sends 48 bytes. The driver reads the
current mode before presenting each frame and confirms a mode change before
sending pixels. This also allows recovery after the Modulino resets separately
from the host or an earlier transfer fails during a mode change.

The firmware processes commands in its main loop and shares its receive and
status buffers. The driver waits `command-delay-ms` after each write, including
failed writes, before allowing the next operation. The default is 1 ms, based
on the Arduino library's mode-switch delay. Applying that delay to every write
is a conservative host policy. The sample's recorded 1,000-frame hardware run
completed with this default; longer runs and visual fidelity need separate
validation. Only this driver should access the
module while it is in use.

### Reserved pixel prefixes

Upstream firmware interprets these byte prefixes before processing pixels:

| Prefix | Firmware action |
| --- | --- |
| `CF` | Persist an I2C address and reset |
| `DIE` | Enter the bootloader |
| `MON` | Select monochrome |
| `GS4` | Select grayscale |

There is no frame opcode or escaping in this protocol. The driver returns
`-EINVAL` without I2C traffic when a proposed complete frame starts with any
of these prefixes. It checks after merging partial updates and also checks
format conversions and writes while blanked. The previous accepted image is
retained. These exact patterns cannot be displayed in the selected wire mode
with this firmware; supporting every possible frame would require an upstream
protocol change.

## Zephyr integration

Enable I2C and Display in the consuming application's `prj.conf`:

```ini
CONFIG_I2C=y
CONFIG_DISPLAY=y
```

`CONFIG_MODULINO_LED_MATRIX` defaults to `y` when an enabled compatible node
is present. The Meshbus west module supplies its source and binding.
The driver uses `PIXEL_FORMAT_L_4`, so the consuming Zephyr version must expose
that Display API format (as the current workspace does).

Add the device to the board's connected I2C controller. Replace `&i2c0` with
the actual controller; configure pinctrl and bus frequency for the host board
and connected hardware:

```dts
&i2c0 {
    status = "okay";

    modulino_matrix: display@39 {
        compatible = "arduino,modulino-led-matrix";
        reg = <0x39>;
        width = <12>;
        height = <8>;
        /* command-delay-ms = <1>; */
    };
};
```

An application can choose it explicitly with
`DEVICE_DT_GET(DT_NODELABEL(modulino_matrix))`. If it is the application's
primary display, optionally set `zephyr,display = &modulino_matrix` in
`/chosen`. This does not require replacing another product display.

Example: show four corner pixels using the default `PIXEL_FORMAT_MONO01`:

```c
#include <zephyr/drivers/display.h>

int show_matrix_corners(void)
{
    const struct device *display = DEVICE_DT_GET(DT_NODELABEL(modulino_matrix));
    const uint8_t corners[12] = {0x81, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x81};
    const struct display_buffer_descriptor desc = {
        .width = 12, .height = 8, .pitch = 12, .buf_size = sizeof(corners),
    };
    int ret;

    if (!device_is_ready(display)) {
        return -ENODEV;
    }
    ret = display_write(display, 0, 0, &desc, corners);
    return ret < 0 ? ret : display_blanking_off(display);
}
```

For 16-level grayscale, call
`display_set_pixel_format(display, PIXEL_FORMAT_L_4)` and supply 48 bytes for
a full frame. A byte `0xf0` sets the first pixel to maximum intensity and the
second to off. Intensity is controlled per pixel; there is no separate global
brightness or contrast API in this driver.

`display_get_capabilities()` reports the current buffer layout:

- `MONO01`: `SCREEN_INFO_MONO_VTILED`, LSB first. For any supported rectangle,
  each input column is one byte, with bit 0 corresponding to the rectangle's
  first row. Extra pitch columns are padding; only `width` bytes are read.
- `L_4`: horizontal packed grayscale. Each source row occupies
  `ceil(pitch / 2)` bytes. An odd pitch pads the low nibble of the final byte.
  The last row needs only `ceil(width / 2)` readable bytes.

Both formats support rectangles within the 12×8 display and preserve pixels
outside the rectangle. Format changes preserve cached luminance; monochrome
renders any nonzero luminance as lit. A successful monochrome write stores
0 or 15 in the updated pixels.

The panel starts blanked. Writes while blanked update the cache;
`display_blanking_off()` sends the latest image. `display_blanking_on()` sends
black without discarding the cache. Blanking does not power down the module.
Each visible write sends the whole merged frame; `frame_incomplete` does not
defer transmission. Calls use a mutex and blocking I2C and must run in thread
context. Readback, rotation, direct framebuffer access and power-management
callbacks are not implemented.

I2C errors are returned to the caller, and failed operations do not commit
new cached pixels, format or blanking state. An error cannot guarantee that
no bytes reached the physical device. A subsequent successful presentation
resends the entire frame after discovering its mode.

## Validation

On 2026-09-14, against local Zephyr `753bd132d31c`, the following command
passed 1/1 configurations and 14/14 test cases on `qemu_x86/atom`, with no
Twister warnings. Reports are under the specified output directory.

From `west topdir`, with the environment activated:

```sh
source ~/.zephyr/env/bin/activate
west twister -T meshbus/tests/drivers/display/modulino_led_matrix \
  -p qemu_x86 -O twister-out/modulino-led-matrix-20260914 \
  --inline-logs -v -c -j 1
```

The Ztest I2C emulator enforces address routing, STOP boundaries, exact packet
lengths, command padding, mode reports and the configured default write delay.
Tests cover cold/warm initialization, pixel mapping, odd grayscale pitch,
partial updates, blanking, reserved prefixes, invalid descriptors, transfer
errors and recovery after mode changes or device resets.

The following source checks also passed; checkpatch reported zero errors
and zero warnings:

```sh
# From the Meshbus repository root, with the environment activated:
clang-format --dry-run --Werror drivers/display/modulino_led_matrix.c \
  tests/drivers/display/modulino_led_matrix/src/main.c
perl ../zephyr/scripts/checkpatch.pl --no-tree --file \
  drivers/display/modulino_led_matrix.c \
  tests/drivers/display/modulino_led_matrix/src/main.c
git diff --check
```

This is host-driver build and simulated protocol evidence. It does not prove
physical wiring, electrical compatibility, installed Modulino firmware,
visible orientation, grayscale quality, or sustained refresh timing. No board
profile is modified and no device is accessed by this test.
