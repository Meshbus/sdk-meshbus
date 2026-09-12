# Meshbus Service Implementations

These rules apply to Meshbus service directories and the shared `settings`,
`mgmt`, and `shell` modules. The adjacent `dfu`, `u8g2`, and `zui` components
retain their own responsibilities.

Services own runtime state, synchronization, persistence, device/PM policy,
ZBus wiring, and thin shell/MCUmgr adapters. Public declarations live under
`include/<module>/`; local CMake/Kconfig and tests define enabled behavior.

Use the `mbs_` / `MBS_` service namespace, including private shared helpers,
ZBus objects, registration descriptors, and Kconfig. Preserve the separately
owned protobuf namespace, existing devicetree chosen names, persisted keys,
and protocol/metadata values when renaming C identifiers.

- Keep persisted configuration separate from runtime and I/O state. Define lock
  ordering where locks may nest, and return copied or immutable snapshots from
  public getters.
- Do not hold state or configuration locks across driver calls, settings or
  filesystem I/O, sleeps, callbacks, work cancellation, or ZBus publication.
- Keep validators and listeners short and non-blocking. Copy lightweight state
  in callbacks and hand blocking work to an owning work item or workqueue.
- Public ZBus declarations belong in the service header; definitions, validators,
  observers, and handoff stay in the implementation. Use bounded publication in
  hot paths and avoid high-rate warning logs.
- Treat missing optional hardware as an unavailable capability. Pair runtime PM
  claims and releases, avoid repeated error logs, and do not commit an enabled
  configuration after hardware apply fails.
- Use the shared settings helpers for Meshbus-owned records. Stage loads, apply
  once at commit, coalesce runtime writes, remove every owned key on reset, and
  prevent malformed records from blocking boot.
- Keep shell and MCUmgr files as transport adapters over the public service API
  or established ZBus path. Do not fork validation or business behavior there.
- Keep service-specific keys, migrations, lock details, and lifecycle caveats
  beside the owning code or public contract.

Validate through public observable behavior. Keep deterministic contract tests,
focused integration seams, and real-device claims separate.
