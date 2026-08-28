# AGENTS.md - Meshbus Desktop UI module

This directory contains the Meshbus Desktop runtime built on the new FoBE ZUI
public API.

## Current Architecture

- Desktop is a Zephyr library consumed by integration applications such as
  the separate firmware repository's `app/`.
- The active UI runtime is `runtime.c`.
- ZUI usage must go through the new public API:
  `<zephyr/zui/zui.h>`, `zui_host`, `zui_router`, `zui_screen`,
  `zui_draw`, widgets, and `zui_toast`.
- Do not add new Desktop code that depends on legacy ZUI concepts such as
  `zui_view_dispatcher`, `zui_scene_manager`, `zui_view`, `zui_canvas`,
  `zui_gui`, scenes, or view ports.
- The old Desktop scenes/views/app-subdirectory implementation has been
  removed during the ZUI migration cleanup. Treat any reference to those paths
  as stale unless a completed plan is being read for history.

## Active Layout

- `runtime.c`: host/router runtime, render thread, input queue drain, app
  opening/exit glue, sleep/display integration, and redraw scheduling.
- `dashboard.c`: root dashboard screen, active widget dispatch, tab animation,
  and dashboard widget tick scheduling.
- `main_menu.c`: BACK-opened root app list and app selection.
- `power_menu.c`: long-BACK root power menu and confirmation modal.
- `desktop_input.c`: Zephyr input bridge and shell-injected input routing.
- `notify/`: Desktop UI notification adapters that translate runtime events,
  such as Bluetooth zbus state/pairing events, into host-owned `zui_toast`
  feedback.
- `services/`: Desktop-local background/cache services, such as zbus listeners
  that maintain shared state for apps or widgets.
- `apps/`: built-in app implementations as direct new-ZUI screen/widget
  composition.
- `widgets/`: dashboard widgets split into one implementation file per widget
  plus shared helpers.
- `registry/`: iterable app and dashboard-widget registration.
- `assets/`: retained Desktop icons and generated bitmap/font assets.
- `text/`: centralized user-visible Desktop strings.

Keep production naming stable. Do not mix behavior changes with structural
renames or file-split cleanup.

## Build / Verify

Run from the west topdir:

```sh
source ~/.zephyr/env/bin/activate
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  app
```

Do not run `west flash` unless the user explicitly requests hardware flashing.

## Coding Rules

- Keep Desktop as runtime glue. UI behavior should live in public ZUI modules
  or in the owning built-in app/widget implementation.
- Keep visual and interaction behavior pixel-level compatible with the accepted
  hardware behavior unless the user explicitly requests a design change.
- Use negative errno return values.
- Keep user-visible text in `text/desktop_text.h` and the selected
  `text/<locale>/desktop_text.c` language pack. Every selectable pack must
  define the full header symbol set and preserve format argument signatures.
- Prefer fixed-size app/widget state. Avoid heap churn in draw paths.
- Draw callbacks must only render current state; they must not query services,
  allocate, block, or log repeatedly.
- Request redraw through the active `zui_host` or Desktop redraw helper after
  state changes.
- Cancel delayable work before freeing app/widget state.
- Do not reintroduce legacy LLEXT exports while the cleanup plan is active.

## Validation Checklist

Before finishing Desktop changes:

1. Confirm new code includes only new ZUI public headers.
2. Run a focused grep for legacy ZUI names in active Desktop sources.
3. Build the Desktop-capable host application when behavior changed.
4. State any unverified hardware visual/interaction coverage in the final
   response.
