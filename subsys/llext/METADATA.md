# MBA metadata and interface contract

Meshbus applications use metadata **V1**, a 368-byte little-endian record in
`.meshbus.llext.meta`. `METADATA_VERSION` declares the record format. Bytes
216 through 263 and 348 through 367 are reserved and must be zero. The public
layout and offset assertions are defined in
[`include/llext/metadata.h`](../../include/llext/metadata.h).

The EDK exports `metadata-version`, which the CLI writes into each MBA.
Unsupported metadata versions reject with `-EPROTONOSUPPORT` before ELF
loading, constructors or creation of an MBA session.

## Loading requirements

The loader validates metadata magic, V1 format, record size, strings, resource
limits and reserved bytes, then exact target equality with `CONFIG_BOARD_TARGET`.
Zephyr checks ELF loading, required symbols, relocation and entry lookup.
Missing imports reject the application. There is no host interface version gate.

Service headers use `<module/module.h>` and service APIs use `mbs_` / `MBS_`.
Build an application using an EDK for its intended host target. The recorded
`edk_version` identifies the build input; a different firmware version produces
a loader warning when the host provides an application version.

CLI installation/session checks and Desktop package validation retain build
revision and firmware image hashes as provenance. Differences or unknown build
image hashes warn that the app may malfunction, but do not prevent execution.
Desktop shows a non-blocking toast when launching a registered package with
such a difference. Package file hashes remain mandatory integrity checks.

Adding exports while preserving existing function signatures, public layouts,
calling conventions and behavior allows existing applications to keep running.
Symbol availability alone cannot detect incompatible changes to those contracts.

## Native device access

With `CONFIG_MBS_LLEXT_BRIDGE`, host devicetree devices are exported for native
Zephyr driver calls from MBAs. Device symbols use path hashes. Applications
need an EDK exported with the host's device-export configuration, and all
metadata and target checks still apply. Missing device imports fail relocation.
Device paths, pin mappings and driver behavior are part of the selected target
configuration.

Metadata validation does not sign applications or sandbox their code. MBA
applications follow the [owner-trusted execution model](../../docs/adr/0004-owner-trusted-mba-applications.md).
