# Desktop Built-In App Rules

Scope: built-in apps under this directory. Parent Desktop and service rules also
apply.

## Ownership And Layout

Apps register through the Desktop app registry and run on the shared app stack.
Each app owns its router/screens, fixed-size state, subscriptions, work, and
cleanup.

A simple app may remain one translation unit. Split a complex app by complete
feature domains so callbacks, state, service calls, async work, and cleanup stay
together. Keep shared app lifecycle helpers small; do not move app business
logic into generic `app_common` files or public headers. List sources explicitly
in CMake.

Use current public ZUI host/router/screen/draw and component APIs. Do not recreate
legacy scene/view layouts or setter-heavy compatibility APIs.

## State And Interaction

- Allocate app-owned state on open and release it on exit.
- Keep draw callbacks pure and input callbacks bounded.
- Move blocking or stack-heavy service operations to app work/thread context.
- Disable new callbacks, wait for in-flight callbacks when needed, and cancel
  work synchronously before freeing state.
- Reset transient workflow state when entering a fresh flow; preserve it only
  for deliberate child-return behavior.
- Preserve consistent BACK/OK behavior and explicit Apply/Cancel semantics.
- Read service snapshots when a status page opens or is explicitly refreshed;
  avoid background refresh work unless the interaction requires it.
- Keep user-visible strings in the Desktop text packs with matching format
  signatures.

Use existing app patterns as examples, not immutable repository-wide templates.
Keep private IDs and types local to the app and name them after the app domain.

## Validation

Run `git diff --check`, grep touched code for removed legacy ZUI APIs, and run
the focused Desktop integration scenario. Build the current consuming product
application when source composition or hardware behavior is affected. State
unverified visual and interaction coverage.
