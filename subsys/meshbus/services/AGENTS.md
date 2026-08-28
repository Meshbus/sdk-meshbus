# Meshbus Service Implementation Rules

Scope: service implementations under `subsys/meshbus/services/`.

Non-scope: public header shape, public ABI, Doxygen policy, and exported type
rules. See `include/zephyr/meshbus/AGENTS.md` for those.

## 1. Service Shape

For a service under `services/<svc>/`, keep the standard structure:

- `CMakeLists.txt`, guarded by `CONFIG_MESHBUS_<SVC>`.
- `Kconfig`, with service menuconfig, log template, and feature toggles.
- `<svc>.c` for core runtime behavior.
- Optional `shell.c`, `mgmt.c`, and private headers.

Wire new services into:

- `subsys/meshbus/services/CMakeLists.txt`
- `subsys/meshbus/services/Kconfig`
- `include/zephyr/meshbus/<svc>.h` when the service exposes public API or channels.

Prefer this `.c` order: includes, log module, ZBus definitions, stats,
Devicetree devices, constants/defaults, state, declarations, helpers, work/I/O,
settings, public API implementation, init hooks.

For non-trivial services, keep the primary `<svc>.c` readable with standard
section banners instead of splitting files too early:

```c
/* -------------------------------------------------------------------------- */
/* Defaults And State                                                          */
/* -------------------------------------------------------------------------- */
```

Use short title-case section names. Good section order is:

- ZBus Channels
- Statistics
- Devices
- Defaults And State
- Declarations
- Device And Config Validation
- Cached Data And Time Helpers
- Runtime PM And Hardware Apply
- Settings Schema And Apply
- Callbacks And Work
- Public API
- Power Callback
- Initialization

Only add `settings.c`, private headers, or other split files when the split
isolates a real subsystem boundary. Do not split a config-only lifecycle where
validate/apply/settings load/export/reset become harder to follow across files.

## 2. State And Concurrency

- Split persisted config/settings state from runtime/I/O state.
- Treat settings/config locks as outer locks; define lock ordering when multiple
  locks can be held.
- Do not hold config/state mutexes across driver calls that can block,
  settings/filesystem writes, sleeps, or callbacks.
- A dedicated writer mutex may span bounded settings I/O when the persisted
  collection itself requires transaction serialization. Keep ordinary reads
  outside that mutex, document the lock order, and release it before callbacks
  or ZBus publish.
- Mirror hot-path flags into `atomic_t` when callbacks or listeners need cheap
  reads.
- Public getters must copy data under lock or use an explicit immutable snapshot
  contract; do not return mutable internal state after unlocking.

## 3. Devices, Runtime PM, And Optional Hardware

- Use `DT_HAS_CHOSEN(meshbus_<svc>)` and `DEVICE_DT_GET(DT_CHOSEN(...))` for a
  main service device when applicable.
- Centralize readiness in a helper such as `<svc>_device_ready()`.
- Missing optional hardware is normal: return `-ENODEV` or a clear unavailable
  status, and avoid log spam in periodic work.
- For sampling or externally powered devices, pair runtime PM get/put carefully.
- Do not persist an enabled config until required runtime PM or power-domain
  claims succeed; rollback or force disabled state on failure.

## 4. ZBus Runtime Rules

- Public channel declarations belong in headers; channel definitions and
  observer wiring belong in the service.
- Validators must be fast and side-effect free.
- ZBus callbacks should copy lightweight state and hand blocking work to a work
  item or dedicated workqueue.
- Use `zbus_chan_pub(..., K_NO_WAIT)` in hot paths.
- Log high-rate publish failures at `LOG_DBG`; use `LOG_WRN` for rare control
  events.

## 5. Settings Persistence

Default persisted config shape is protobuf blob settings:

- subtree: `meshbus/<svc>`
- scalar config key: `config`
- bounded record key: `<collection>/<idx>`
- extra raw per-slot payload key, when needed: `<collection>_<leaf>/<idx>`

Current Meshbus-owned persistent services use these keys:

- scalar config: `meshbus/<svc>/config`
- Channel slot: `meshbus/channel/<idx>`
- MeshCore config: `meshbus/meshcore/config`
- Contact slot: `meshbus/contact/<idx>`
- Contact advert raw: `meshbus/contact/advert_raw/<idx>`

Contact path semantics:

- Treat `out_path.size == 0 && is_neighbor == true` as a known zero-hop direct
  path. Treat `out_path.size == 0 && is_neighbor == false` as unknown path,
  meaning peer-targeted MeshCore sends should use flood fallback.
- `has_out_path == true` on Contact path/advert response events means known path
  metadata is present; `out_path_len == 0` is valid and marks zero-hop direct.
  Do not use byte length alone as the direct/flood discriminator.

Rules:

- Use whole protobuf blobs for Meshbus-owned scalar configs and bounded slot
  records unless a new task explicitly documents a different storage ABI.
- Prefer common blob helpers in `subsys/meshbus/common/settings.h` and
  `settings.c` for blob load/save/export/delete and indexed blob records.
- Blob settings values are Meshbus storage records, not naked nanopb payloads:
  write and read them only through the common helpers so the magic/version header
  is preserved.
- Do not introduce full-record DRAM caches for Channel or Contact records; keep the
  small metadata indexes and load slot blobs on demand.
- Stage settings during load, apply once in commit, and avoid re-saving the
  initial loaded config.
- For scalar config apply paths, use the normal sequence: validate input, short
  hold `settings_mutex` only to snapshot old config/runtime flags, release it,
  serialize hardware/runtime work with an apply mutex when needed, then short
  hold `settings_mutex` again to commit config and atomic mirrors.
- Coalesce runtime writes through delayed persistence work.
- A shutdown hook may run on the system workqueue. Do not enqueue required
  persistence back onto that queue; quiesce delayed writes and complete the
  bounded write synchronously before the power action continues.
- Reset by cancelling persistence work, applying defaults through the normal
  path, and deleting all keys owned by the service.
- Ignore malformed keys, unknown tags, invalid lengths, and invalid booleans
  with warnings; bad settings must not block boot.
- Do not hold `settings_mutex` across driver calls, runtime PM operations,
  settings/filesystem writes, work sync cancellation, sleeps, callbacks, or ZBus
  publish. If an apply step can fail after touching hardware, restore the old
  state or expose an explicit error/degraded state.
- Meshbus persistent build surfaces should use the Settings ZMS backend with
  ZMS lookup cache and Settings ZMS linked-list cache by default. RAM-constrained
  aggregate builds such as `samples/subsys/meshbus/combine` may explicitly leave
  these caches disabled.

## 6. Shell And MCUmgr

Shell:

- Use command namespace `meshbus <svc> ...`.
- Prefer stable `status`, `config`, `reset`, and `trigger` commands.
- Use helpers in `subsys/meshbus/common/shell.h` for parsing and consistent
  invalid-argument output.

MCUmgr:

- Keep `mgmt.c` as a thin transport adapter: decode, validate, call public API
  or publish to the existing ZBus path, encode.
- Do not duplicate normal config validation in `mgmt.c`; keep public config set
  APIs as the authority. Use `MB_MGMT_CONFIG_SET_HANDLER_DEFINE_NO_VALIDATE`
  for standard config set handlers that can rely on the service API validation.
- Prefer proto-over-CBOR helpers from `subsys/meshbus/common/mgmt.h`.
- Use generated service proto group/command enums instead of local opcode
  defines for new or normalized work.
- Keep command-specific request/response messages; do not overload one body for
  unrelated operations.
- Reuse shell-visible semantics; do not fork service behavior for mgmt.
- MCUmgr group IDs come from the generated service protobuf enums
  (`meshbus_<Service>MgmtGroupId_*`). Do not maintain local numeric group ID
  lists in service code or docs; update the owning `.proto` schema first, then
  use the generated enum in `mgmt.c`.

Use `services/radio/` as the reference for normalized config/settings/mgmt
patterns. Older services do not need churn-only rewrites, but substantial edits
should move them toward the normalized pattern.

## 7. Logging, Stats, And Errors

- `LOG_INF`: service ready once, major enable/disable transitions, config changes.
- `LOG_DBG`: high-rate events and periodic-loop details.
- `LOG_WRN`/`LOG_ERR`: rare invalid input, driver failure, or recovery-worthy
  conditions. Use warn-once flags for repeated optional-hardware failures.
- Stats, when enabled, should count TX/RX, publish failures, and key error paths.
- Default errors:
  - `-EINVAL`: invalid args or validation failure.
  - `-ENODEV`: missing/not-ready hardware or service unavailable.
  - `-EBUSY`: state conflict or resource busy.
  - `-ENOTSUP`: unsupported optional capability.

## 8. Safety Pitfalls

- Fixed-size string writes must zero-init or tail-clear and explicitly NUL
  terminate.
- Reconfiguration paths must quiesce delayed work before touching hardware state.
- If work cancellation cannot happen in the current context, use a two-step
  handoff to avoid races.
- Bounded slot tables need explicit reuse/reclaim semantics.
- Keep Kconfig defaults, ranges, and help text aligned with runtime behavior.
- LLEXT load paths must actually load/bring up and record ownership; unload
  paths must teardown, unload, and clear registry state.
