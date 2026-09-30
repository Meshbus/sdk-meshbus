<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tasks

## 1. Scoped preparation

- [x] 1.1 Record/validate CLI source receipts and fetch only pinned schemas; test mismatched source, graph and dirty schema rejection and document provenance.
- [x] 1.2 Make hosted disk cleanup conditional and bounded; verify skip, early-stop and insufficient-space cases and document budgets.

## 2. Rust compilation

- [x] 2.1 Add daily CI profile and profile-specific caching while retaining strict release builds; verify profile selection and packaging records.
- [x] 2.2 Build Intel macOS on ARM with target tests and native artifact checks; verify workflow structure locally and execute cross compilation where available.

## 3. Twister and CI selection

- [x] 3.1 Adapt shard counts to selected work without changing inventories; verify disjoint coverage, small selections and full baseline compatibility.
- [x] 3.2 Select known CI helpers by consumer and retain real integration for execution changes; verify path-selection regressions and update the CI guide.

## 4. Board and product coverage

- [x] 4.1 Add board/profile include-consumer selection with conservative fallback; verify board, shared include, version, mixed and deleted-input cases and document scope.

## 5. Combined acceptance

- [x] 5.1 Pass CI/release boundary tests, workflow syntax, metadata/docs, OpenSpec and license policy; record actual validation and limits.
- [ ] 5.2 Review and commit only this change, push once, inspect hosted CI and compare timings and complete inventories with run 36749625503.

### Local validation

- CI Python suite: 131 tests passed; release suite: 101 tests passed.
- Actionlint, Ruff, documentation links, 132-document/105-scenario metadata,
  all four strict OpenSpec validations and the repository license policy passed.
  License policy reports zero remaining findings with 29 existing exemptions;
  this does not claim full REUSE compliance.
- Rust 1.97.1: ARM64 and x86-64 macOS builds passed with the CI profile.
  The x86-64 tests ran under Rosetta: 116 passed, one ignored. This is not
  native Intel runner evidence. Hosted validation retains that separate check.
- The local Cargo checks used the existing workspace schema checkout. The
  isolated receipt tests verified fetching only the pinned schema, graph/source
  conflicts, dirty inputs and unsafe paths without initializing west.
- Hosted timing and complete inventory acceptance remain pending the combined
  commit. New Cargo cache keys make its first run a cold-cache observation.

### First hosted run and correction

- Run 36761673956 at cbffabe: 17 jobs passed and 13 failed. All four products,
  eight Twister shards (60 runtime and 52 compile instances), host checks,
  source checks, workspace checks and preparation passed.
- All six CLI jobs failed before compilation because the planner JSON-quoted
  its scalar profile output. Six native jobs then had no archives to download;
  Required checks correctly rejected the incomplete result.
- The new boundary regression reproduces both daily and full-profile failures
  by passing the actual Actions output to the CLI argument parser. Scalar
  strings now remain unquoted; booleans and matrix objects retain JSON output.
- Prepare took 204 seconds versus 290 in baseline 36749625503. CLI schema
  setup took one second. These step observations do not establish complete
  pipeline or Rust build speed because CLI compilation never started.
- Complete acceptance remains pending a corrective commit and successful run.
- After the correction, all 132 CI tests passed, including both previously
  failing profile cases. Ruff, strict OpenSpec and repository license policy
  also passed; workflow structure and build implementation are unchanged.
