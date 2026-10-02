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

- [ ] 5.1 Validate the exact staged tree, commit functional batches excluding website work, push and confirm full CI success.
- [ ] 5.2 Complete exact-source Candidate and explicit/automatic staging runs; verify reused builds and no publication.
- [ ] 5.3 Compare fixed full/small inventories with cold preparation and two warm samples per scheduler; meet the documented runner/wall-time criteria.
- [ ] 5.4 Record actual evidence, preserve website changes, push acceptance record and verify remote equality.

## Validation

Local implementation validation:

- Complete CI script suite: 249 tests passed, including scheduling, timing,
  baseline reuse, promotion and qualification regressions.
- License policy: 37 tests; pinned tool cache: 7 tests; legacy installer: 1 test.
- Product selection: 8 new and 7 existing tests; planner: 39 tests; CI gates: 21 tests.
- Release tools: 106 tests; host tools: 34 tests; remote tools: 9 tests.
- R2 default/dev configuration completed using a task-local west configuration;
  prod sysbuild completed with prj.prod.conf, local ISR tables and LTO enabled.
  The build retained existing Zephyr LTO symbol type/size warnings.
- Documentation links, strict OpenSpec and workflow lint passed. Alpha's base
  capability is still in its completed active change; synchronize that capability
  before archiving this delta.

Exact staged-tree and hosted acceptance remain pending. No hardware or public
publication acceptance is claimed by this change.
