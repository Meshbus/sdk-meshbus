---
status: accepted
---

# Distribute firmware 1.0.0 as artifacts only

Firmware 1.0.0 uses Artifact-only Distribution. The approved firmware binaries,
EDK compiler inputs, documentation, and other selected release material may be
published, but neither the outer firmware source repository nor the nested SDK
source repository is published as part of this release.

## Consequences

- Firmware 1.0.0 must not be described or licensed as an open-source release.
- Publishing an EDK does not publish either repository. Its redistributed
  headers, build metadata, examples, and tools require explicit distribution
  terms and a provenance review.
- The release package includes all third-party notices and licenses required for
  the distributed artifacts.
- The security model cannot depend on source or binary secrecy. Downloadable
  binaries and an owner-accessible SWD interface permit inspection and copying.
- Any later source publication is a separate, irreversible publication decision
  with its own licensing and secret-removal review.

## Repository consolidation (2026-09-07)

The former firmware application and SDK now share the Meshbus repository.
The parent is a local Zephyr workspace. References above to two repositories
record the original layout; Artifact-only Distribution still excludes product
and SDK source from the public firmware release. Consolidation does not change
that accepted distribution policy or authorize publication.
