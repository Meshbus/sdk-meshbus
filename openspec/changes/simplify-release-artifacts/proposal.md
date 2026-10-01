# Proposal

## Why

Alpha.2 exposes 23 assets and hundreds of repeated license files per archive.
Users need a small, consistently named download set; maintainers and agents
need task-scoped licensing guidance and retained build evidence.

## What Changes

- **BREAKING**: future releases expose board-only firmware and EDK archive
  names, six release-versioned CLI archives and one release manifest. Remove
  standalone APP/SBOM downloads and public SHA256SUMS (14 assets today).
- Deliver one deduplicated NOTICE.txt with applicable terms in each archive.
  Keep detailed component, generator, runtime and font evidence in CI parts.
- Rename MCUboot merged images to firmware.bin/hex and update flash maps.
- Keep the CLI's independent executable version explicit in its manifest.
- Limit EDK notices to actual exported headers, and font terms to selected
  families. Preserve contribution review, original notices and material failures.
- Consolidate licensing documentation and agent routes; remove stale policy
  exemptions. Preserve old releases and schema 1/2 verification.

## Capabilities

### New Capabilities

- `compact-release-distribution`: archive contents, notice/evidence separation,
  download naming, complete integrity verification and legacy compatibility.

### Modified Capabilities

None.

## Impact

Python release/CI collectors and tests, Rust EDK creation/verification, firmware
VERSION, license policy and current documentation. No dependency revisions,
hardware operations, signing, Git submission or hosted publication are part of
this local implementation. New publication requires a new Alpha tag and its
own exact-source hosted acceptance; published alpha.1/2 remain fixed.
