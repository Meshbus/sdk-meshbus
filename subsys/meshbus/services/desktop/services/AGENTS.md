# AGENTS.md - Desktop Local Services

This directory contains Desktop-internal services that are not UI screens,
widgets, or built-in apps.

Use this area for code that continuously observes Meshbus/Zephyr runtime
signals, commonly through zbus listeners, and maintains shared Desktop state
for apps or dashboard widgets.

Rules:

- Keep services UI-free. They may expose cached state to apps/widgets, but must
  not own ZUI screens or draw logic.
- Keep service APIs private to Desktop. Do not expose them through public
  Meshbus or ZUI headers.
- Protect shared mutable state with Zephyr synchronization primitives.
- Listener callbacks must stay short and non-blocking; defer heavy work to a
  work item if needed.
- Name files by the shared state they provide, for example `messages_cache.*`,
  instead of by a UI consumer.
