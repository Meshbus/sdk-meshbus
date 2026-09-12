# Public Meshbus Interfaces

These headers own public API/ABI declarations, service types, ZBus payloads,
registration descriptors, and extension metadata. Implementations live under
`subsys/`; affected public tests live under `tests/`.

Meshbus modules expose `include/<module>/<module>.h`. Narrow capabilities live
beside the module entry: Clock timestamps, GNSS heading, and LLEXT metadata/ZBus.
Use `mbs_<module>_*` for service functions, types and ZBus objects; use
`MBS_<MODULE>_*` for macros and enum values. Keep module ownership in exported
names, including observers owned by consuming modules. Service configuration
uses `CONFIG_MBS` and `CONFIG_MBS_*`; protobuf-generated names retain their
schema namespace. Do not add old-path forwarding headers or promote private
Settings, MCUmgr, or Shell helpers. Zephyr extension headers in
`zephyr/` retain their owning driver, binding, display, DFU, linker, or ZUI scope.

Flat service header guards use `MESHBUS_INCLUDE_<MODULE>_H_` for module
entries and `MESHBUS_INCLUDE_<MODULE>_<CAPABILITY>_H_` for narrow headers,
such as `MESHBUS_INCLUDE_CLOCK_TIMESTAMP_H_`. Keep the opening directives
and closing comment consistent. This project/path convention is separate
from the `mbs_` / `MBS_` service API namespace.

- Keep public headers minimal and implementation-neutral. Do not expose private
  state, locks, work items, settings handlers, shell helpers, or MCUmgr helpers.
- Treat public struct layouts, enum values, constants, registration metadata,
  and extension channel numbers as compatibility surfaces. Append stable IDs;
  do not renumber existing values except within an explicitly approved
  [pre-release migration](../docs/adr/0001-limit-the-first-firmware-ga-to-c2-client.md#pre-release-compatibility).
- Declare public ZBus channels in the owning service header. Document payload
  direction, valid lengths, units, ownership, and lifetime.
- Document pointer ownership, buffer sizes, units, persistence, and
  state-dependent errors. Use standard negative errno values.
- Keep feature-gated includes and declarations under the same Kconfig condition.
- When a public contract changes, update its implementation, public tests,
  schemas or adapters, and board-facing consumers in the same scoped work.
- When exported service symbols change, update EDK/MBA consumers according to
  [LLEXT compatibility](../subsys/llext/API_COMPATIBILITY.md). Verifying an old
  EDK archive does not establish that its compiled apps load in new firmware.

Before completion, check that no private implementation type escaped, public
payloads match their channel definitions, and affected ABI consumers remain
aligned.
