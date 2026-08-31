# CastleBoy MBA Port

This directory contains an experimental source-level Meshbus `.mba` port of
CastleBoy for the Meshbus Desktop LLEXT app loader.

Upstream source snapshot:

- Repository: `https://github.com/jlauener/CastleBoy`
- Commit: `1d758fdd862d6269dc459661779c52d75cc87f98`
- License: MIT

The upstream Arduino entry file is included through a unity C++ port layer.
The `meshbus-arduboy` SDK provides the Arduboy2 and ArduboyTones compatibility
layer backed by a 128x64 `ZUI_BITMAP_FORMAT_MONO_VLSB` framebuffer.

Audio:

- ArduboyTones calls are routed to the Meshbus indicator buzzer as
  `INDICATOR_SOURCE_SYSTEM`.
- `tone()` and `tones()` support one-shot single-channel frequency/duration
  sequences on the board PWM buzzer.
- `TONES_REPEAT` sequences are played once instead of looping indefinitely.

Current limitations:

- EEPROM/audio settings are kept in memory only.
- The full upstream asset set plus buzzer bridge is large; the current build
  estimates about 72 KB of LLEXT heap and therefore needs the C2 host app
  reserve to be larger than the earlier 64 KB MicroCity-only setting.

Build with the installed Rust `meshbus` CLI and a released app-profile EDK
for the intended Desktop host. Set `ZEPHYR_SDK_INSTALL_DIR` and install CMake
and Ninja; no Firmware source checkout is required:

```sh
meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
  sdk-meshbus/samples/subsys/meshbus/services/llext/apps/castleboy
```

Manual validation checklist:

- CastleBoy appears in the Desktop app launcher.
- Launch opens a fullscreen 128x64 screen.
- T9 `2`/`8`/`4`/`6` moves menu selection and the player.
- T9 `5` or `#` maps to Arduboy A.
- T9 `*` maps to Arduboy B.
- Long T9 `*` exits the `.mba` app, detaches the fullscreen layer, and returns
  to Desktop.
