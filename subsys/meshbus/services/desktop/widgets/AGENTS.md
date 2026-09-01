# Desktop Dashboard Widgets

Widgets own their registry entry, prepared display state, update/tick behavior,
and dashboard rendering. Shared drawing/formatting helpers stay narrow.

- Poll services or update snapshots outside draw callbacks, then request redraw
  only when rendered state changes.
- Draw prepared fixed-size state without service queries, allocation, blocking,
  mutation, or noisy logging.
- Keep user-visible text in the Desktop text contract and shared helpers free of
  widget-specific behavior.
- Preserve accepted visual and interaction behavior unless explicitly changed;
  compilation does not prove pixel-level hardware behavior.
