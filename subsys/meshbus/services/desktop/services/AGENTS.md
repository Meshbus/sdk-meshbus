# Desktop-Local Services

These private, UI-free services own bounded cached state and runtime signal
observation for Desktop consumers. They are not screens, apps, or widgets.

- Keep their APIs private to Desktop and expose copied or immutable snapshots.
- Protect shared mutable state with Zephyr synchronization primitives.
- Keep listeners short and non-blocking; defer heavy work to an owning work
  item and notify consumers only after committed state changes.
- Stop observations, cancel work, and prevent late callbacks during shutdown.
