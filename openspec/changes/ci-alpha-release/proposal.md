<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Proposal

## Why

Meshbus can validate and package engineering candidates in Actions, but it has
no CI path that makes verified firmware available through GitHub Releases.
Use `v1.0.0-alpha.1` to exercise committed-source build, packaging, upload and
public download verification before establishing an ongoing release practice.

## What Changes

- Add an on-demand Alpha release workflow triggered by a version tag. Require
  the tag, committed firmware VERSION and packaged firmware/EDK identities to
  agree, and keep every job on that tag's source commit.
- Reuse complete strict validation and the existing candidate product builds,
  SPDX/license collection, EDK checks and complete firmware assembly. Resolve
  the builder to one immutable digest for the run.
- Prepare a deterministic publication inventory from verified candidate bytes.
  The proposed pilot publishes Mesh Probe R1's APP-only UF2, firmware archive,
  matching EDK, public SBOM, license-bearing companion package, checksums and
  a portable release manifest. The user accepted this scope by instructing
  implementation after the R1 plan review.
- Create a draft, upload the selected assets, verify their downloaded bytes,
  then expose the release as a Pre-release with `latest=false`. Verify public
  downloads after publication and retain the exact Actions evidence.
- Define public Alpha eligibility separately from GA/device qualification.
  Existing candidate qualification records remain truthful; CI success does
  not establish hardware operation or production authentication.
- Prevent published asset replacement and tag movement. A failed upload keeps
  its draft for recovery; retries reuse verified bytes instead of replacing a
  published version with a fresh rebuild.

## Capabilities

### New Capabilities

- `alpha-release-publication`: Publish traceable, verified firmware Alpha assets
  through CI while preserving qualification boundaries and version immutability.

### Modified Capabilities

None. The existing `development-validation` selection and result-accounting
requirements remain applicable without changes.

## Impact

- `.github/workflows/candidates.yml`: expose its existing preparation pipeline
  for reuse while retaining manual engineering candidate preparation.
- A new release workflow and narrowly scoped publication tooling/tests under
  `scripts/ci/`; GitHub Release write permission belongs only to publication.
- `DISTRIBUTION.md` and `.github/CI.md`: document Alpha eligibility, tag operation,
  public assets, failed drafts and the distinction from qualified GA releases.
- The existing local `apps/meshbus/VERSION` edit to `alpha.1` must be included in
  a separately authorized committed source revision before hosted execution.
- CLI packages retain their independent version and release train; the pilot
  uses CI-built CLI artifacts internally and does not publish them.
- No dependency/manifest updates, daily schedule, image signing, notarization,
  device programming or hardware-qualification claim is included.

Local implementation is authorized. Consequential Git/publication operations
still require separate explicit user instructions.
