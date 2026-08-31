# MicroCity MBA Port

This directory contains a source-level Meshbus `.mba` port of MicroCity for the
Meshbus Desktop LLEXT app loader.

Upstream source snapshot:

- Repository: `https://github.com/jhhoward/MicroCity`
- Tag: `v1.3`
- Commit: `ae265f1923e9ac9789df601bb438faa29b7ae339`
- Upstream package metadata version: `1.1`
- License: GNU GPL v3

The upstream Arduino entry file `MicroCity.ino`, Arduboy2 dependency, EEPROM
dependency, Windows/SDL debug source, and packaged `MicroCity.hex` are not used.
The files under `upstream/` are the game logic and assets from
`Source/MicroCity`; `src/microcity_app.cpp` is the Zephyr/ZUI port layer.

Port boundaries:

- ZUI input state maps to MicroCity `INPUT_*` bits.
- Rendering uses a 128x64 `ZUI_BITMAP_FORMAT_MONO_VLSB` framebuffer.
- `GetPowerGrid()` returns that same 1024-byte buffer, matching the upstream
  Arduboy framebuffer scratch behavior.
- Save data uses `/extra/saves/microcity.dat` with a Zephyr-only versioned header
  and checksum. AVR EEPROM compatibility is not preserved.
- Long BACK/MENU/HOME or long T9 `*` exits through the app screen action-state
  handler; short BACK remains MicroCity's B button.

Build with the installed Rust `meshbus` CLI and a released app-profile EDK
for the intended Desktop host. Set `ZEPHYR_SDK_INSTALL_DIR` and install CMake
and Ninja; no Firmware source checkout is required:

```sh
meshbus llext --llext-sdk /path/to/app-edk.tar.xz -o build/llext \
  sdk-meshbus/samples/subsys/meshbus/services/llext/apps/microcity
```

Install the generated app in the extra partition under `/apps`, for example:

```sh
west mklfs -d build.microcity-host -o /tmp/microcity-extra-lfs.bin \
  --file build/llext/microcity.mba:/apps/games/microcity.mba
```

Manual validation checklist:

- MicroCity appears in the Desktop app launcher.
- Launch opens a fullscreen 128x64 screen.
- D-pad or T9 `2`/`8`/`4`/`6` moves menu selection and in-game cursor.
- SELECT, T9 `5`, or T9 `#` maps to MicroCity A and opens the toolbar from
  gameplay.
- Short BACK or T9 `*` maps to MicroCity B and confirms/cancels normal game
  actions.
- Long BACK or T9 `*` exits the `.mba` app, detaches the fullscreen layer, and
  returns to Desktop.
- New city creation reaches the gameplay screen.
- Save writes `/extra/saves/microcity.dat`; load restores the saved city after
  app relaunch.
- Relaunch after exit does not crash, leak callbacks, or reuse stale input
  state.
