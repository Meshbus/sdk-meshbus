# Desktop Built-In Apps

Each app owns its registry entry, screens/router, state, subscriptions, work,
and cleanup on the shared app stack. Sources are listed explicitly in CMake.
Nearby apps illustrate current public ZUI APIs; they are not mandatory skeletons.

- Keep a simple app in one translation unit. Split only a complete feature
  domain with its state, callbacks, service work, and cleanup.
- Prefer fixed-size app-owned state. Draw callbacks render prepared state only;
  service calls and blocking work run outside draw and input callbacks.
- Disable new callbacks, wait for in-flight access when needed, and cancel work
  before freeing app state.
- Request redraw after state changes. Keep user-visible strings in the Desktop
  text contract and preserve format signatures across language packs.
- Preserve accepted navigation and visual behavior unless the task explicitly
  changes it. Treat QEMU integration, product compilation, and physical UI
  observation as separate evidence.
