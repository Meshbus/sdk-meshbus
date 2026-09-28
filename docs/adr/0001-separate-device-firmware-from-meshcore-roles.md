---
status: accepted
---

# Separate device firmware from MeshCore roles

A device has one firmware composition, independent of its MeshCore role.
The device profile selects services, storage, static capacities and boot policy;
Settings selects the protocol role among those the compiled services support.
This avoids multiplying firmware identities, partition layouts and release
artifacts for each role, and lets owners change roles without replacing firmware.

Role changes apply synchronously through the protocol runtime without rebooting
the device. Successful application commits the configuration and schedules
persistence; it does not promise immediate flash durability. Failed application
preserves the committed configuration and attempts to restore the prior runtime.
Compiled services continue to reserve their static RAM across role changes.
The [product guide](../../apps/meshbus/README.md#device-firmware-and-meshcore-role)
defines the current configuration behavior.

SDK board support, a registered product profile and a successful build are
separate from hardware qualification. Each release qualifies its Product Targets
and actual capabilities; evidence from a Qualification Fixture does not qualify
another target. The [distribution guide](../../DISTRIBUTION.md) owns release
procedures and qualification requirements.
