# Arduboy3D MBA Port

This directory contains a Meshbus `.mba` port of Catacombs of the damned! for the
Meshbus Desktop LLEXT app loader.

Upstream source snapshot:

- Repository: `https://github.com/jhhoward/Arduboy3D`
- Commit: `929db9f3429cc20a318934099d992f1219a081bd`
- Upstream app version: `1.0`
- License: MIT

The upstream Arduino entry file and game sources are included through a unity
C++ port layer. The `meshbus-arduboy` SDK provides the narrow Arduboy2 shim
backed by a 128x64 `ZUI_BITMAP_FORMAT_MONO_VLSB` framebuffer.

Port boundaries:

- ZUI input state maps to Arduboy `A/B/D-pad` button bits.
- Short BACK is available to the game as B. Long BACK/MENU/HOME or long T9 `*`
  exits the `.mba` app.
- Rendering uses the upstream page-major Arduboy framebuffer layout.
- ArduboyTones score data is converted to single-channel buzzer melodies through
  the Meshbus indicator buzzer.
- Upstream Arduboy3D does not use EEPROM, so this port does not create a save
  file.
- The full 3D renderer and C++ relocation metadata are large. The C2
  `meshbus_client` host must reserve at least the heap size reported by
  `meshbus llext`; this port keeps its app stack at 4096 bytes and does not adjust
  host heap policy locally.

Build with the installed Rust `meshbus` CLI and a released EDK
for the intended Desktop host. Set `ZEPHYR_SDK_INSTALL_DIR` and install CMake
and Ninja; no Firmware source checkout is required:

```sh
meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
  sdk-meshbus/samples/subsys/meshbus/services/llext/apps/arduboy3d
```

Manual validation checklist:

- Catacombs 3D appears in the Desktop app launcher.
- Launch opens a fullscreen 128x64 title/menu screen.
- D-pad or T9 `2`/`8` moves the menu selection.
- SELECT, T9 `5`, or T9 `#` starts the game or toggles sound.
- In game, D-pad turns/moves and SELECT fires.
- Short BACK or T9 `*` maps to B.
- Long BACK or T9 `*` exits the `.mba` app, detaches the fullscreen layer, and
  returns to Desktop.
- Buzzer effects play when sound is enabled and stop after app exit.
