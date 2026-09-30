<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tasks

## 1. Alpha publication identity and policy

- [x] 1.1 Confirm the proposed R1 pilot scope during plan review and document public Alpha eligibility, actual qualification and independent CLI versioning in DISTRIBUTION.md; verify the guide agrees with the capability scenarios and preserves GA acceptance requirements.
- [x] 1.2 Implement canonical Alpha tag/source/VERSION checks and their boundary tests; verify matching `v1.0.0-alpha.1` succeeds and malformed tags, moved/conflicting source identity and version mismatch fail before publication.
- [x] 1.3 Document committed VERSION/tag operation in .github/CI.md, keeping the existing `alpha.1` edit within the future authorized commit; verify the documented trigger uses a fixed committed source without dirty in-run version stamping.

## 2. Reusable complete candidate preparation

- [x] 2.1 Expose candidates.yml for workflow_call while preserving manual preparation and its full strict matrix, retention and immutable builder identity; verify actionlint and focused CI/workflow tests cover both entries and same-source handoff.
- [x] 2.2 Wire the Alpha entry point to candidate preparation and required success accounting; verify failed/missing candidate jobs block publication and ordinary PR/main selection remains unchanged, then update .github/CI.md with the job sequence.

## 3. Verified public download collection

- [x] 3.1 Collect the explicit R1 UF2, firmware archive, EDK and public SBOM from successful assembled candidates without rebuilding or repacking them; verify unit fixtures exercise missing products, UF2/archive disagreement, EDK/version mismatch and unexpected/unsafe assets.
- [x] 3.2 Generate release-manifest.json, SHA256SUMS and the retained complete staging inventory with source/dependency/builder/run identities and truthful Alpha qualification; verify complete checksum coverage, source consistency and absence of private paths or recursive self-hashes.
- [ ] 3.3 Verify selected notice/SBOM/font material against the delivered artifacts and admission policy, reconciling unresolved selected-component permissions before publication; record the actual review and test missing/corrupt required materials. Update DISTRIBUTION.md with asset contents and the raw UF2's companion notices.

## 4. Draft upload, publication and recovery

- [x] 4.1 Implement draft creation, owned-draft resume, complete upload and authenticated download verification, then Pre-release publication with latest=false; verify boundary tests prevent public visibility after a partial upload, missing asset, wrong digest or conflicting existing draft.
- [x] 4.2 Implement published-version detection and immutable recovery behavior; verify tests reuse existing public bytes, preserve matching draft assets and reject source/content conflicts without tag movement, deletion or clobber.
- [x] 4.3 Implement anonymous final download verification and an Actions summary with the real publication state; verify tests distinguish a retained draft from publication followed by a public-download failure, and document failed-job recovery in .github/CI.md.
- [x] 4.4 Restrict contents:write to publication and serialize identical tags; verify actionlint and workflow checks show build/validation jobs remain read-only and publication requires every stated gate.

## 5. Integration and first hosted pilot

- [x] 5.1 Run affected CI/release boundary suites, source documentation/metadata checks, actionlint, OpenSpec strict validation, git diff --check and the repository license policy; record actual outcomes here without equating structural checks with hosted acceptance.
- [ ] 5.2 After separate explicit Git authorization, commit/push the reviewed implementation and firmware VERSION edit; verify the committed diff/source identity and successful hosted CI, preserving unrelated work.
- [ ] 5.3 After separate explicit public-publication authorization, create/push the fixed `v1.0.0-alpha.1` tag and follow its full strict validation, production packaging, EDK qualification, complete assembly and draft verification; retain source SHA, resolved graph, builder digest, job results and artifact references here.
- [ ] 5.4 Verify the resulting GitHub Release is public, prerelease=true and latest=false, then independently download the exact R1 asset set and verify checksums, version and UF2/EDK identity; record the final Release URL and CI evidence here. Physical-device, production-signing and GA acceptance remain unperformed in this CI-only pilot.

## Validation

Planning baseline: main `64b4739c8fe1c80f4ed465694424dd0596baac36` has a
[successful full CI run](https://github.com/Meshbus/sdk-meshbus/actions/runs/36724697347)
with all 30 jobs passing. This is existing validation evidence, not execution
of this proposed candidate/publication pipeline.

At the planning stage, only planning files were added. The earlier local
firmware VERSION edit remains in the working tree. Hosted candidate execution,
tagging and public Alpha publication are still pending.

Planning checks on 2026-09-30:

- `openspec validate ci-alpha-release --strict --no-interactive`: passed.
- `npm run spec:check`: 3 items passed, 0 failed.
- Local documentation references and metadata checks: passed (external anchors
  were not checked; metadata reported 129 documents and 105 unique scenarios).
- Repository license policy: passed with 0 remaining file findings and 29
  metadata exemptions; raw REUSE compliance remains false.
- `git diff --check`: passed. Existing VERSION edit is unchanged by planning.

These planning checks validate structure and metadata, not implementation or
hosted publication acceptance.

Implementation checks on 2026-09-30 (working tree based on `64b4739`):

- CI boundary suite: 105 tests passed, including 27 Alpha cases. Coverage
  includes source/version conflicts, complete candidate accounting, exact public
  archive reuse, UF2 disagreement, EDK identity/notices, changed/missing license
  review, restricted fonts, unsafe archives, draft resume and public visibility.
  After bounding archive member counts, the affected 27 Alpha cases passed again.
- Release regression suite: 89 tests passed. This includes native image/tool
  fixture checks, not a new product build or physical qualification.
- `ruff check --select E4,E7,E9,F scripts/ci`: passed.
- actionlint 1.7.12: passed for all workflows. The temporary Darwin executable
  was verified against the upstream release checksum; no project dependency or
  shared tool environment was changed.
- Local documentation references/includes and metadata: passed; 129 documents,
  105 unique scenarios. External anchors were not checked.
- OpenSpec strict validation and `npm run spec:check`: passed (3 items).
- Repository license policy: passed with 0 remaining findings, 29 metadata
  exemptions; raw REUSE compliance remains false.
- `git diff --check`: passed. The earlier Alpha VERSION edit is preserved.

Task 3.3 is partially implemented: archive/notice/font checks, scope-bound review
enforcement, manual CI review-input export and documentation are in place and
tested. Actual selected-component, toolchain/runtime, generated-input and EDK
distribution review cannot be claimed from fixtures or the earlier Enterprise
UF2 build. No `alpha-license-review.json` approval has been fabricated. Run the
reviewed manual Candidate pipeline and inspect its actual retained bytes, inputs
and terms before completing this task and creating the publication tag.

Tasks 5.2-5.4 were pending explicit Git/remote/publication authorization and
hosted evidence at the end of local implementation. No SDK/dependency commits, pushes, tags, Actions dispatches,
GitHub Release mutations, signing or device operations occurred during local
implementation. The active developer west workspace remains Enterprise; its
configuration and dependencies were preserved.

The user authorized committing/pushing the reviewed changes and running manual
Candidate preparation on 2026-09-30. Task 5.2 is in progress. Tag creation and
public GitHub Release publication remain outside this authorization.
