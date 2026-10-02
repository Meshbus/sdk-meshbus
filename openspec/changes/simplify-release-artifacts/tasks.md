# Tasks

## 1. Archive production

- [x] 1.1 Implement deduplicated notices and selected-font terms with private material evidence; verify attribution, source-location and tamper regression tests.
- [x] 1.2 Produce compact firmware/CLI archives and release-versioned names, rename merged images, and document the layout; verify packaging and native-verifier tests.
- [x] 1.3 Produce header-scoped EDK notices and schema 2 with schema 1 compatibility; verify Rust EDK creation, verification and qualification tests.

## 2. Release interface

- [x] 2.1 Stage fourteen assets using manifest schema 3 without outer checksums, retain published schemas and all existing gates, and update CI guidance; verify staging, upload and workflow boundary regressions including material failures.

## 3. Guidance and policy

- [x] 3.1 Consolidate current licensing/contribution/distribution and agent guidance, remove stale exemptions and prepare the next Alpha version; verify document links, metadata policy and unchanged dependency revisions.

## 4. Integration acceptance

- [x] 4.1 Run affected Python/Rust tests, actionlint, document checks, strict OpenSpec validation, development-environment license policy and git diff --check; record actual results and remaining acceptance.
- [x] 4.2 Exercise compact staging using retained real matrix artifacts and validate published compatibility; record artifact/source boundaries without claiming a new hosted release.

## Later hosted acceptance (outside this local implementation)

Any newly authorized publication still requires the final commit's complete CI
baseline, Candidate success, new tag build, upload and anonymous download
verification. Published alpha.1/2 stay unchanged. These checks are not satisfied
by local tests or retained artifacts. Alpha.3 completed the later authorized
hosted acceptance recorded below.

## Local acceptance — 2026-10-02

- Implemented on main based on `40916d54edf93f9554274408a7096bcf296b3c67`;
  uncommitted work prepares `1.0.0-alpha.3`. Dependency manifests, Cargo version
  and lock, REUSE attribution and generated skills are unchanged.
- Release Python: 106 passed; CI boundary/release tests: 156 passed. Rust all
  targets: 118 passed, one optional pinned C-decoder test ignored because
  MESHBUS_DETOOLS_C was not configured. Clippy with warnings denied, Cargo fmt,
  Ruff, actionlint, documentation links, strict OpenSpec (5 items), development
  Python license policy (zero remaining findings) and diff whitespace passed.
- Failures remain covered for damaged/missing notices, runtime/font/generated
  materials, unsafe archives, source/version/native/qualification and asset
  conflicts. Compact inputs work without approval files; evidence-only mode
  does not write public assets. Complex header license expressions that cannot
  be completely collected fail rather than silently discarding obligations.
- Replayed the complete retained four-product/three-EDK/six-CLI candidate from
  source `6286ba050e7325120707a3fce65851bf4bce4f61`, using a snapshot reconstructed
  from its retained release manifest. Final schema 3 staging verified 14 files.
  Firmware archives have 7/10/7/7 files, CLI archives 4, EDK notices 26,046 bytes.
  This is local packaging replay of old binaries, not new-source hosted evidence.
- Built the current arm64 macOS CLI in the release profile with development
  provenance; its alpha.3-named archive passed architecture/version/help and
  signed-fixture/tamper native checks. Current CLI verified all three public EDKs
  using the adjacent manifest without sidecars. All four compact flash maps
  passed offline inspect, including the R2 merged firmware.bin.
- Verified retained published alpha.1 schema 1 and alpha.2 schema 2 inventories
  and manifests. No tags, published assets, remote Actions, Git index or commits
  were changed. No hardware or new hosted CI/publication acceptance was performed.

## Hosted Candidate acceptance — 2026-10-02

- Exact source `74e45aa218ae3dfc1841e23ae4c93f9c88ab3fd4` passed full
  [CI 36910211301](https://github.com/Meshbus/sdk-meshbus/actions/runs/36910211301)
  with all 30 jobs successful. Retained plan/source/job records confirm complete
  runtime and compile roots, eight Twister shards and Required checks.
- [Candidate 36920305748](https://github.com/Meshbus/sdk-meshbus/actions/runs/36920305748)
  completed with 28 successful jobs and one intentionally skipped Twister
  placeholder: fresh strict validation, six native-checked CLI targets,
  four production-profile firmware packages, three EDK qualifications, assembly
  and evidence-only material checks. It reused the exact-source full baseline
  with Builder `sha256:edeed0755c6379c5553696f9456c3ea1ca0c8c6df9836effba4ba9df2e537300`;
  the frozen west graph matched and no duplicate Twister jobs ran. The workflow
  retained verified-firmware-candidate and alpha-license-evidence, without
  verified-alpha-assets or public publication.
- Downloaded the current Candidate and replayed staging in a clean local
  checkout, preserving another session's untracked website planning files.
  Schema 3 staging passed publication inventory validation for 14 files:
  four firmware archives, three EDKs, six CLIs and release-manifest.json.
  Firmware file counts are 7/10/7/7; CLI archives contain four files each.
  Tracker T1000-E does not enable LLEXT; R1, R2 and Wio Tracker L1 supply EDKs.
  Each EDK has one 26,046-byte NOTICE and passed the current Candidate CLI's
  adjacent-manifest verification without sidecars. Material scopes agree with
  CI by target; CLI scope list enumeration order differs between hosts.
- At this Candidate checkpoint, the local replay did not establish hosted Alpha
  staging, upload or anonymous download acceptance. No alpha.3 tag or Release
  had been created; publication required separate authorization. The later
  authorized public acceptance is recorded below. Hardware and signing
  qualification remain unperformed.

## Public Alpha.3 acceptance — 2026-10-02

- The user explicitly authorized creating and pushing `v1.0.0-alpha.3` on
  `74e45aa218ae3dfc1841e23ae4c93f9c88ab3fd4`. The annotated tag object is
  `e7c8850d4128c0041cb189ca33ef8e56a5410eb9`, and the remote tag peels to that
  exact source. Existing published tags and assets were preserved.
- [Alpha release 36924758374](https://github.com/Meshbus/sdk-meshbus/actions/runs/36924758374)
  completed successfully: 31 jobs succeeded and one Twister placeholder was
  intentionally skipped. Preflight, fresh strict validation, six native CLI
  checks, four production-profile packages, three EDK qualifications, assembly,
  material checks, public staging and publication passed. It reused the same
  exact-source full CI baseline and immutable Builder as the manual Candidate.
- CI created and verified its own draft, checked uploaded bytes, then exposed
  [v1.0.0-alpha.3](https://github.com/Meshbus/sdk-meshbus/releases/tag/v1.0.0-alpha.3)
  as a public Prerelease without making it latest. CI anonymous verification
  passed. Release metadata confirms source `74e45aa`, draft=false,
  prerelease=true and 14 uploaded assets: four firmware archives, three EDKs,
  six CLI archives and release-manifest.json (schema 3).
- Independently downloaded every public asset without authentication. All 14
  sizes and SHA256 values matched the tag run's retained upload inventory,
  including the public manifest. Actual firmware/CLI layouts passed; all three
  public EDKs passed adjacent-manifest verification using the public arm64 macOS
  CLI, without sidecars. No standalone APP/SBOM or outer SHA256SUMS was published.
- No hardware, image authentication, host signing/notarization or production
  qualification was added. This acceptance record remains local and uncommitted;
  unrelated website planning files and the existing Git index were preserved.
