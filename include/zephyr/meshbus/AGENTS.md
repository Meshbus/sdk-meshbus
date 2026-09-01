# Public Meshbus Interfaces

These headers own public API/ABI declarations, service types, ZBus payloads,
registration descriptors, and extension metadata. Implementations live under
`subsys/meshbus/services/`; affected public tests live under `tests/`.

- Keep public headers minimal and implementation-neutral. Do not expose private
  state, locks, work items, settings handlers, shell helpers, or MCUmgr helpers.
- Treat public struct layouts, enum values, constants, registration metadata,
  and extension channel numbers as compatibility surfaces. Append stable IDs;
  do not renumber existing values.
- Declare public ZBus channels in the owning service header. Document payload
  direction, valid lengths, units, ownership, and lifetime.
- Document pointer ownership, buffer sizes, units, persistence, and
  state-dependent errors. Use standard negative errno values.
- Keep feature-gated includes and declarations under the same Kconfig condition.
- When a public contract changes, update its implementation, public tests,
  schemas or adapters, and board-facing consumers in the same scoped work.

Before completion, check that no private implementation type escaped, public
payloads match their channel definitions, and affected ABI consumers remain
aligned.
