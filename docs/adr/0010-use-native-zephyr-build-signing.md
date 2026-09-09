---
status: accepted
---

# Use native Zephyr build signing

Meshbus is development-board firmware and favors a simple release command.
Use Zephyr sysbuild's native MCUboot/imgtool signing for all official targets,
with the shared Ed25519 Production Image Key. This supersedes ADR 0009's
separate build/signing jobs and the C2-only external-signing configuration.

`west release build --image-signing-key` accepts a caller-owned private PEM
file and passes its path through Zephyr's standard
`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`. There is no PEM-content environment
adapter or temporary private-key lifecycle in Meshbus. The release host or CI
owns storage, backup and deletion, and keeps the key available for rebuilds and
EDK export. The command exports the public PEM; packaging verifies the signed
APP and its correspondence to the key embedded in MCUboot.
The DFOTA Manifest Key remains independent as defined in ADR 0003.

The release host or protected CI must trust the reviewed source, build scripts,
and toolchain: those processes can access the private key during the build.
This broader trust boundary is accepted to remove a separate signing handoff.
Keep private material out of source, logs and artifacts, retain an encrypted
backup, and clean ephemeral storage after forced termination. Public-key
fingerprints and exact signed release bytes remain part of release records.
Signing does not authorize publication or replace board qualification.
