# Tasks

## 1. Alpha publication identity and policy

- [x] 1.1 Confirm the proposed R1 pilot scope during plan review and document public Alpha eligibility, actual qualification and independent CLI versioning in DISTRIBUTION.md; verify the guide agrees with the capability scenarios and preserves GA acceptance requirements.
- [x] 1.2 Implement canonical Alpha tag/source/VERSION checks and their boundary tests; verify matching `v1.0.0-alpha.1` succeeds and malformed tags, moved/conflicting source identity and version mismatch fail before publication.
- [x] 1.3 Document committed VERSION/tag operation in .github/CI.md, keeping the existing `alpha.1` edit within the future authorized commit; verify the documented trigger uses a fixed committed source without dirty in-run version stamping.

## 2. Reusable complete candidate preparation

- [x] 2.1 Expose candidates.yml for workflow_call while preserving manual preparation and its full strict matrix, retention and immutable builder identity; verify actionlint and focused CI/workflow tests cover both entries and same-source handoff.
- [x] 2.2 Wire the Alpha entry point to candidate preparation and required success accounting; verify failed/missing candidate jobs block publication and ordinary PR/main selection remains unchanged, then update .github/CI.md with the job sequence.
- [x] 2.3 Following the user's CI-time refinement, reuse successful full main CI Twister evidence for the exact source, frozen graph and immutable Builder; retain baseline provenance, reject mismatches/missing evidence and run other strict/package/EDK gates afresh. Verify boundary tests, actionlint and hosted Candidate job accounting.

## 3. Verified public download collection

- [x] 3.1 Collect the explicit R1 UF2, firmware archive, EDK and public SBOM from successful assembled candidates without rebuilding or repacking them; verify unit fixtures exercise missing products, UF2/archive disagreement, EDK/version mismatch and unexpected/unsafe assets.
- [x] 3.2 Generate release-manifest.json, SHA256SUMS and the retained complete staging inventory with source/dependency/builder/run identities and truthful Alpha qualification; verify complete checksum coverage, source consistency and absence of private paths or recursive self-hashes.
- [x] 3.3 Move permission decisions to contribution review under the existing admission policy; add contributor guidance and a PR template, remove per-Alpha approval files and retain automatic firmware/EDK/font/runtime material checks and evidence. Verify approval-free staging, NOASSERTION handling, evidence-only output, migrated interfaces and missing/corrupt material failures; update the publication documentation and preserve earlier review findings.

## 4. Draft upload, publication and recovery

- [x] 4.1 Implement draft creation, owned-draft resume, complete upload and authenticated download verification, then Pre-release publication with latest=false; verify boundary tests prevent public visibility after a partial upload, missing asset, wrong digest or conflicting existing draft.
- [x] 4.2 Implement published-version detection and immutable recovery behavior; verify tests reuse existing public bytes, preserve matching draft assets and reject source/content conflicts without tag movement, deletion or clobber.
- [x] 4.3 Implement anonymous final download verification and an Actions summary with the real publication state; verify tests distinguish a retained draft from publication followed by a public-download failure, and document failed-job recovery in .github/CI.md.
- [x] 4.4 Restrict contents:write to publication and serialize identical tags; verify actionlint and workflow checks show build/validation jobs remain read-only and publication requires every stated gate.

## 5. Integration and first hosted pilot

- [x] 5.1 Run affected CI/release boundary suites, source documentation/metadata checks, actionlint, OpenSpec strict validation, git diff --check and the repository license policy; record actual outcomes here without equating structural checks with hosted acceptance.
- [x] 5.2 After separate explicit Git authorization, commit/push the reviewed implementation and firmware VERSION edit; verify the committed diff/source identity and successful hosted CI, preserving unrelated work.
- [x] 5.3 After separate explicit public-publication authorization, create/push the fixed `v1.0.0-alpha.1` tag and follow its verified full CI Twister reuse, fresh strict validation, production packaging, EDK qualification, complete assembly and draft verification; retain source SHA, resolved graph, builder digest, job results and artifact references here.
- [x] 5.4 Verify the resulting GitHub Release is public, prerelease=true and latest=false, then independently download the exact R1 asset set and verify checksums, version and UF2/EDK identity; record the final Release URL and CI evidence here. Physical-device, production-signing and GA acceptance remain unperformed in this CI-only pilot.

## Validation

The historical evidence below describes the earlier approval-file mechanism.
The contribution-stage refinement at the end records the current contract and
acceptance; it supersedes earlier statements that task 3.3 requires a committed
per-Alpha approval without rewriting the observed runtime-material findings.

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
Candidate preparation on 2026-09-30. The reviewed implementation and VERSION
were committed as `9b273284d17fbdd7933e6a4a4660afb4fedadfb6` and pushed normally
to `meshbus/main`. The remote source identity was independently checked.
Its [main CI run](https://github.com/Meshbus/sdk-meshbus/actions/runs/36740191819)
completed successfully with all 30 jobs passing on 2026-10-01 (Asia/Shanghai).
Task 5.2 is complete. Tag creation and public GitHub Release publication remain
outside this authorization.

Manual [Candidate preparation](https://github.com/Meshbus/sdk-meshbus/actions/runs/36740230643)
was dispatched with `image=stable` and the same committed source. Its retained
snapshot records 76 dependency revisions, manifest SHA-256
`7aed573187d52799c03180ad5b5c891a443f9d765dfe241fc6103f832993f41f` and Builder
`ghcr.io/meshbus/sdk-meshbus-builder@sha256:edeed0755c6379c5553696f9456c3ea1ca0c8c6df9836effba4ba9df2e537300`.
The original Candidate completed with all 36 jobs successful. Independent checks
verified the retained R1 UF2 against the original firmware archive and BIN,
verified archive checksums and ran EDK verification with the actual CI-built
Darwin arm64 CLI. No device operation was performed. Actual license review found
that linked SDK Picolibc permission/copyright material was absent from the
firmware archive. Task 3.3 remains open while runtime collection is corrected
and the actual rebuilt bytes are reviewed.

On 2026-10-01 the user requested reducing unnecessary release-stage Twister.
The revised contract preserves full baseline validation while reusing its
Twister evidence in Candidate and tag publication. The main pipeline remains
unchanged; explicit read-only Actions lookup is confined to Candidate reuse.
Task 2.3 tracks implementation and hosted verification of this refinement.

Local refinement checks: the complete CI boundary suite passed 115 tests, then
the two added Alpha reuse/provenance cases passed in the affected suite. The
release regression suite passed 94 tests, including five runtime-collection
failure/retention cases. Ruff, actionlint, documentation/metadata, strict
OpenSpec and repository license policy passed (0 findings, 29 exemptions; raw
REUSE remains false). Read-only execution against main CI run `36740191819`
successfully selected all eight actual Twister shards, the exact source and
Builder, and retained the frozen-manifest hash. This does not establish hosted
execution of the revised Candidate workflow; that remains to be verified.

Hosted refinement acceptance on 2026-10-01 (Asia/Shanghai):

- Implementation and runtime collection were committed/pushed as
  `94db2fad1cc77a58ea4d9c1c371722d513164163`; remote main matched that SHA.
- [Full main CI](https://github.com/Meshbus/sdk-meshbus/actions/runs/36749625503)
  passed all 30 jobs. Its actual host log records 117 CI boundary tests and
  94 release regression tests passing, alongside the other host suites.
- [Candidate preparation](https://github.com/Meshbus/sdk-meshbus/actions/runs/36751986315)
  passed 28 executed jobs. One SDK matrix placeholder was deliberately skipped;
  no Twister shard jobs ran. Previously, Candidate executed 36 jobs.
- The downloaded Candidate plan records `full: true`, `sdk: false` and baseline
  run `36749625503`, attempt 1, including all eight successful Twister shards.
  Source SHA, Builder digest and frozen manifest SHA-256
  `296b63ac56c1b0e71b8f26523f2269e434068afd89fd6c00ee566f0c27ef64dc` match.
  Independent verification of the actual new dependency snapshot passed.
- Fresh strict CLI/native validation, all four production packages, C/C++ EDK
  qualification, complete assembly and Candidate required checks passed.
- Downloaded R1 part/archive checksums and UF2/BIN/HEX consistency passed.
  UF2 SHA-256 is
  `c2fda7b5ebff85cfc4c08b57da252d75262ba186d7c1fbd16a2a75a674d9abe5`.
  The actual CI-built Darwin arm64 CLI independently verified its new EDK.
- R1 runtime records identify SDK 1.0.1 and the actual linked `libc.a`/`libgcc.a`
  archive digests. The firmware archive now retains GCC license/exception and
  Picolibc/Newlib materials. Downloaded Picolibc materials match the installed
  official SDK 1.0.1 notice bytes. This corrects the observed missing materials;
  it does not fabricate completion of the selected distribution approval.

Task 2.3 is complete. Task 3.3's committed scope-bound distribution approval and
the separately authorized publication tasks 5.3-5.4 remain pending. No tag,
GitHub Release, device operation or production signing was performed. These
post-run acceptance records are retained locally for the next review commit;
the validated implementation remains at `94db2fa`.


### Current contribution-stage refinement (2026-10-01)

The user approved moving permission decisions to contribution review while
retaining the existing admission scope and exceptions. Task 3.3 now covers
that migration and automatic distribution-material checks, replacing the old
per-Alpha approval acceptance. Earlier Picolibc findings and their correction
above remain historical evidence; no approval or legal-clearance claim is added.

- Added contributor guidance, a PR template and README/agent entry points.
  Maintainers review applicable source/version, terms, choices/exceptions,
  output usage and notice obligations before merging. Existing CI is reused;
  there is no new approval registry or signing requirement.
- LICENSING.md retains admission policy ownership and records known gaps from
  existing Heatshrink, Nanopb, LoRa Basics Modem and Mbed TLS/TF-PSA-Crypto terms.
  Actual source license texts were inspected in the existing workspace. This
  is targeted documentation, not a blanket approval of every module file.
- Removed Alpha approval-file reading and matching. Candidate uses
  `--evidence-only`; both workflows retain automatically checked materials.
  `license-evidence.json` keeps schema 1 scope/digest as provenance, and
  `alpha-license-evidence` replaces the old Candidate artifact. Public manifest
  schema 1 omits the approval digest. Removed arguments fail parsing.
- Final CI boundary suite: 138 tests passed, including 37 Alpha tests. Coverage
  includes approval-free staging, NOASSERTION preservation, material/digest
  changes without reapproval, evidence-only output, actual workflow arguments,
  missing/corrupt notices, runtime inventory, fonts and EDK license conflicts.
  Version/source, complete-matrix, asset and visibility boundaries still pass.
- Release regression suite: 101 tests passed. These are local fixture/tool
  checks, not fresh hosted product packages or device acceptance.
- Ruff and actionlint 1.7.12 passed. Local documentation links/includes,
  explicit checks of both new Markdown files, and metadata checks passed
  (132 tracked metadata documents, 105 unique scenarios). External URLs and
  anchors were not crawled.
- Strict OpenSpec validation passed all four items. Repository license policy
  passed with zero remaining file findings and 29 existing exemptions;
  raw REUSE compliance remains false. `git diff --check` passed.

Task 3.3 is complete under the revised contract. The changes remain local on
base `ac66d89`; no staging, commit, push, Actions dispatch, tag, public Release,
production signing or device operation was performed. The shared west manifest
remains Enterprise. Tasks 5.3-5.4 stay open: hosted acceptance for the final
committed source and first public tag/download verification still require
separately authorized execution. No new hardware or legal clearance is claimed.

### Markdown metadata refinement (2026-10-01)

The user exempted all repository Markdown from per-file SPDX declarations.
Removed the two first-party declaration lines from 25 documents; the audit of
45 tracked/nonignored Markdown files, including hidden paths and archives,
found no remaining declaration headers. Third-party notices, central REUSE
attribution and generated skills remain unchanged. AGENTS.md, OpenSpec context,
contributor guidance and licensing/CI documentation now state the file scopes.

- License policy exempts only missing Markdown copyright/license metadata,
  including new or updated documents, without hash registration. Other scanner
  failures, source metadata checks and path/symlink protections remain active.
- CI boundary suite: 140 tests passed, including new Markdown scope and
  retained-failure regression cases. Ruff, actionlint, documentation checks,
  metadata checks (132 documents, 105 scenarios), strict OpenSpec validation
  (four items) and `git diff --check` passed.
- Development-environment license policy passed with zero remaining findings
  and 54 exemptions, including 25 Markdown documents. Raw REUSE compliance
  remains false. Earlier release regression results above remain the evidence
  for unchanged release code; no new hosted or device validation was performed.

Changes remain local and tasks 5.3-5.4 remain open.

### First-publication readiness recheck (2026-10-01)

Read-only GitHub checks found remote main at
`ac66d89de66a0609f25372970742a64a17bf184c`, matching local HEAD. No
`v1.0.0-alpha.1` tag or Release exists. The committed firmware VERSION already
declares `1.0.0-alpha.1`. Contribution-stage and Markdown policy refinements
above remain uncommitted; the index is empty.

The latest successful [main CI run](https://github.com/Meshbus/sdk-meshbus/actions/runs/36773089302)
ran Source checks, Plan validation and Required checks, skipping the eight
workspace/build/test jobs. It retains only source-checks and ci-plan artifacts,
so it is not a complete Twister baseline. The older complete CI and Candidate
results cover earlier source revisions, not the pending refinements.

After explicit authorization for the pending Git changes and hosted execution,
commit/push the reviewed refinements, dispatch full CI for that final source,
then run Candidate preparation with the same immutable Builder digest. Require
both to pass for that exact source before creating/pushing the fixed Alpha tag
under public-publication authorization. The tag-triggered pipeline performs its
fresh candidate/staging/upload checks and reuses only matching full CI Twister
evidence. Tasks 5.3-5.4 remain incomplete until their real hosted acceptance.

### Authorized hosted pilot and tag-event regression (2026-10-01)

The user authorized committing/pushing the refinements, full CI and Candidate
execution, then the tag-triggered public pilot after both passed. Commit
`75898e1eaf81141b2cacdb996081c6f253597b05` was pushed to main.

- [Full CI](https://github.com/Meshbus/sdk-meshbus/actions/runs/36850535364)
  passed all 30 jobs, including all eight runtime/compile Twister shards.
  The automatically triggered narrowed push run was cancelled to avoid
  duplicating non-Twister work. Hosted license policy had zero remaining
  findings and 54 documented exemptions.
- [Manual Candidate](https://github.com/Meshbus/sdk-meshbus/actions/runs/36851843539)
  passed 28 jobs with one expected Twister-reuse skip. Its source, frozen graph
  and Builder matched full CI. All four production packages and EDK checks,
  assembly and material checks passed. The actual `alpha-license-evidence`
  contains only `license-evidence.json`, schema 1 and 13 SBOM components, with
  a verified input digest and no approval field.
- Builder:
  `ghcr.io/meshbus/sdk-meshbus-builder@sha256:edeed0755c6379c5553696f9456c3ea1ca0c8c6df9836effba4ba9df2e537300`.
- The fixed annotated `v1.0.0-alpha.1` tag triggered
  [Alpha run](https://github.com/Meshbus/sdk-meshbus/actions/runs/36854476663).
  Preflight and exact-source Twister reuse passed, but Workspace checks failed
  at C/C++ patch style. GitHub supplies an all-zero `before` for a new tag;
  the quality gate treated it as a new branch and checked the complete tree,
  reporting 3,885 historical style errors. Full CI and manual Candidate had
  checked the target commit against its available parent instead. Packaging,
  public staging and publication were skipped; no Release was created.

The local correction uses the available parent when a tag event has no resolvable
previous ref; ordinary new branches and root commits retain complete-tree
fallback. Real Git fixture regressions reproduce the tag failure and retain
C/header patch checks and those conservative fallbacks. The user explicitly
authorized correcting the tag before any Release, preserving alpha.1. The tag
remains unchanged until the corrected source passes hosted acceptance.
The final corrected source still needs complete hosted CI and
Candidate acceptance. Tasks 5.3-5.4 remain open; no public acceptance is claimed.

Local correction validation: 142 CI boundary tests passed, including the tag,
missing-old-ref, new-branch and root-commit regression cases. Ruff, actionlint,
local documentation, strict OpenSpec and repository license policy passed;
the policy retains zero findings and 54 exemptions. Release code is unchanged;
the preceding 101-test release regression remains its local evidence.

### Completed first public Alpha acceptance (2026-10-01)

The corrected publication source is
`6286ba050e7325120707a3fce65851bf4bce4f61`. After the user's explicit
unpublished-tag correction authorization, this exact source passed a new full
CI baseline and manual Candidate before the tag was changed.

- [Full CI](https://github.com/Meshbus/sdk-meshbus/actions/runs/36856504155)
  passed all 30 jobs, including all eight Twister shards. The duplicate narrowed
  push run was cancelled. No failed gate was bypassed.
- [Manual Candidate](https://github.com/Meshbus/sdk-meshbus/actions/runs/36857808653)
  passed 28 jobs with one expected Twister-reuse skip. Fresh strict validation,
  all four production packages, EDK qualification, assembly and actual
  distribution-material checks passed without an approval file.
- Before correction, GitHub had no Release for the tag. The original annotated
  tag and complete history were backed up and verified. The authorized push
  used an exact lease on old tag object
  `0f4af3c7edcfcbdc9eda7c515607a147f8e0e8b5`; the new annotated tag object is
  `39bdd7c66ddcdd497da254d32f22fd6a289b6998`, resolving to the corrected source.
- [Tag-triggered Alpha](https://github.com/Meshbus/sdk-meshbus/actions/runs/36860528713)
  passed 31 jobs with one expected Twister-reuse skip. C/C++ patch style passed
  on the real tag event. Fresh strict checks, production packages, EDK,
  assembly, staging, verified draft upload and anonymous publication checks
  all passed. These results supersede the earlier failed tag attempt.
- Downloaded plans and snapshots for both Candidate and Alpha reused baseline
  `36856504155`, attempt 1, with all eight successful Twister shards. Exact
  source, frozen dependency graph and immutable Builder were independently
  checked against the full CI snapshot. Frozen-manifest SHA-256:
  `296b63ac56c1b0e71b8f26523f2269e434068afd89fd6c00ee566f0c27ef64dc`.
  Input manifest SHA-256:
  `7aed573187d52799c03180ad5b5c891a443f9d765dfe241fc6103f832993f41f`.
  Builder remains
  `ghcr.io/meshbus/sdk-meshbus-builder@sha256:edeed0755c6379c5553696f9456c3ea1ca0c8c6df9836effba4ba9df2e537300`.
- Actual `alpha-license-evidence` artifacts for the manual and tag Candidates
  agree. Each contains only `license-evidence.json`, schema 1, scope and a
  verified provenance digest:
  `80006ac6bb103ea15077fbbb0769766f0c1421b1e2b70182353afbbfb7235baa`.
  No approval field is present. Public manifest schema 1 also contains no
  `license_review_scope_sha256` or replacement approval field.
- [Public Release](https://github.com/Meshbus/sdk-meshbus/releases/tag/v1.0.0-alpha.1)
  was published by CI at 2026-10-01 12:43:29 UTC with `draft=false`,
  `prerelease=true` and `latest=false`. Its source is the corrected commit,
  and its ownership marker and manifest identify Alpha run `36860528713`.
- Independent downloads used all six public browser download URLs without an
  Authorization header or cookies. The complete inventory matches the retained
  `alpha-evidence/inventory.json`, GitHub asset sizes and all SHA-256 checks.
  Internal firmware checksums, public/archive UF2 equality and BIN/HEX payload,
  EDK host version/source identity, public/archive SBOM equality,
  dependency provenance, runtime notices, fonts and EDK material checks passed.

The verified public assets and SHA-256 digests are:

| Asset | SHA-256 |
| --- | --- |
| `SHA256SUMS` | `eaa90b04561a36396ac9570b9d6fa731bbc1cf1920da61fca0a080a0023f5680` |
| `app-1.0.0-alpha.1-mesh_probe_r1-nrf52840-edk.tar.xz` | `b6efad3bb2a3192f0e2b683f3dc4abcaaeaf0dc42b2b7450bb245aab733e3f91` |
| `meshbus-1.0.0-alpha.1-mesh_probe_r1_nrf52840-SBOM.spdx` | `1a4ff03a3f01639dd4c061193177833e02f5ba33f18e7f0fa587e396d79d8c50` |
| `meshbus-1.0.0-alpha.1-mesh_probe_r1_nrf52840-firmware.tar.gz` | `29b07dd30693faa5ad49aa88f3faec7275eefbc44f91f3e1db035a40e3632f3b` |
| `meshbus-1.0.0-alpha.1-mesh_probe_r1_nrf52840.uf2` | `7485161f269487f95c62d5b2b15b3baffd4f19cb30233f1f8dba550c7875e7d1` |
| `release-manifest.json` | `4b58c6aa2dc8fed07c43c50874ba8e4b1e6d70eddeb4326379a273213a981be3` |

Tasks 5.3 and 5.4 are complete. This establishes the CI-only Alpha pilot for
`mesh_probe_r1/nrf52840`; physical-device qualification, production signing and
GA acceptance remain unperformed. The published tag and assets stay fixed;
this acceptance-record update does not change their validated source.

Acceptance-record checks: local documentation references/includes, metadata
(132 documents, 105 scenarios), OpenSpec strict validation (four items) and
`git diff --check` passed. The development-environment license policy passed
with zero remaining findings and 54 exemptions; raw REUSE compliance remains
false under the documented repository metadata scope. This documentation-only
update does not require a new firmware build or move the public tag.

## 6. Full matrix and CLI follow-up

- [x] 6.1 Extend verified staging and provenance to all registered products, applicable EDKs and six native-validated CLI archives; test mixed UF2/MCUboot, independent CLI version, complete notice/runtime inventories, missing/conflicting targets and legacy schema 1 verification.
- [x] 6.2 Pass CLI parts and native artifacts to Candidate evidence and Alpha staging; update specs, publication guides and firmware VERSION to alpha.2; verify workflow interfaces, complete CI/release suites, actionlint, documentation, strict OpenSpec, license policy and whitespace.
- [ ] 6.3 Commit/push the authorized change and complete full CI and manual Candidate for the final source before pushing a new alpha.2 tag; retain exact source, Builder, dependency graph and reused Twister evidence.
- [ ] 6.4 Follow tag CI through public Pre-release publication, independently download and verify the complete board/CLI inventory, and record the release and evidence without claiming hardware or host signing qualification.

Full-matrix local validation (2026-10-01):

- The user requested normal CI publication for all adapted boards and CLI.
  The expanded publication advances firmware VERSION to alpha.2, preserving
  the already published alpha.1 tag and bytes. CLI retains Cargo version 1.0.0.
- CI boundary suite passed 149 tests, then the affected Alpha suite passed
  46 tests including two additional matrix/safe-ZIP regressions. Release
  regression suite passed all 101 tests. Ruff, actionlint, local documentation,
  metadata (132 documents, 105 scenarios), strict OpenSpec (four items),
  repository license policy and whitespace checks passed. Policy has zero
  remaining findings and 54 exemptions; raw REUSE remains false.
- Independently downloaded all original firmware, six CLI and native validation
  artifacts from completed Alpha run 36860528713. The new collector passed
  complete local staging of those actual retained source-6286ba0 bytes without
  changing any public asset. This validates collector compatibility, not a new
  alpha.2 build or hosted acceptance for the pending implementation.
- The staged schema 2 inventory contains 23 files: four product archives,
  three UF2s and one MCUboot APP BIN, four public SBOMs, three applicable EDKs,
  six native-validated CLI archives, manifest and checksums. Both R2 runtime
  domains and merged firmware contents passed checks. No EDK is claimed for
  tracker_t1000_e, whose actual product capability disables LLEXT.

Tasks 6.3-6.4 remain pending exact-source hosted and public download acceptance.
