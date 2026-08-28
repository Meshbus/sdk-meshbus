# AGENTS.md - Desktop Built-In Apps

This directory contains built-in Meshbus Desktop apps implemented directly with
the new ZUI public API. Treat `radio/` as the current reference architecture
for a complex app: one app-local private header, one lifecycle/registration
entry file, and one implementation file per complete feature domain.

## Architecture Model

- Apps register through `MESHBUS_DESKTOP_APP_DEFINE(...)` and are opened by the
  Desktop runtime on the shared app stack.
- Each app owns its `zui_router`, app screens, ZUI widget instances, fixed-size
  state, service subscriptions, work items, and cleanup.
- Desktop remains runtime glue. App UI behavior belongs either in public ZUI
  components or in the owning app implementation.
- Do not recreate legacy scene/view layouts, `*_ids.h`, `*_app_priv.h`, or old
  per-scene directories. The old Desktop scene/view implementation was removed.

## Reference Layout

Use the Radio app pattern for complex built-in apps. New app directories should
use `<app>.c` as the main entry file; existing `<app>_app.c` files may be
renamed separately when doing dedicated cleanup.

- `<app>.c`: app entry point, allocation, router/screen construction,
  app-stack attach/detach, registration, and top-level cleanup ordering.
- `<app>_private.h`: app-local IDs, limits, model/state structs, shared helper
  declarations, and screen ops declarations. It is private to the app
  directory, not a public API.
- `<app>_common.c`: small cross-domain helpers such as redraw, toast, shared
  input predicates, screen switching, and generic formatting.
- `<app>_<domain>.c`: one complete feature domain, keeping its selected
  callbacks, draw/input callbacks, service calls, async/listener state, work
  items, and cleanup helpers together.
- `CMakeLists.txt`: list every app domain source explicitly. Do not hide app
  source selection behind globs.
- `app_common.[ch]`: small shared helpers for app lifecycle/input plumbing
  only. Do not put app business logic or app-specific screen composition here.

For simple apps, a single translation unit is acceptable. Split only when a
complete domain can move together; do not split only `draw.c`, `input.c`, or
`helpers.c` away from the state they mutate.

## ZUI API Boundary

Allowed ZUI surface:

- `zui_host`, `zui_router`, `zui_screen`
- `zui_draw`
- current ZUI components such as `zui_list`, `zui_sublist`, `zui_form`,
  `zui_text_editor`, `zui_number_editor`, `zui_hex_editor`, `zui_text_view`,
  `zui_modal`, `zui_progress`, `zui_file_picker`, and `zui_toast`

Disallowed for app code:

- `zui_view_dispatcher`
- `zui_scene_manager`
- `zui_view`
- `zui_canvas`
- `zui_gui`
- old setter-heavy legacy widget APIs

## App State

- Prefer one app-owned state struct allocated when the app opens and destroyed
  when it exits.
- Keep local buffers fixed-size and scoped to app state. Avoid heap churn in
  draw paths and hot input paths.
- Keep private app types named after the app domain, for example
  `struct radio_settings`, not `struct zui_*`, unless they are real public ZUI
  types.
- Put user-visible strings in `text/desktop_text.h` and every selectable
  `text/<locale>/desktop_text.c` pack; app code should reference text constants.
  Keep language-pack symbols and format argument signatures in parity.
- Keep app-local IDs and layout constants in the private header when shared
  across domains. Keep domain-only tables and constants inside the owning
  domain `.c`.

## Screen And Component Rules

- Launcher-style menus use `zui_list`; titled app submenus use `zui_sublist`.
- Settings pages use `zui_form` and must preserve Apply/Cancel semantics.
- Modal choices use `zui_modal`; transient feedback uses `zui_toast`.
- Editors must route BACK, OK, and long-OK consistently with the ZUI component
  contract.
- App BACK behavior must return to the previous app screen or detach the app
  router according to the accepted flow.
- Entering a fresh workflow from a parent menu must reset transient page state
  before switching screens. Preserve in-progress state only for deliberate
  child-return paths such as target pickers, editors, confirmation modals, or
  reply flows.
- Status pages are snapshots: read current service state when the page opens,
  refresh it on long-OK, and preserve the current scroll position across that
  manual refresh. Do not add periodic Status-page refresh work unless the
  accepted interaction explicitly requires it.
- For `zui_form` rows that navigate to a subpage, use
  `DESKTOP_TEXT_COMMON_ROUTE` as the value plus
  `ZUI_FORM_VALUE_ALIGN_RIGHT`; do not pad text constants with spaces for
  layout. Direct request/action rows should express the action in the label
  with a leading `> ` and keep the value empty.
- Preserve accepted hardware behavior and pixel layout unless the user asks for
  a design change.

## Service And Threading Rules

- Draw callbacks render prepared state only. They must not query services,
  allocate, block, or log repeatedly.
- Service calls, zbus listeners, async work, and spawned threads belong to the
  owning domain file.
- If an app uses listeners, work items, delayable work, or spawned threads,
  cleanup must disable new callbacks, wait for in-flight callbacks when needed,
  cancel work synchronously, and prevent callbacks from touching freed app
  state.
- Potentially blocking or stack-heavy service operations should run from app
  work/thread context, not directly in the input callback.
- Request redraw through the app host after state changes.

## Build And Validation

Default integration build from the west topdir:

```sh
source ~/.zephyr/env/bin/activate
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  sdk-meshbus/samples/subsys/meshbus/combine
```

When the no-compat config is available, also build with:

```sh
source ~/.zephyr/env/bin/activate
west build -p auto -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
  sdk-meshbus/samples/subsys/meshbus/combine -- \
  -DEXTRA_CONF_FILE=/tmp/fobe-zui-desktop-full.conf
```

Before finishing app changes:

1. Run `git diff --check` on touched app files.
2. Grep touched app code for legacy ZUI names.
3. Build the combine sample when behavior or source layout changed.
4. State any unverified hardware visual/interaction coverage in the final
   response.
