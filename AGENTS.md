# Meshbus SDK Entry

This is an independent Git repository for reusable services, public APIs,
drivers, protocols, UI components, samples, and tests. Product composition,
boot/release policy, host CLI, and distribution belong to the firmware repository.

Before substantive changes, review, or validation, read this root `AGENTS.md`
and applicable local rules along each target path. Reuse instructions already
read in this task unless their content or the task scope changes.
Public headers, schemas,
Kconfig, CMake, devicetree, test metadata, source, and tests remain the technical
sources of truth.

Use `git rev-parse --show-toplevel` for the SDK Git root and `west topdir` for
the consuming workspace. Keep SDK work and firmware-product work separately
scoped. Treat sibling west project sources, toolchains, and shared caches as
read-only build context unless the user requests changes there.

Read these local rules only when the task enters their scope:

- `include/zephyr/meshbus/AGENTS.md` for public Meshbus API and ABI.
- `subsys/meshbus/services/AGENTS.md` for service runtime and persistence.
- `subsys/meshbus/services/desktop/AGENTS.md` for Desktop and ZUI integration.
- `tests/subsys/meshbus/AGENTS.md` for tests and evidence boundaries.

Preserve unrelated changes and keep patches narrow. Obtain explicit
authorization for dependency or manifest changes, remote or hardware actions,
flash, reset, debug, signing, and publication. Stage, commit, or push only when
requested. Keep secrets out of source, logs, and responses.

Continue work already authorized by the user without requesting the same
approval again. Skill workflows must respect the user's requested scope and
this repository's authorization boundaries. For review-only requests, inspect
and propose changes without editing. Ask only when a missing decision materially
changes scope, acceptance, or an action requiring explicit authorization;
continue independent work while that decision is pending.

Treat existing generated artifacts as evidence; do not edit them by hand.
For authorized validation, generate outputs in a task-specific directory and
preserve unrelated build outputs.

Use the tracker or ticket location already established for the task. A missing
SDK-local tracker configuration does not block source review, diagnosis, or
other work independent of tracker writes. Determine a destination only when a
required write has no established location; do not copy a consuming product's
tracker configuration into this repository merely to satisfy a skill.

Select the smallest build or test that proves the affected behavior. Read the
nearest `sample.yaml` or `testcase.yaml` for supported targets and scenarios,
run Zephyr commands from `west topdir`, and report the exact commands and
results. Once required checks pass, expand validation only for new changes,
failures, or unresolved concerns. Lead the final response with the outcome and
state unverified hardware and higher-level qualification relevant to the task.
