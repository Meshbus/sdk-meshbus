# Meshbus Service Implementation Rules

Scope: service implementations under `subsys/meshbus/services/`.

Public API and ABI rules live in `include/zephyr/meshbus/AGENTS.md`. Service-
specific source, Kconfig, CMake, devicetree, and tests remain authoritative.

## Service Shape

A service normally owns guarded CMake/Kconfig entries, one readable core source
file, and optional transport or feature files such as `mgmt.c` and `shell.c`.
Wire new services into the parent CMake and Kconfig files and add a public header
only when a stable external contract is needed.

Keep related state and lifecycle code together. Split a file only when the split
isolates a real subsystem boundary; do not create one file per callback or API.
Use clear title-case section banners in long source files and follow nearby
ordering rather than enforcing a repository-wide empty skeleton.

## State And Concurrency

- Separate persisted configuration from runtime/I/O state.
- Define lock ordering when locks can nest.
- Do not hold ordinary state/config locks across driver calls, settings or
  filesystem I/O, sleeps, callbacks, work cancellation, or ZBus publication.
- A dedicated transaction mutex may span bounded persistence I/O when the
  collection requires serialization; document why.
- Copy public getter results under lock or expose an explicit immutable
  snapshot.
- Use atomic mirrors only for genuinely hot callback/listener flags.
- Quiesce asynchronous work before reconfiguration or teardown, and prevent
  callbacks from accessing released state.

## Devices And Runtime PM

Prefer devicetree chosen nodes and central readiness helpers over board-specific
constants. Missing optional hardware is a normal `-ENODEV`/unavailable path and
must not cause periodic log spam.

Pair runtime-PM and power-domain claims carefully. Do not commit an enabled
configuration until required hardware/runtime changes succeed; rollback or
expose a clear degraded state after partial failure.

## ZBus

Public declarations belong in the owning header; definitions and observer
wiring belong in the service. Validators must be fast and side-effect free.
Callbacks should copy small state and defer blocking work. Use bounded waits and
non-blocking publication in hot paths, with debug-level handling for expected
high-rate contention.

## Persistence

Use the common Meshbus settings helpers for Meshbus-owned protobuf blob records
so storage headers and versions remain consistent. Keep service keys and
migration semantics documented next to their implementation or public contract,
not in this broad rule file.

Stage settings during load and apply them once at commit. Coalesce runtime
writes when appropriate, avoid resaving an unchanged initial load, and make
reset cancel pending persistence before restoring defaults and deleting only
service-owned keys. Malformed or unknown records should be rejected or ignored
without blocking boot.

Do not add large DRAM caches merely to simplify persistent collections. Keep
failure, rollback, shutdown, and reboot behavior observable through the public
service contract.

## Shell And MCUmgr

Keep shell commands under `meshbus <service>` with stable status/config/reset or
service-specific action semantics. Reuse common parsing helpers.

Keep `mgmt.c` as a thin transport adapter: decode, call the public service path,
and encode. Validation belongs in the service API. Prefer generated protobuf
group and command enums; change the owning schema instead of duplicating numeric
IDs locally.

## Logging And Errors

Use informational logs for readiness and major transitions, debug logs for
high-rate events, and warning/error logs for actionable failures. Avoid repeated
optional-hardware noise. Use standard errno meanings consistently, including
`-EINVAL`, `-ENODEV`, `-EBUSY`, and `-ENOTSUP`.

## Validation

Update public tests when observable behavior changes. Select the narrow service
suite and closest consuming sample from current YAML. Check fixed-size string
termination, delayed-work teardown, bounded collection reuse, Kconfig/runtime
alignment, and load/unload ownership where applicable.
