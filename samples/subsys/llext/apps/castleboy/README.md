# CastleBoy MBA Port

This directory contains an experimental source-level Meshbus `.mba` port of
CastleBoy for the Meshbus Desktop LLEXT app loader.

Upstream source snapshot:

- Repository: `https://github.com/jlauener/CastleBoy`
- Commit: `1d758fdd862d6269dc459661779c52d75cc87f98`
- License: MIT

The SDK compiles the upstream sketch and game sources as separate translation
units and links them with the shared runtime.
The `meshbus-arduboy` SDK provides the Arduboy2 and ArduboyTones compatibility
layer backed by a 128x64 `ZUI_BITMAP_FORMAT_MONO_VLSB` framebuffer.

Audio:

- ArduboyTones calls are routed to the Meshbus indicator buzzer as
  `INDICATOR_SOURCE_SYSTEM`.
- `tone()` and `tones()` support one-shot single-channel frequency/duration
  sequences on the board PWM buzzer.
- `TONES_REPEAT` sequences use the shared runtime repeat support.

Storage and capacity:

- The shared runtime persists EEPROM/audio settings under
  `/extra/saves/castleboy.sav`.
- Use the heap requirement reported by the current CLI build. The selected
  host EDK must provide sufficient application and total LLEXT heap capacity.

Build with the installed Rust `meshbus` CLI and a released EDK
for the intended Desktop host. Set `ZEPHYR_SDK_INSTALL_DIR` and install CMake
and Ninja, and provide the external Meshbus Arduboy SDK described in the
[shared build requirements](../../README.rst). A firmware source checkout is
not required when these compiler inputs are supplied. Run from the west workspace
root when using the repository paths below:

```sh
meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
  meshbus/samples/subsys/llext/apps/castleboy
```

Manual validation checklist:

- CastleBoy appears in the Desktop app launcher.
- Launch opens a fullscreen 128x64 screen.
- T9 `2`/`8`/`4`/`6` moves menu selection and the player.
- T9 `5` or `#` maps to Arduboy A.
- T9 `*` maps to Arduboy B.
- Long T9 `*` exits the `.mba` app, detaches the fullscreen layer, and returns
  to Desktop.
