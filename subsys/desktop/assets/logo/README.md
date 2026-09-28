# Meshbus boot logo

The logo uses solid letter faces and a lower-right depth contour.
`meshbus-green-light.svg` is the color vector source for generation;
`meshbus-silkscreen.svg` is an additional source reference.
`meshbus_84x56.png` and `meshbus_logo.h` are generated assets. The converter
extracts the seven paths in `letter-faces`, fits them uniformly into 82x54
pixels, and adds a two-pixel lower-right contour with a one-pixel black
separation. It deliberately replaces the color gradients, highlights and side
geometry with this pixel treatment. No antialiasing or dithering is used.

The complete asset remains 84x56, including its depth contour. The even-odd
fill preserves the two openings in B. On a 128x64 display it starts at (22, 4),
leaving 22 pixels on each side and 4 above and below.

The header contains 616 bytes of raw XBM data: 56 rows of 11 bytes, LSB first,
with unused row bits cleared. Foreground bits are 1; the ZUI/U8G2
adapter handles display byte order and the configured output inversion.

Regenerate from the repository root using Python with Pillow installed.
Keep previews and verification outputs in a task directory:

```sh
python3 \
  subsys/desktop/assets/logo/generate.py \
  --output-dir .scratch/boot-logo/generated
cp .scratch/boot-logo/generated/meshbus_logo.h \
   .scratch/boot-logo/generated/meshbus_84x56.png \
   subsys/desktop/assets/logo/
```

The same command creates 128x64 and nearest-neighbor 4x preview PNGs. The
converter deliberately supports only the source's rectilinear absolute
M/L/Z path; unsupported SVG geometry fails instead of silently changing it.

Desktop presents this logo once before its first dashboard frame. The default
hold is 1000 ms, controlled by `CONFIG_MBS_DESKTOP_BOOT_LOGO_DURATION_MS`;
0 disables the logo and delay. The hold is local to the Desktop thread and
navigation queued during it is discarded. Smaller or unavailable displays
and an already suspended UI skip it. This is a Desktop startup screen; it
appears after earlier firmware initialization, when Desktop has a display.

The bitmap can also be drawn by another ZUI consumer. Display-only firmware
does not automatically acquire a splash-screen lifecycle from this asset.
