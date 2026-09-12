# Hollow MBA Port

This directory contains an experimental source-level Meshbus `.mba` port of
Hollow for the Meshbus Desktop LLEXT app loader.

Upstream source snapshot:

- Repository: `https://github.com/obono/ArduboyWorks`
- Path: `hollow`
- Commit: `d4b1f041789dcd1d71907654e4025d613b4ab420`
- Upstream app version: `0.33`
- License: MIT

The upstream Arduino entry file is included through a unity C++ port layer.
The `meshbus-arduboy` SDK provides the narrow Arduboy 1.1-style shim backed by
a 128x64 `ZUI_BITMAP_FORMAT_MONO_VLSB` framebuffer.

Port boundaries:

- ZUI input state maps to Hollow's Arduboy `A/B/D-pad` button bits.
- Rendering uses the upstream page-major Arduboy framebuffer directly.
- Long BACK/MENU/HOME or long T9 `*` exits the `.mba` app. Short BACK remains
  Hollow's B button.
- Hollow's EEPROM record block is stored at `/extra/saves/hollow.dat` with the
  same byte layout the upstream title code expects.
- Arduboy Playtune score data is converted to short single-channel buzzer
  melodies through the Meshbus indicator buzzer.

Build with the installed Rust `meshbus` CLI and a released EDK
for the intended Desktop host. Set `ZEPHYR_SDK_INSTALL_DIR` and install CMake
and Ninja, and provide the external Meshbus Arduboy SDK described in the
[shared build requirements](../../README.rst). A firmware source checkout is
not required when these compiler inputs are supplied. Run from the west workspace
root when using the repository paths below:

```sh
meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
  meshbus/samples/subsys/meshbus/services/llext/apps/hollow
```

Manual validation checklist:

- Hollow appears in the Desktop app launcher.
- Launch opens a fullscreen 128x64 screen.
- D-pad or T9 `2`/`8`/`4`/`6` moves the menu selection and player.
- SELECT, T9 `5`, or T9 `#` maps to Hollow A.
- Short BACK or T9 `*` maps to Hollow B.
- Long BACK or T9 `*` exits the `.mba` app, detaches the fullscreen layer, and
  returns to Desktop.
- Completing or losing a run updates `/extra/saves/hollow.dat` and record data
  survives app relaunch.
