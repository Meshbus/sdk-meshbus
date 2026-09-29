# Modulino LED Matrix looping test

Select `<qualified-board-target>` from this sample's `sample.yaml` and inspect
the matching configuration/overlays. A placeholder is not a claim of support
for additional boards.

A standalone Zephyr Display API sample. On power-up it
runs the Display API checks once, then continuously cycles through ten
visual tests. It needs no shell commands or buttons. Any failed API call
prints `MODULINO_SAMPLE_FAIL` and stops playback.

## Hardware and build

Provide a display node with `compatible = "arduino,modulino-led-matrix"` and
select it through `zephyr,display`. The selected devicetree defines the I2C
controller, pins, bus speed and address (normally `0x39`). Use `sample.yaml`
for existing target support; additional targets need matching hardware and DTS.

Power the matrix with a shared ground and compatible I2C voltage/pull-ups.
Firmware must report identity `0x72` followed by `MON` or `GS4`.
All operations use the Display API, without raw I2C commands. The driver
rejects frames whose first bytes match a firmware command (`CF`, `DIE`,
`MON` or `GS4`) with `-EINVAL`; arbitrary pixel patterns are therefore not
always representable. Readback, rotation and direct framebuffer access are
unsupported. See the [devicetree binding](../../../../dts/bindings/display/arduino,modulino-led-matrix.yaml)
for address and write-delay configuration.

Activate your Zephyr development environment and run from `west topdir`:

```sh
west build -b '<qualified-board-target>' \
  meshbus/samples/drivers/display/modulino_led_matrix \
  -d 'build/<task>-matrix'
west flash -d 'build/<task>-matrix'
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
| `scroll` | The text defined in `src/main.c` scrolls right to left in a 5×7 font, starting and ending off-screen. |
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
MODULINO_SAMPLE_SCROLL_COMPLETE
VISUAL scene=corners ...
...
MODULINO_SAMPLE_SEQUENCE_COMPLETE scenes=10 visual_confirmation=required
MODULINO_SAMPLE_CYCLE_COMPLETE cycle=1
MODULINO_SAMPLE_CYCLE_BEGIN cycle=2
```

Scene and cycle completion mean the Display API calls completed successfully.
The protocol has no pixel readback, so actual appearance needs observation.

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

## Capture a complete cycle

Immediately after flashing, from `west topdir`:

```sh
capture_tmp="$(mktemp -d "${TMPDIR:-/tmp}/meshbus-modulino.XXXXXX")"
python meshbus/scripts/serial_use.py monitor '<serial-port>' \
  --baudrate 115200 --timeout 60 \
  --wait MODULINO_SAMPLE_API_PASS \
  --wait 'MODULINO_SAMPLE_SCROLL_COMPLETE' \
  --wait 'MODULINO_SAMPLE_REFRESH_PASS format=MONO01' \
  --wait 'MODULINO_SAMPLE_REFRESH_PASS format=L_4' \
  --wait 'MODULINO_SAMPLE_CYCLE_COMPLETE cycle=1' --wait-all \
  --fail-pattern MODULINO_SAMPLE_FAIL --post-wait-seconds 1 \
  --transcript "$capture_tmp/refresh-runtime.log"
python meshbus/scripts/serial_use.py check-log \
  --file "$capture_tmp/refresh-runtime.log" \
  --fail-pattern MODULINO_SAMPLE_FAIL
```

If attaching after startup, omit the startup API-pass wait and wait for a
future cycle marker. A missed startup log is not a failed display test.
Use one serial owner at a time. The sample metadata is build-only: a CI build
is not a hardware or visual pass.
