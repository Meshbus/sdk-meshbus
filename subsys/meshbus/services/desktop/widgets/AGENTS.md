# AGENTS.md - Desktop Dashboard Widgets

This directory contains the dashboard widget implementation for Meshbus
Desktop.

## Current Shape

- Dashboard widgets are split into one `*_widget.c` implementation per widget.
- Shared drawing and formatting helpers live in `widget_common.c/.h`.
- Widget ordering and tab titles are still defined in `widget_ids.h`.
- Widgets register through `MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(...)` near
  the widget implementation they expose.

## ZUI And Rendering Rules

- Use only the new ZUI draw API and Desktop runtime APIs.
- Do not reintroduce `zui_view`, `zui_canvas`, view models, or ViewDispatcher
  update callbacks.
- Draw callbacks must be pure: read prepared widget state and draw pixels only.
- Poll Meshbus services in tick/update paths, then request redraw only when the
  rendered state actually changes.
- Keep accepted dashboard visuals: header, widget body, page/tab indicator
  animation, icon sizes, and scroll behavior are hardware-validated surfaces.

## Text And Data

- User-visible widget text must come from `text/desktop_text.h` unless it is a
  widget tab title from `widget_ids.h`.
- Use fixed-size strings and numeric snapshots in widget state.
- Avoid service queries, allocation, blocking, or noisy logging in draw paths.

## Validation

Use the combine sample no-compat build for compile validation and hardware
feedback for pixel-level checks:

```sh
source ~/.zephyr/env/bin/activate
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  sdk-meshbus/samples/subsys/meshbus/combine -- \
  -DEXTRA_CONF_FILE=/tmp/fobe-zui-desktop-full.conf
```
