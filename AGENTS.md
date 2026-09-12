# Meshbus Repository Entry

This repository owns both the reusable Meshbus Zephyr module and the product
firmware at `apps/meshbus/`. Product composition, boot/release policy, the host
CLI, and distribution are maintained here. The parent directory is a local
Zephyr west workspace, not a product source repository.

Before substantive changes, review, or validation, read this root `AGENTS.md`
and applicable local rules along each target path. Reuse instructions already
read in this task unless their content or the task scope changes.
Public headers, schemas,
Kconfig, CMake, devicetree, test metadata, source, and tests remain the technical
sources of truth.

Services and public headers are flat under `subsys/<module>/` and
`include/<module>/`. Service identifiers use `mbs_` / `MBS_`; the product and
west module remain Meshbus. For namespace boundaries and migration guidance,
read [the SDK integration guide](README.md#zephyr-integration) and the scoped
include/subsys rules before renaming identifiers or configuration.

Use `git rev-parse --show-toplevel` from this repository for the source root
and `west topdir` for the workspace. The active manifest is `west.yml` here;
`apps/meshbus/` is a consumer of this same module. Firmware and SDK changes share
one Git history. Treat sibling west projects, toolchains, and shared caches as
read-only build context unless the user requests changes there.

Read `DEVELOPMENT.md` for environment, workspace, build, or test work, and
`DISTRIBUTION.md` for packaging, signing, update, or release work.
For selecting validation tools or collecting device evidence, read
`docs/agents/testing.md`.

Read these local rules only when the task enters their scope:

- `apps/meshbus/AGENTS.md` for product composition and sysbuild policy.
- `apps/meshbus/boards/AGENTS.md` for product device profiles and partitions.
- `include/AGENTS.md` for public Meshbus API and ABI.
- `subsys/AGENTS.md` for service runtime and persistence.
- `subsys/desktop/AGENTS.md` for Desktop and ZUI integration.
- `tests/subsys/AGENTS.md` for tests and evidence boundaries.

Preserve unrelated changes and keep patches narrow. Local source/log inspection,
offline debugging, and device-free tests are within the requested development
scope; continue through in-scope fixes and affected checks without asking again.
Obtain explicit authorization for dependency or manifest changes, remote access,
device access or control (including flash, reset, and probe/debug connections),
signing, and publication. Stage, commit, or push only when requested. Keep
secrets out of source, logs, and responses.

Continue work already authorized by the user without requesting the same
approval again. Skill workflows must respect the user's requested scope and
this repository's authorization boundaries. For review-only requests, inspect
and propose changes without editing. Ask only when a missing decision materially
changes scope, acceptance, or an action requiring explicit authorization;
continue independent work while that decision is pending.

Treat existing generated artifacts as evidence; do not edit them by hand.
For authorized validation, generate outputs in a task-specific directory and
preserve unrelated build outputs.

Local specifications and tickets live under the ignored `.scratch/` directory.
Use the tracker or ticket location already established for the task. Missing
tracker configuration does not block work independent of tracker writes.
Read `docs/agents/issue-tracker.md` when writing or fetching tickets and
`docs/agents/triage-labels.md` when setting their category, triage, or progress.
For domain terminology or architectural decisions, follow
`docs/agents/domain.md`, root `CONTEXT.md`, and relevant `docs/adr/` entries.
Historical records may use the former workspace `app/` and SDK `meshbus/`
paths. Preserve dated evidence; use current repository paths for new work.

Select the smallest build or test that proves the affected behavior. Read the
nearest `sample.yaml` or `testcase.yaml` for supported targets and scenarios,
run Zephyr commands from `west topdir`, and report the exact commands and
results. Once required checks pass, expand validation only for new changes,
failures, or unresolved concerns. Lead the final response with the outcome and
state unverified hardware and higher-level qualification relevant to the task.
