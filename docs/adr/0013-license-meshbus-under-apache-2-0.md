---
status: accepted
---

# License Meshbus-owned content under Apache 2.0

FoBE Studio-owned content in this repository uses Apache-2.0, including the
reusable SDK, product firmware, host CLI, tooling, assets and documentation.
This permits use in proprietary products while preserving the license's notice
and attribution requirements. Third-party portions retain their own terms.

## Ownership boundaries

- Preserve third-party license terms, copyright notices and provenance. The
  default does not relabel upstream or derived material under Apache-2.0.
- Reconcile file declarations, metadata, generator templates, packaging,
  verification and current documentation with the selected default.
- Independently maintained dependencies are governed by their own license
  declarations. The repository's default does not establish the licensing of
  every component in a complete firmware distribution.

## Consequences

- EDKs contain the Apache-2.0 text for owned headers and preserve notices for
  exported third-party headers. The EDK verifier checks these materials against
  the current distribution contract in [LICENSING.md](../../LICENSING.md).
- Continue the compiled-third-party admission policy in
  [ADR 0012](0012-restrict-compiled-third-party-licenses.md). Retain unresolved
  third-party provenance findings instead of relabeling those materials.

Publication, signing and history rewriting require their own authorization.
