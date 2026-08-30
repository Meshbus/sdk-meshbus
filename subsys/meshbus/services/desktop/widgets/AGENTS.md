# Desktop Dashboard Widget Rules

Scope: dashboard widgets under this directory. Parent Desktop rules also apply.

- Keep one coherent implementation per widget and shared drawing/formatting in
  narrow common helpers.
- Register widgets through the existing Desktop registry without renumbering or
  destabilizing public descriptors.
- Draw callbacks read prepared widget state and render only.
- Poll services in update/tick paths and request redraw only when rendered state
  changes.
- Use fixed-size snapshots and centralized Desktop text; keep tab-title ownership
  consistent with the current registry.
- Preserve accepted header, body, tab/page, icon, animation, and scroll behavior
  unless a design change is requested.

Validate through the focused Desktop integration scenario and the current
Desktop-capable consuming application when composition changes. Hardware visual
and timing behavior remains unverified until explicitly exercised.
