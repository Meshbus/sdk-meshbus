# Design

## Context

The published alpha.2 layout contains separate firmware, APP, SBOM and checksum
downloads, and repeats detailed licensing evidence inside each archive. The
existing collectors already select components and verify their material hashes;
that evidence can stay in CI without becoming the download interface.

## Goals / Non-Goals

Deliver four firmware archives, three EDK archives, six CLI archives and one
manifest for the current supported matrix. Preserve attribution, applicable
license terms, provenance and negative checks. Keep guidance short and scoped.

Do not change dependencies, the build matrix, Twister, signing, publication
permissions or published releases. Local acceptance does not establish hosted
CI or hardware acceptance.

## Decisions

- Firmware names are `meshbus-<release>-<board>.tar.gz`; EDK names are
  `meshbus-edk-<release>-<board>.tar.xz`; CLI names are
  `meshbus-cli-<release>-<target>.tar.gz` (Windows uses `.zip`). Full qualified
  targets remain in metadata; duplicate board basenames fail.
- Firmware includes actual images, flash-map.json, SBOM.spdx, SHA256SUMS and
  NOTICE.txt. MCUboot merged images use firmware.bin/hex. CLI includes its
  executable, manifest.json, SHA256SUMS and NOTICE.txt. EDK keeps compiler
  inputs, original source headers and one NOTICE.txt for exported headers.
- EDK attribution stays in original source headers. Its notice carries each
  required full standard text once, with automatically collected content hashes.
  Apache/GPL alternatives use Apache; BSD-2-Clause/CC0 alternatives use BSD.
  Missing selected texts or changed headers/notices fail validation.
- Notice assembly groups identical text by digest while retaining every
  associated component and original path. Selected font attribution and terms
  remain; unselected fonts do not contribute terms. MPL dependencies include
  their precise source download location. No new approval database is added.
- Detailed material files and manifests stay in `material-evidence/` beside
  archives in private CI parts. Staging regenerates notices from those materials
  and validates hashes and existing font/runtime/component constraints.
- New release manifest schema 3 carries hashes for all payloads and separates
  the release version from the CLI executable version. Internal upload inventory
  also hashes the public manifest. No public outer SHA256SUMS is necessary.
  Published manifest schemas 1/2 remain verifiable. EDK schema 2 binds its notice;
  schema 1 verification remains supported.
- Download consumers read archive checksums from an adjacent release manifest
  when no private sidecar exists. Missing, duplicate or conflicting entries fail;
  a corrupt existing sidecar cannot be bypassed by supplying a manifest.
- LICENSING.md owns admission and exceptions. Contribution review remains the
  human gate. Agent guidance routes to detailed policy only for relevant work;
  Markdown stays exempt from per-file declarations. Remove only demonstrably
  stale exemptions, not inherited attribution.

## Risks / Trade-offs

Packaging changes are intentionally incompatible with old filename assumptions.
Consumers use the manifest and tests cover both new and published layouts.
Aggregated notices must retain complete terms and attribution, not merely SPDX
identifiers; regeneration and tamper tests guard against accidental omission.
Runtime and code-generator selection remains conservative during this change.

## Migration

Prepare the next Alpha version locally. Keep alpha.1/2 assets immutable. Validate
new archive contents, old manifest acceptance and material failures before any
future authorized full CI, Candidate and tag publication.
