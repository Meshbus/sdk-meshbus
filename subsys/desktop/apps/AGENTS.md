# Desktop Built-In Apps

Each app owns its registry entry, screens/router, state, subscriptions, work,
and cleanup on the shared app stack. Sources are listed explicitly in CMake.
Nearby apps illustrate current public ZUI APIs; they are not mandatory skeletons.

Keep a simple app in one translation unit. Split only a complete feature domain
with its state, callbacks, service work, and cleanup. Common rendering, lifecycle,
text, and evidence rules are maintained in [Desktop Runtime](../AGENTS.md).
