# Desktop Dashboard Widgets

Widgets own their registry entry, prepared display state, update/tick behavior,
and dashboard rendering. Shared drawing/formatting helpers stay narrow.

- Poll services or update snapshots outside draw callbacks, then request redraw
  only when rendered state changes.
- Keep prepared display state fixed-size and do not mutate it during drawing.
- Keep shared helpers free of widget-specific behavior.

Common rendering, lifecycle, text, and evidence rules are maintained in
[Desktop Runtime](../AGENTS.md).
