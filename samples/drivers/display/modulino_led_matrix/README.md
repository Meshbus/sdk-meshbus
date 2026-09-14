# Modulino LED Matrix looping test

A standalone Zephyr sample for `devkit_esp32c6/esp32c6/hpcore`. On power-up it
runs the Display API checks once, then continuously cycles through ten
visual tests. It needs no shell commands or buttons. Any failed API call
prints `MODULINO_SAMPLE_FAIL` and stops playback.

## Hardware and build

The board definition supplies `zephyr,display = &modulino_matrix`:

| Connection | Board configuration |
| --- | --- |
| SDA | GPIO22 / SDA / A4 |
| SCL | GPIO23 / SCL / A5 |
| I2C | I2C0, 100 kHz, 7-bit address `0x39` |
| Console | Native USB Serial/JTAG, GPIO12/13, 115200 baud |

Power the matrix and share ground with the host. The board's 3.3 V GPIO and
I2C pull-up requirements apply. Firmware must report identity `0x72` followed
by `MON` or `GS4`; see the [driver documentation](../../../../docs/modulino-led-matrix.md).
All operations use the Display API, without raw I2C commands.

From `west topdir`:

```sh
source ~/.zephyr/env/bin/activate
west build -b devkit_esp32c6/esp32c6/hpcore \
  meshbus/samples/drivers/display/modulino_led_matrix \
  -d build/modulino-refresh-esp32c6-20260914
west flash -d build/modulino-refresh-esp32c6-20260914 \
  --runner esp32 --esp-device /dev/tty.usbmodem21244401
```

Use the confirmed board's current serial path. This standalone engineering
application replaces the selected board's application using its default
flash layout. The sample does not update the Modulino firmware or change its
I2C address.

`CONFIG_SAMPLE_MATRIX_SCENE_MS=1500` controls static scene duration. Walking
pixels advance every 30 ms. `CONFIG_SAMPLE_MATRIX_SCROLL_MS=80` sets the
scrolling text interval per column. Each complete cycle takes approximately
40 seconds with the defaults. The initial three-second delay allows serial reconnection.

## Playback and coverage

The startup checks validate capabilities in both formats, invalid descriptors,
unsupported operations, reserved prefixes while visible and blanked, and a
partial update that would assemble a reserved prefix. Success prints
`MODULINO_SAMPLE_API_PASS checks=60`.

The following sequence then repeats indefinitely:

| Scene | Expected display and exercised feature |
| --- | --- |
| `scroll` | `FOBE DEVKIT ESP32C6` scrolls right to left in a 5×7 font, starting and ending off-screen. |
| `refresh` | Moving diagonal wave, first monochrome then grayscale, each sent as fast as the Display API permits for three seconds. Compares full-frame update rates. |
| `corners` | Top left: one dot; top right: two dots; bottom left: three dots; bottom right: a 2×2 square. Checks orientation and edges. |
| `checker` | Alternating lit/dark pixels across all 12 columns and eight rows. |
| `mono_partial` | Bright border retained, with a 3×4 patch at `(4,2)` reading `#.#`, `.#.`, `.#.`, `#.#`. Pitch five tests padding; `frame_incomplete=true` still updates immediately. |
| `gray` | Levels 0 through 15 repeated six times in row-major order. Check level zero is off, increasing brightness, and flicker. |
| `gray_partial` | Bright border retained; a 3×2 patch at `(3,2)` contains levels `1 2 3 / 4 5 6`. Exercises odd pitch, odd destination x and nibble alignment. |
| `walk` | One lit pixel walks all 96 positions, left to right, top to bottom. |
| `blank` | Corners, then full darkness while a checker is cached, then the checker appears on unblank. |
| `convert` | Grayscale ramp, then nonzero pixels fully lit in monochrome, then the original grayscale ramp restored. |

Console output identifies each scene and cycle:

```text
MODULINO_SAMPLE_API_PASS checks=60 (return values; no pixel readback)
MODULINO_SAMPLE_CYCLE_BEGIN cycle=1
VISUAL scene=scroll ...
MODULINO_SAMPLE_SCROLL_COMPLETE text=FOBE DEVKIT ESP32C6 frames=126
VISUAL scene=corners ...
...
MODULINO_SAMPLE_SEQUENCE_COMPLETE scenes=10 visual_confirmation=required
MODULINO_SAMPLE_CYCLE_COMPLETE cycle=1
MODULINO_SAMPLE_CYCLE_BEGIN cycle=2
```

Scene and cycle completion mean the Display API calls completed successfully.
The protocol has no pixel readback, so actual appearance needs observation.
The user confirmed grayscale operation on the attached hardware; see the
[hardware validation record](VALIDATION.md) for the exact evidence boundary.

The sample's repeated transitions also exercise actual MON/GS4 switching and
cached blanking across cycles. Bus fault injection, wrong identities,
unconfirmed mode changes, custom addresses and cache rollback remain covered
by the existing 14-case QEMU driver suite; the sample does not induce physical
faults or rewrite Modulino configuration to reproduce those cases.

## Maximum refresh animation

The `refresh` scene follows the scrolling text. A 32-phase diagonal wave
advances on every submitted frame. Monochrome renders its bright portions as
moving bands; grayscale renders the full 16-level wave. Both use full-screen
writes, covering all 96 pixels.

`CONFIG_SAMPLE_MATRIX_REFRESH_MS=3000` controls each format's measurement
window. There is no application sleep or per-frame logging in that window.
The driver still performs its status read, frame write and 1 ms settling wait.
Mode setup and console reporting are outside the timed window.

Each phase reports `MODULINO_SAMPLE_REFRESH_PASS` with format, frame count,
elapsed time, FPS and average/minimum/maximum `display_write()` duration in
microseconds. FPS includes animation generation and driver work; call latency
is measured separately with the cycle counter. All 32 wave phases in both
formats were checked against the firmware's reserved command prefixes.

This measures the host's full-frame update ceiling for the current **100 kHz
I2C** configuration and driver. It is not the module's LED scan/PWM frequency,
nor proof that the physical display presented every submitted frame without
tearing. The firmware provides no frame-present acknowledgment or vsync event.
No bus overclock or change to the module firmware is made by this scene.

The [2026-09-14 hardware run](VALIDATION.md#measured-update-rates) measured
333.222 host FPS in monochrome and 142.857 host FPS in grayscale. Mean full
write times were 2.984 ms and 6.981 ms respectively.

## Capture a complete cycle

Immediately after flashing, from `west topdir`:

```sh
python meshbus/scripts/serial_use.py monitor /dev/tty.usbmodem21244401 \
  --baudrate 115200 --timeout 60 \
  --wait MODULINO_SAMPLE_API_PASS \
  --wait 'MODULINO_SAMPLE_SCROLL_COMPLETE text=FOBE DEVKIT ESP32C6 frames=126' \
  --wait 'MODULINO_SAMPLE_REFRESH_PASS format=MONO01' \
  --wait 'MODULINO_SAMPLE_REFRESH_PASS format=L_4' \
  --wait 'MODULINO_SAMPLE_CYCLE_COMPLETE cycle=1' --wait-all \
  --fail-pattern MODULINO_SAMPLE_FAIL --post-wait-seconds 1 \
  --transcript meshbus/.scratch/modulino-led-matrix/refresh-runtime.log
python meshbus/scripts/serial_use.py check-log \
  --file meshbus/.scratch/modulino-led-matrix/refresh-runtime.log \
  --fail-pattern MODULINO_SAMPLE_FAIL
```

If attaching after startup, omit the startup API-pass wait and wait for a
future cycle marker. A missed startup log is not a failed display test.
Use one serial owner at a time. The sample metadata is build-only: a CI build
is not a hardware or visual pass.
