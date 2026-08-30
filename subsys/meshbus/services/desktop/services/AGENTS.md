# Desktop Local Service Rules

Scope: Desktop-internal background/cache services that are not screens, apps,
or widgets. Parent Desktop and Meshbus service rules also apply.

- Keep these services UI-free and their APIs private to Desktop.
- Observe runtime signals and expose bounded cached snapshots to UI consumers.
- Protect shared mutable state with Zephyr synchronization primitives.
- Keep listener callbacks short and non-blocking; defer heavier work.
- Define teardown so listeners/work cannot update state after shutdown.
- Name a service after the shared state it provides, not one current consumer.
