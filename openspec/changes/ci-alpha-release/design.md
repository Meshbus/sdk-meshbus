<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Design

## Context

See `proposal.md` for the intended outcome and pilot scope.

At source `64b4739`, normal full CI can build four products and six CLI targets,
execute metadata-selected tests and check CLI archives on native hosts. The
[main run](https://github.com/Meshbus/sdk-meshbus/actions/runs/36724697347)
passed all 30 jobs. That run did not execute Candidate preparation's additional
production-profile packaging, EDK qualification and assembly stages.

`candidates.yml` already runs full strict validation, packages every discovered
firmware product and assembles the complete set. `release.py` rejects CLI parts
in firmware assembly and retains `publishable: false` and unverified gates.
Firmware and CLI source versions are independent. The local firmware VERSION
is now `1.0.0-alpha.1` but remains uncommitted; the local UF2 used the Enterprise
workspace and is not an input to the proposed publication.

## Goals / Non-Goals

**Goals:** Bind a public R1 Alpha download to one reviewed source commit and
verified CI-produced bytes; recover interrupted draft uploads; provide concise
release notes and independently checkable provenance.

**Non-Goals:** Establish production/device qualification, redefine firmware
assembly to permit incomplete matrices, publish a CLI release or add automatic
daily publication.

## Decisions

### 1. A release tag is the build authority

Add a tag-triggered Alpha workflow, accepting canonical tags such as
`v1.0.0-alpha.1`. A read-only preflight validates the tag's source SHA, committed
firmware VERSION and Alpha numbering before invoking candidate preparation.
Checkouts in called workflows must resolve the same commit; snapshots and
package records must agree with it. The workflow operates only in the owning
repository, and published tags remain fixed.

Tag-triggered operation fits releases made when a version is ready. Branch-based
dispatch with a free-form version would introduce two authorities and tempt
in-run VERSION edits, which conflict with clean candidate requirements.

### 2. Reuse the complete candidate pipeline

Expose `candidates.yml` through `workflow_call` while preserving its manual entry
point. The release workflow calls it with full/strict behavior and Twister reuse;
nested validation resolves the builder once to an immutable digest. A read-only
Actions gate selects successful complete `ci.yml` evidence on main for the exact
source, root manifest and digest. It checks both Twister layers and every planned
shard, then binds the new dependency snapshot to the tested frozen graph. Only
Twister is deselected in the candidate plan; other strict checks run afresh.
Missing or expired evidence fails with an instruction to complete full CI first.
Normal PR/main selection stays stateless and unchanged. The retained baseline
and public manifest identify the actual reused run rather than claim fresh tests.
All four products
are packaged and assembled, and the six CLI targets remain validation/internal
tool inputs. Publication reads only successful artifacts from this run.

R1-only publication therefore exercises the real complete assembly contract.
Narrowing production builds would require a separate partial-assembly contract,
which is unnecessary for this pilot. Ordinary PR/main selection is unchanged.

### 3. Separate public export from private candidate evidence

Use a small collector under `scripts/ci/` to validate existing checksum records,
source identities, package versions, R1 UF2 payload and its matching EDK before
copying an explicit download set. Reuse existing package and checksum validators
where possible. A publication job must not rebuild firmware or repack verified
firmware/EDK archives.

The pilot download set is:

- A version/target-named standalone R1 UF2.
- The existing complete R1 firmware archive with notices, license materials,
  curated SBOM and flash instructions.
- The existing matching R1 EDK archive.
- A version/target-named copy of the curated public SBOM.
- `release-manifest.json` and `SHA256SUMS`.

The manifest enumerates payload assets and records source/tag, resolved
dependency revisions, tool/builder identities, Actions run, selected product and
actual qualification. SHA256SUMS covers payloads and the manifest; neither file
attempts to hash itself. The full staging inventory, including hashes of both
metadata files, remains in Actions evidence for upload verification.

Every public filename must be unique and path-safe. Archive/path and source
checks reject symlinks, unexpected files, private source locations, credentials
and missing components. SPDX/license collection and source/font policy results
remain required. Unresolved selected-component license permission must be
reconciled before public publication; an automated policy pass alone is not a
completed distribution review. Release notes link the raw UF2 to its companion
archive's applicable notices and programming prerequisites.

Manual Candidate preparation also retains an Alpha review-input record without
approval or public staging. A separately reviewed, committed
`alpha-license-review.json` binds component choices, source/notice obligations,
runtime/generated inputs and EDK terms to the actual selected licensing scope.
Moving first-party source identity and aggregate product version are omitted
from that scope digest to avoid a circular approval commit hash; exact public
bytes still bind to the tag in the publication inventory. Staging rejects a
missing or stale review and preserves its review input. The first actual hosted
distribution review remains acceptance work, not an inferred policy pass.

### 4. Use a staged GitHub Release transition

The workflow stages verified assets first, then a dedicated publication job
uses `GITHUB_TOKEN` with `contents: write`. Validation/build jobs keep read-only
permissions. Serialize runs for the same tag without cancelling an active
publication. No persistent personal token or image signing key is required.

For a new tag:

1. Require the existing remote tag and check its resolved source SHA.
2. Create a draft marked as a Pre-release with `latest=false`, recording its
   source and owning CI run in release metadata.
3. Upload the complete allowlisted assets and download them through authenticated
   Release APIs to verify exact inventory, sizes and SHA256 values.
4. Publish the draft as a Pre-release only after those checks pass.
5. Download the public assets without authentication and check their bytes again;
   report the final URL and full validation outcome in the Actions summary.

This uses the documented [draft/tag flags](https://cli.github.com/manual/gh_release_create)
and [draft publication](https://cli.github.com/manual/gh_release_edit) interfaces.
Publishing before all uploads finish would expose an incomplete download set.

### 5. Retry from retained bytes and preserve published assets

Before invoking builds, detect an already published version. Verify its tag,
publication manifest and public download inventory, then report success without
rebuilding or mutating it. Conflicting identity or bytes fail visibly.

A draft can be resumed only when its CI ownership, tag/source and retained
inventory agree. Retry failed publication jobs against the original Actions
artifacts: keep matching assets and upload missing ones. A conflicting draft
asset fails rather than using `--clobber`. Full rebuilds can change archive bytes
because metadata contains generation times; they are not a replacement for a
published version or the retained original upload set.

After public visibility, a download-verification failure is reported as a failure
after publication, preserving the release for investigation. A necessary content
correction uses a new Alpha number. No automated tag deletion/movement, release
deletion or public asset replacement is part of recovery.

### 6. Add an explicit public Alpha eligibility record

Update the distribution guide to distinguish an unpublished Engineering
Candidate, an eligible public Alpha and a qualified GA. The new publication
manifest describes eligibility for the Alpha channel and keeps production
qualification false. Existing candidate fields and truthful `not-run` gates
are preserved; setting their `publishable` field to true would falsely remove
unmet qualification and also conflict with current assembly validation.

The public Alpha statement covers CI build/package/integrity evidence and actual
license review. It explicitly reports missing physical-device evidence and the
configured authentication mode. This adds a separate channel contract rather
than claiming the existing GA publication requirements have passed.

## Risks / Trade-offs

- Candidate preparation consumes a complete non-Twister matrix even for one public
  product -> reuse the existing validated contracts and record each failed stage.
- GitHub upload/download interruption can leave a draft -> retain original
  artifacts and resume only a matching draft without overwriting bytes.
- Candidate packaging or license review can expose unresolved issues -> fail
  publication and fix the specific issue without weakening the required gate.
- R1 currently links with LTO symbol warnings and has tight static memory use
  -> retain compiler/size evidence and the unverified runtime status in Alpha
  notes; a production-profile build is not physical runtime evidence.

## Migration Plan

1. Review the R1 pilot scope, implement locally and complete targeted checks.
2. After explicit authorization, commit/push the workflow, documentation and
   existing firmware VERSION edit through the repository's normal process.
3. Confirm those committed inputs and create/push `v1.0.0-alpha.1` only after
   explicit authorization to trigger the public pilot.
4. Follow candidate and publication jobs until the Pre-release and anonymous
   checksum verification pass; retain their evidence in this change's tasks.
5. Preserve the published release. Further content changes use `alpha.2` or a
   later approved version; disabling future workflow execution is separate from
   removing an existing release.
