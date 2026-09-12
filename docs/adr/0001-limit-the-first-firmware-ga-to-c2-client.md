---
status: accepted
---

# Limit the first firmware GA to the C2 device

Firmware 1.0.0 will qualify only the C2 device as a Product Target. The DevKit
configurations remain deferred Qualification Fixtures. Their migration is not
part of the C2 device architecture change; buildability alone does not establish
a supported production BOM or hardware qualification.

## Consequences

- Firmware GA assembly must use an explicit Product Target set rather than treat
  every available board configuration as a required product.
- DevKit evidence must be labelled as fixture evidence and cannot qualify C2
  hardware behavior.

## Device firmware revision

The approved single-device-firmware spec replaces the original Client-qualified
product identity with `idea_mesh_tracker_c2/nrf54l15/cpuapp`. Device configuration
owns services and static capacities; Settings owns the MeshCore role, initially
CHAT. Role changes and MeshCore configuration reset apply synchronously on the
engine's owning workqueue without rebooting the device. Successful application
commits the configuration and schedules delayed persistence; success does not
guarantee immediate flash durability. Failed application returns an error and
attempts to restore the previously committed engine configuration. Static RAM
remains reserved for compiled services.

This 2026-09-07 revision supersedes the earlier save-then-reboot decision. The
behavior above and the [application guide](../../apps/meshbus/README.md#device-firmware-and-meshcore-role)
describe the current contract. Dated validation evidence is retained locally in
`.scratch/meshcore-settings-apply/README.md`; that optional historical record is
not distributed with source checkouts and is not required to interpret this ADR.

This changes the original role-qualified identity without expanding the GA device
set or changing the signed single-app recovery policy.

## Pre-release compatibility

Before the first firmware release, an explicitly approved schema or ABI migration
need not preserve compatibility with earlier engineering candidates. Update the
affected implementations, tests, schemas, adapters, and consumers together as
required by the [public-interface rules](../../include/AGENTS.md).
This exception does not apply to released contracts or authorize arbitrary
renumbering of protocol-defined identifiers or changes outside the approved scope.
