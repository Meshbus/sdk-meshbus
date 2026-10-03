# Tasks

## 1. License and source preparation

- [x] 1.1 Implement scoped attribution expansion and verify temporary-Git/REUSE regressions.
- [x] 1.2 Gate OpenSpec preparation and cache verified tool downloads; verify source selection and cache integrity tests.
- [x] 1.3 Update scope/tooling owner documentation and pass local links and OpenSpec validation.

## 2. Product and component selection

- [x] 2.1 Add product profile requests and reviewed component scopes; verify exact mixed-selection regressions.
- [x] 2.2 Execute profile-aware product builds; verify command tests and representative configuration/build evidence.
- [x] 2.3 Document product/SDK selection and verify the documented planner examples.

## 3. Twister scheduling

- [x] 3.1 Add multi-task jobs with independent result accounting; verify failure, missing, duplicate and skipped regressions.
- [x] 3.2 Publish and consume bounded trusted timing hints; verify grouping-only effects and safe fallback tests.
- [x] 3.3 Add isolated benchmark entry and document fixed-input comparison; verify qualification rejection and workflow lint.

## 4. Candidate and publication

- [x] 4.1 Reuse complete default-product and Twister evidence; verify schema and expected-job failure cases.
- [x] 4.2 Record qualified Candidate artifact identities and promote them; verify source/attempt/integrity and compatibility regressions.
- [x] 4.3 Add read-only staging dispatch and current audits; verify both DAG paths and publication exclusion.
- [x] 4.4 Update release owner documentation and pass related release tests.

## 5. Integration and hosted acceptance

- [x] 5.1 Validate the exact staged tree, commit functional batches excluding website work, push and confirm full CI success.
- [x] 5.2 Complete exact-source Candidate and explicit/automatic staging runs; verify reused builds and no publication.
- [x] 5.3 Compare fixed full/small inventories with cold preparation and two warm samples per scheduler; meet the documented runner/wall-time criteria.
- [ ] 5.4 Record actual evidence, preserve website changes, push acceptance record and verify remote equality.

## Validation

Local implementation validation:

- Complete CI script suite: 247 tests passed in the isolated proposed tree,
  including scheduling, timing, baseline reuse, promotion and qualification.
  The original worktree also passed 249 tests, including two excluded website
  tests. Ruff and workflow lint passed on the isolated tree.
- License policy: 37 tests; pinned tool cache: 7 tests; legacy installer: 1 test.
- Product selection: 8 new and 7 existing tests; planner: 39 tests; CI gates: 21 tests.
- Release tools: 106 tests; host tools: 34 tests; remote tools: 9 tests.
- R2 default/dev configuration completed using a task-local west configuration;
  prod sysbuild completed with prj.prod.conf, local ISR tables and LTO enabled.
  The build retained existing Zephyr LTO symbol type/size warnings.
- Documentation links, strict OpenSpec and workflow lint passed. Alpha's base
  capability is still in its completed active change; synchronize that capability
  before archiving this delta.

Hosted integration:

- Implementation commits: `91164d5`, `d416494`, `c4220e6`, `ad7339f`.
- [Full CI 37027188496](https://github.com/Meshbus/sdk-meshbus/actions/runs/37027188496)
  passed all 30 jobs on `ad7339f18719ae3c8bddfb2c0878076e7f7c555d`.
- Actual coverage: 60 runtime and 52 compile instances, identical by
  name/platform/toolchain to the previous full baseline, four default products,
  six CLI targets and six native artifact checks.
- The isolated tree's SPDX audit selected 1,226 files with no remaining findings.
  After committing, the preserved website changes selected 17 files incrementally;
  no findings remained and the entire web tree stayed excluded.
- Source checks completed in 41 seconds and saved verified tool archives.
  The first full run used new Twister cache keys; its cold measurements are not
  used as evidence of a scheduling speedup.

- [Candidate 37029185593](https://github.com/Meshbus/sdk-meshbus/actions/runs/37029185593)
  passed on the same implementation SHA and Builder. Its schema 2 baseline
  points to full CI 37027188496; ordinary product/Twister matrices were skipped,
  while four prod firmware/META/EDK builds and six CLI/native targets passed.
- Qualification receipt artifact `11237174898` binds attempt 1 and 14 exact
  input artifact IDs/digests.
- [Explicit staging 37032492927](https://github.com/Meshbus/sdk-meshbus/actions/runs/37032492927)
  and [automatic staging 37033875023](https://github.com/Meshbus/sdk-meshbus/actions/runs/37033875023)
  both passed. Both selected the same Candidate/receipt, refreshed vulnerability
  checks and skipped Candidate rebuilding and publication.
- Downloaded staging assets passed local publish validation. The 13 payloads
  have identical hashes and sizes; manifests retain separate staging run IDs.
  Remote tag and Release inventories each remained at three, unchanged.
- Benchmark source checks skipped Node/OpenSpec preparation and restored
  source tools with SHA-256 verification. Both small-case cold runs passed;
  the combined job preserved separate runtime/compile reports with three
  instances each.
- Both fixed-inventory benchmarks passed their agreed acceptance criteria.
  Each scheduler has one separate cold preparation and two alternating warm
  observations per case. All twelve runs passed exact inventory and raw
  Twister report checks. Independent recomputation from raw job timestamps
  agrees with every reported wall time and summed worker execution time.
- Small-case warm medians: legacy 275 s /
  8.6167 worker runner-minutes; balanced
  296 s / 4.9333. Runner use decreased
  42.75%, with a 21 s wall-time increase below the 60 s limit.
  Six identical instances retained separate runtime and compile reports;
  worker count changed from two to one.
- Full-case warm medians: legacy 414.5 s /
  50.1417 worker runner-minutes; balanced
  430.5 s / 50.4167. Wall time changed by
  +16 s, within the 60 s regression limit. Worker time
  changed by +0.55%. Coverage remained 60 runtime and 52 compile
  instances, with eight workers for both schedulers. These measurements
  retain the variability observed on shared hosted runners.
- Cold runs restored no external ccache entry; every warm worker restored
  its exact scheduler/case/source cache key. Cold preparation is excluded
  from the warm medians. Cache hits within a cold run remain valid.
- Wall time spans the first Twister worker start to the last completion.
  Worker runner-minutes sum those jobs, including container/workspace
  preparation, cache operations, testing and uploads. Earlier prepare/queue
  time, total workflow duration and GitHub billable minutes are separate.
- Benchmark pins: source `ad7339f18719ae3c8bddfb2c0878076e7f7c555d`, Builder
  `ghcr.io/meshbus/sdk-meshbus-builder@sha256:edeed0755c6379c5553696f9456c3ea1ca0c8c6df9836effba4ba9df2e537300`,
  history artifact `11235764252` from full CI 37027188496 with SHA-256
  `52d9ff5fd3d204f09c6688db86dce6fa9d449bc8996b932c6859328e1acc3ace`.

| Case / scheduler | Cold preparation | Warm 1 | Warm 2 |
| --- | --- | --- | --- |
| clock-small / legacy | [37029104666](https://github.com/Meshbus/sdk-meshbus/actions/runs/37029104666) | [37031343273](https://github.com/Meshbus/sdk-meshbus/actions/runs/37031343273) | [37033651727](https://github.com/Meshbus/sdk-meshbus/actions/runs/37033651727) |
| clock-small / balanced | [37030202253](https://github.com/Meshbus/sdk-meshbus/actions/runs/37030202253) | [37032524433](https://github.com/Meshbus/sdk-meshbus/actions/runs/37032524433) | [37034711515](https://github.com/Meshbus/sdk-meshbus/actions/runs/37034711515) |
| full / legacy | [37035947361](https://github.com/Meshbus/sdk-meshbus/actions/runs/37035947361) | [37094650633](https://github.com/Meshbus/sdk-meshbus/actions/runs/37094650633) | [37096030314](https://github.com/Meshbus/sdk-meshbus/actions/runs/37096030314) |
| full / balanced | [37037830569](https://github.com/Meshbus/sdk-meshbus/actions/runs/37037830569) | [37095328061](https://github.com/Meshbus/sdk-meshbus/actions/runs/37095328061) | [37096682997](https://github.com/Meshbus/sdk-meshbus/actions/runs/37096682997) |

Warm worker step medians in seconds; the longest-worker column is the
median of the longest worker in each warm run. Twister includes build and
runtime execution. Preparation covers container, checkout and workspace steps.

| Case / scheduler | Preparation | Cache | Twister | Upload | Longest worker |
| --- | ---: | ---: | ---: | ---: | ---: |
| clock-small / legacy | 210 | 1.5 | 44 | 1.5 | 274 |
| clock-small / balanced | 206.5 | 1.5 | 83 | 1.5 | 296 |
| full / legacy | 199 | 2 | 175.5 | 1 | 414.5 |
| full / balanced | 202 | 2 | 169.5 | 1 | 430 |

Acceptance record validation:

- Strict OpenSpec validation, task-record links and whitespace checks passed.
- The default development-environment license policy selected 18 files
  incrementally, excluded the entire web tree and reported no remaining findings.
- Submission contains only this existing task record. Concurrent website edits
  and all website companion changes remain outside the submission.

No hardware or public publication acceptance is claimed by this change.
