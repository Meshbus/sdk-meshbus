# Tasks

## 1. Proportionate CI selection

- [x] 1.1 Add failing tests for explicit component scope, license/CI-tool isolation and daily versus full extended selection; confirm failures express the new contract.
- [x] 1.2 Simplify ownership and selection, document the resulting CI rules, then pass CI regression tests including mixed changes and strict report accounting.

## 2. Test and source convergence

- [x] 2.1 Remove the identical LLEXT scenario while preserving all 13 test cases and distinct heap/shuffle configurations; verify metadata and run the retained app scenario.
- [x] 2.2 Reuse existing Desktop input predicates and remove proven redundant private Clock checks; verify before/after predicate equivalence, affected QEMU integration/Clock tests and a Desktop-enabled product build.

## 3. Engineering workflow

- [x] 3.1 Align development/testing/OpenSpec guidance with focused TDD, risk-based tests, internal preconditions and stop criteria; verify local links and CLI configuration injection.
- [x] 3.2 Complete the repository-domain review recorded in design.md, preserving independent compatibility/security/device policies; inspect the final diff for unrelated edits and generated-file changes.

## 4. Integrated acceptance

- [x] 4.1 Validate real Twister discovery/filtering, workflow syntax, Python style, OpenSpec structure and repository license policy; record exact commands, outcomes and environmental limits.
- [x] 4.2 Record the final scope/count comparison, local verification and unexecuted qualification in this change; review the results against its acceptance criteria.

## 5. Review corrections

- [x] 5.1 Add failing planner regressions for CLI license inputs and Power feedback consumers, implement the narrow selections, update CI guidance and pass CI regression tests.
- [x] 5.2 Reuse shared input predicates in the MeshCore UI; verify function-body and call-site equivalence and the incremental R2 product build containing all affected UI sources.
- [x] 5.3 Use pristine auto in ordinary product-build examples and verify local documentation references and preserved configuration-isolation guidance.
- [x] 5.4 Sync the clarified validation spec, review the combined diff and pass OpenSpec, metadata, style and repository license checks; record the review correction results.

## Validation

Review corrections are complete and recorded below. The existing Desktop input/registry QEMU
fixture does not compile the MeshCore UI pages. Predicate body and call-site
equivalence plus a product build cover this refactor; rerunning that unrelated
fixture would not add evidence. Earlier results below describe the first pass.

### First implementation pass

Local acceptance on 2026-09-30 uses the active `meshbus-enterprise/west.yml`
workspace with Zephyr `v4.4.0-16441-g4adceb2f861d`, the existing development Python
environment and installed Zephyr SDK. It does not establish the SDK's hosted
frozen-manifest, physical-device or release qualification. No dependencies,
manifest revisions, hardware, Git index or remote state were changed by this
convergence pass. Earlier OpenSpec adoption edits remain in the working tree.

### Selection and test inventory

The counts below are test scenarios present under the selected metadata roots,
before platform filtering. They are not CI job counts or a measured speedup.

| Example change | Previous candidates | Routine candidates now |
| --- | ---: | ---: |
| `subsys/clock/clock.c` | 58 | 30 |
| `subsys/indicator/indicator_audio.c` | 24 | 17 |
| `subsys/radio/radio.c` | 56 | 19 |

The full test inventory is 75 scenarios, down from 76 because one duplicate
LLEXT configuration was removed. All 462 ZTEST declarations remain; 30 sample
scenarios and the four product profiles remain. Mapped public headers now share
their component scope. License metadata and CI Python changes do not independently
select firmware or CLI builds.

Three added planner regression tests first failed against the old behavior.
An identity implementation of the extended filter still failed its expectation,
confirming that the initial missing-function error was not the only Red evidence.
After implementation, `python -m unittest discover -s scripts/ci/tests` passed
75 tests. A final focused rerun of `test_planning.py` passed 28 tests after a
semantics-preserving cleanup of category handling. Existing strict missing,
duplicate, unexpected skip and runtime/compile inventory checks remain covered.

Real Twister discovery was exercised through the production planner:

```sh
source ~/.zephyr/env/bin/activate
# Run from the repository root. snapshot/plan.json contains
# plan.select(['subsys/clock/clock.c']).
python scripts/ci/test_plan.py generate --workspace .. \
  --snapshot .scratch/simplify-repository/discovery/snapshot
```

Discovery produced 34 runtime candidates and 36 compile candidates, including
sample configurations. Routine filtering retained 30 and 32 respectively.
After platform selection and cross-layer deduplication, 19 runtime instances and
10 compile instances were frozen into four shards per layer. Their identities
are disjoint and every selected identity occurs exactly once in its shards.
Full and direct-root selection retain the original extended candidates.
These are discovery checks; the entire selected matrix was not executed locally.

### Runtime and product evidence

Run Zephyr commands from `west topdir`, with the same activated environment:

```sh
west twister -T meshbus/tests/subsys/clock \
  -T meshbus/tests/subsys/desktop/boot_logo \
  -T meshbus/tests/subsys/desktop/integration -T meshbus/tests/subsys/llext \
  -s subsys.meshbus.clock.contract -s subsys.meshbus.clock.timestamps_only \
  -s subsys.meshbus.clock.shell \
  -s subsys.meshbus.desktop.boot_logo.integration \
  -s subsys.meshbus.desktop.boot_logo.disabled.integration \
  -s subsys.meshbus.desktop.integration -s subsys.meshbus.llext.contract.app \
  -p qemu_x86 -j4 --inline-logs \
  -O meshbus/.scratch/simplify-repository/twister
```

Six scenarios passed: Clock contract (7 cases), timestamps-only (1), shell (8),
Desktop boot logo enabled/disabled (1 each), and Desktop integration (17).
LLEXT hit Ninja `Error writing to build log: Input/output error` before runtime.
A separate `-j1` Ninja attempt reproduced that build failure. The isolated
Makefiles run then passed all 13 LLEXT cases:

```sh
west twister --make -T meshbus/tests/subsys/llext \
  -s subsys.meshbus.llext.contract.app -p qemu_x86 -j1 --inline-logs \
  -O meshbus/.scratch/simplify-repository/twister-llext-make
west build --sysbuild -b mesh_probe_r2/nrf54l15/cpuapp meshbus/apps/meshbus \
  -d meshbus/.scratch/simplify-repository/product-r2 -o=-j4
```

Final runtime evidence is seven QEMU configurations and 48 passed cases.
The R2 product sysbuild passed, with Desktop enabled and all 16 changed C files
present in its compilation database; their objects are newer than the final
sources. It was not flashed. The 13 replaced Desktop predicate bodies matched
the existing shared functions after whitespace normalization. Clock call-site
inspection established non-null arguments for the removed private checks;
public input, numeric and system-error checks remain.

### Source and workflow checks

From the repository root with the development environment active:

```sh
ruff check --select E4,E7,E9,F scripts/ci/plan.py scripts/ci/impact.py \
  scripts/ci/test_plan.py scripts/ci/tests/test_planning.py scripts/ci/tests/test_ci.py
python scripts/ci/metadata.py
.scratch/openspec-adoption/tools/actionlint/actionlint .github/workflows/validation.yml
git diff -- subsys/clock subsys/desktop tests/subsys/desktop/boot_logo/CMakeLists.txt \
  | perl ../zephyr/scripts/checkpatch.pl --no-tree -
OPENSPEC_TELEMETRY=0 npm run spec:check
OPENSPEC_TELEMETRY=0 npm run openspec -- validate --archived --strict --no-interactive
python scripts/ci/license_policy.py --output .scratch/simplify-repository/license-policy
git diff --check
```

Ruff, actionlint, checkpatch and whitespace checks passed. Metadata validation
found 125 tracked documents and 105 unique test/sample scenarios. Local-link
validation passed for 87 tracked and new Markdown/RST files; external URLs and
anchors are outside that check. Syntax checks including new files passed for
128 YAML/TOML documents. OpenSpec strict validation passed and CLI apply
instructions include the configured context and all three operation guidelines.
The development-validation main spec contains the three implemented requirements.
Repository license policy passed with zero remaining metadata findings and 29
existing exemptions; this is not a claim of full REUSE compliance.

The final review retained independent protocol, security, resource, persistence
and lifecycle protections and did not modify generated skills/assets. Raw logs,
Red/Green output, equivalence results and plan verification are under ignored
`.scratch/simplify-repository/`; the results above are the shared review record.
The implemented change remains active for team review; it has not been archived.

### Review corrections on 2026-09-30

The second review identified two selection gaps: LICENSE and
LICENSES/Apache-2.0.txt are CLI inputs, and Indicator audio/feedback are direct
consumers of Power events. Two new planner regression tests first produced four
expected failing subcases. After the narrow fixes, the planner passed 30 tests
and the complete CI suite passed 77. Ruff passed for the changed Python files.
Commands, from the repository root with the development environment active:

```sh
python -m unittest discover -s scripts/ci/tests -p test_planning.py
python -m unittest discover -s scripts/ci/tests
ruff check --select E4,E7,E9,F scripts/ci/plan.py scripts/ci/tests/test_planning.py
python scripts/ci/test_plan.py generate --workspace .. \
  --snapshot .scratch/simplify-repository/follow-up/power-plan/snapshot
```

The discovery snapshot uses `plan.select(['subsys/power/power.c'])`. Real Twister
discovery and filtering select both Indicator scenarios for QEMU execution.
Its 16 runtime and five compile instances form disjoint, complete shards. This
checks selection; those 21 instances were not executed as a new test matrix.
CLI input selection enables host/Rust/CLI checks without SDK or product builds;
pure metadata retains source-only selection. No license or CLI implementation
changed, so Rust/package builds were not repeated locally.

MeshCore UI now uses the existing shared predicates at 22 call sites. All three
removed function bodies match the shared functions after whitespace normalization;
the six-file comparison contains only the intended deletions, substitutions,
header inclusion and line wrapping. No old predicate symbols remain. Checkpatch
reported zero errors and zero warnings. From `west topdir`:

```sh
west build -p auto --sysbuild -b mesh_probe_r2/nrf54l15/cpuapp meshbus/apps/meshbus \
  -d meshbus/.scratch/simplify-repository/product-r2 -o=-j4
```

The incremental product build passed. All five changed C files have current
objects in the Desktop/MeshCore-enabled product build. No flash or physical UI
validation was performed. Development and product README examples now use
pristine auto, with fresh directories retained for target/authentication changes.

The main spec includes the clarified CLI-input and Power-integration scenarios.
OpenSpec strict validation passed for the main spec, active change and existing
archive. Metadata checks passed for 125 tracked documents and 105 scenarios;
checks including new files passed for 128 YAML/TOML documents and local links in
87 Markdown/RST documents. `git diff --check` passed. Repository license policy
passed with zero remaining findings and the same 29 exemptions using:

```sh
python scripts/ci/license_policy.py \
  --output .scratch/simplify-repository/follow-up/license-policy
```

Raw follow-up logs and equivalence/discovery records are under
`.scratch/simplify-repository/follow-up/`. The active enterprise workspace is
unchanged; hosted SDK CI and hardware qualification remain unexecuted. No new
framework, dependency, generated skill edit, commit or publication was introduced.
