# Architecture decisions

These records explain current design constraints and their trade-offs for SDK
integrators and contributors. Product configuration and public APIs remain the
technical sources of truth. An accepted decision is not evidence that a device
or release has passed qualification.

| ADR | Decision | Scope |
| --- | --- | --- |
| [0001](0001-separate-device-firmware-from-meshcore-roles.md) | Separate device firmware from MeshCore roles | Device identity, runtime settings and qualification boundaries |
| [0002](0002-separate-firmware-and-cli-release-trains.md) | Separate firmware and CLI release trains | Independent versions and release cycles |
| [0003](0003-signing-and-update-trust-roots.md) | Make image authentication optional and keep update trust separate | Public hash-only builds, downstream signing and DFOTA metadata |
| [0004](0004-owner-trusted-mba-applications.md) | Run owner-trusted native MBA applications | Desktop extension scope, compatibility and lack of isolation |
| [0007](0007-owner-replaceable-mcuboot-products.md) | Keep replacement and recovery under owner control | Mesh Probe R2 MCUboot physical recovery and rollback |
| [0012](0012-restrict-compiled-third-party-licenses.md) | Restrict compiled third-party licenses | Dependency admission for proprietary use |
| [0013](0013-license-meshbus-under-apache-2-0.md) | License Meshbus-owned content under Apache 2.0 | Repository-owned content and the Apache EDK contract |

Keep records focused on durable decisions and the reasons for them. Update
scope when the architecture changes; keep target inventories, commands,
dependency versions and validation results in their owning documentation.
ADR identifiers are stable references and need not be consecutive.

- [Domain vocabulary](../../CONTEXT.md)
- [Product composition](../../apps/meshbus/README.md)
- [Build and distribution procedures](../../DISTRIBUTION.md)
- [Engineering contracts](../../DEVELOPMENT.md#engineering-contracts)
- [Third-party notices](../../LICENSING.md)
