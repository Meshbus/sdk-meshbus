# Meshbus Public API And ABI Rules

Scope: public headers under `include/zephyr/meshbus/`.

Service runtime, persistence, shell, MCUmgr, workqueue, and device policy belong
under `subsys/meshbus/services/`.

## Boundary

Anything declared here is a public API or ABI contract. Keep headers minimal,
implementation-neutral, and stable. Do not expose service mutexes, work items,
settings handlers, transport helpers, or mutable internal state.

When an observable declaration changes, align the implementation and affected
public tests. Update samples or external consumers when their use changes.

## Header Shape

Prefer this order:

1. File banner and concise Doxygen `@file` description.
2. Include guard and minimal includes.
3. `extern "C"` guard.
4. Forward declarations and typedefs.
5. Public constants, types, channels, registration macros, and functions.
6. Language and include-guard closes.

Use standard headers before Zephyr headers and generated/local headers. Prefer
forward declarations when only pointer types are needed. Keep feature-gated
includes and declarations under matching Kconfig conditions.

## Names And Types

- Use `meshbus_<service>_*` for new symbols and `MESHBUS_<SERVICE>_*` for
  macros.
- Preserve shipped names and numeric values unless a breaking change is
  intentional and explicitly scoped.
- Prefer the service-owned protobuf type for protobuf-backed configuration;
  avoid parallel public C representations.
- Treat public struct layouts, enum values, registration descriptors, metadata
  formats, and bridge/channel numbers as ABI.
- Append ABI enum values; do not renumber existing values.

## Public Channels And Functions

Declare a public ZBus channel only in its owning service header. Document event
direction, payload lengths, units, ownership, lifetime, and valid states. Keep
validators, observers, callbacks, and work handoff in implementation files.

Public getters must copy data or document an immutable snapshot contract. For
configuration APIs, keep get/set/reset behavior and persistence semantics
consistent across services unless the service has a documented reason to
differ.

Use concise Doxygen for public functions, types, ABI constants, and channels.
Document argument ranges, units, buffer sizes, ownership, state-dependent
errors, and standard negative errno returns.

## Sensitive ABI Areas

Desktop registration/event descriptors and LLEXT metadata, section names,
limits, restart/state values, and ZBus bridge numbers have external consumers.
Read the nearest Desktop rules and inspect those consumers before changing
these surfaces.

## Completion

Before finishing, verify declarations against enabled implementations,
generated protobuf schemas, public channel payloads, tests, and any affected
Desktop/LLEXT consumers. Avoid maintaining a static header inventory here; the
directory and build metadata are authoritative.
