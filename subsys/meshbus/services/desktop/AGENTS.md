# Meshbus Desktop Rules

Scope: the Desktop runtime and its child apps, local services, notifications,
registries, text, and dashboard widgets.

Desktop is a Zephyr library built on the public ZUI host/router/screen/draw and
component APIs. Keep Desktop as runtime and product-UI glue; reusable UI behavior
belongs in public ZUI, while app/widget behavior belongs to its owning child.

## Architecture

- `runtime.c` owns host/router lifecycle, render/input handoff, app attach/exit,
  display integration, and redraw scheduling.
- Root dashboard/menu files own top-level navigation.
- `apps/`, `widgets/`, and `services/` follow their nearest `AGENTS.md`.
- `notify/`, `registry/`, `assets/`, and `text/` retain their narrow roles.

Do not reintroduce removed legacy scene/view-dispatcher APIs. Avoid structural
renames or migration cleanup in the same patch as behavior changes unless the
user requests both.

## Runtime Rules

- Draw callbacks render prepared state only; they do not query services,
  allocate, block, or emit repeated logs.
- Use fixed-size state where practical and avoid heap churn in draw/input paths.
- Request redraw after state changes through the active host/runtime helper.
- Cancel and synchronize delayed work/listeners before releasing owned state.
- Keep user-visible strings in the central text interface and every selectable
  language pack, preserving format signatures.
- Preserve accepted interaction and pixel behavior unless a design change is
  explicitly requested.

## Validation

Run the focused Desktop integration scenario declared in its `testcase.yaml`.
When product composition or hardware visuals matter, build the current
Desktop-capable consuming firmware using that repository's documented qualified
target. Report unverified visual, timing, and physical-input behavior.
