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
by local tests or retained artifacts.

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
