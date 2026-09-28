# Desktop-Local Services

These private, UI-free services own bounded cached state and runtime signal
observation for Desktop consumers. They are not screens, apps, or widgets.

- Keep their APIs private to Desktop and expose copied or immutable snapshots.
- Protect shared mutable state with Zephyr synchronization primitives.
- Notify consumers only after committed state changes. Common callback and
  teardown rules are maintained in [Desktop Runtime](../AGENTS.md).
