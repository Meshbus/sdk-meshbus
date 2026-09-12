# Ard Drivin LLEXT app

This sample ports `rveilleux/ard-drivin` to a Meshbus Desktop LLEXT app.

- Repository: `https://github.com/rveilleux/ard-drivin`
- Upstream snapshot: `78b6a0730878a622f3ca5b0c8fffd572507cd9b1`
- License: MIT

The `upstream/` directory is a verbatim fixed snapshot of the upstream
repository. The app-local `src/` bridge uses the shared Arduino/AVR compatibility
headers from the `meshbus-arduboy` SDK and reimplements the upstream
`ArduboyRem` hardware boundary on top of ZUI, without editing upstream game
sources.

## Port notes

- Rendering uses the upstream 128x64 page-major framebuffer directly through
  `ZUI_BITMAP_FORMAT_MONO_VLSB`.
- The upstream flicker-gray clear pattern is preserved in the Zephyr
  `paintScreen()` replacement.
- ZUI input state maps to the upstream Arduboy 1.0 button bit layout.
- `ArduboyTones::tone()` is routed to the Meshbus indicator buzzer as short
  single-channel notes.
- Upstream EEPROM calls are backed by a 1024-byte LittleFS mirror at
  `/extra/saves/ard_drivin.dat`, so the audio enable flag persists across app
  launches.

For EDK, service namespace migration and external Meshbus Arduboy SDK setup,
see the [shared MBA build requirements](../../README.rst).
