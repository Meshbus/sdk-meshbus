# AGENTS.md - Meshbus Zephyr SDK Agent Router

This is the short entry file for agents opened in `sdk-meshbus/`. Keep durable area
rules in the nearest nested `AGENTS.md`; treat local `testcase.yaml`,
`sample.yaml`, Kconfig, CMake, DTS, and source files as the source of truth.

## Context Budget

Default to reading only this file at session start. Do not preload
`docs/agent/*`, `PLANS.md`, `docs/agent/plans/*`, or `docs/agent/reviews/*`.

Open `docs/agent/index.md` only when the task needs task-specific guidance,
then open only the single matching route first. Add another route only when the
task crosses that boundary.

Open `PLANS.md` only for task charters, phase documents, plan lifecycle, or
plan migration work. Open `docs/agent/plans/*` only for the specific task or
phase being discussed or executed. Open `docs/agent/reviews/*` only for review
or audit work that names or clearly needs those files.

## Scope And Workspace

`sdk-meshbus/` is the active Git repository and the default writable scope. It is a
Zephyr module repository inside the parent west workspace that also contains
`.west/` and `zephyr/`. Zephyr Python tooling is provided by the shared user
environment at `~/.zephyr/env`.

The parent workspace, `.west/`, `zephyr/`, toolchains, downloaded modules, and
sibling projects are build context and read-only references unless explicitly
requested. Prefer a local `sdk-meshbus/` solution before any Zephyr-tree change.

For task-specific build, test, workspace, or review detail,
route through `docs/agent/index.md`. For task charters, phase documents, or
the planning/active/completed lifecycle, follow `PLANS.md`.

## Workspace Discovery

Do not hardcode `../zephyr` or absolute workspace paths. Use `west topdir`
after making `west` available.

If `west` is not on PATH, activate the shared Zephyr environment:

```sh
source ~/.zephyr/env/bin/activate
west topdir
```

Run Zephyr builds and Twister from the `west topdir` root, using `sdk-meshbus/<path>`
arguments. If `west topdir` fails, report the invalid workspace instead of
guessing paths.

## Repository Map

- `zephyr/module.yml`: module integration for Kconfig, CMake, board roots, DTS
  roots, and module extension roots.
- `include/`: public headers and stable module APIs.
- `drivers/`: Meshbus-owned Zephyr drivers and integration glue.
- `dts/`: Meshbus-owned devicetree bindings.
- `boards/`: Meshbus/Qikira board definitions, DTS, defconfig, and board Kconfig.
- `subsys/`: Zephyr-facing subsystems, including Meshbus, MeshCore adapters,
  DFU, U8G2, and ZUI.
- `samples/`: board-facing and integration build surfaces.
- `tests/`: Twister/ztest suites and test support trees.
- `scripts/`: repo-local helper tooling.

## Area Rules

Read the closest matching file before changing an area:

- `include/zephyr/meshbus/AGENTS.md`: public Meshbus headers and ABI.
- `subsys/meshbus/services/AGENTS.md`: Meshbus service implementations,
  settings, shell, MCUmgr, state, and ZBus runtime rules.
- `tests/subsys/meshbus/AGENTS.md`: Meshbus service contract-test rules.
- `subsys/meshbus/services/desktop/AGENTS.md`: Meshbus Desktop/ZUI service.
- `subsys/meshbus/services/desktop/apps/AGENTS.md`: built-in desktop apps.
- `subsys/meshbus/services/desktop/widgets/AGENTS.md`: dashboard widgets.

If no nested rule exists, inspect nearby CMake, Kconfig, devicetree, tests, and
samples before deciding the edit shape.

MeshCore is an external module. Keep changes to that library in its own
repository; SDK-side adapters and tests remain in this repository.

## Hard Rules

- Do not modify `../zephyr`, `../.west`, sibling west projects, manifests,
  toolchain files, or generated build artifacts unless explicitly requested.
- Do not run `west update`, `west flash`, `west debug`, hardware tests, or
  destructive commands without explicit authorization.
- Do not add third-party dependencies, binary blobs, or new toolchain
  requirements without explicit approval.
- Keep patches narrow. Do not reformat unrelated files or perform broad churn.
- Public headers are contracts; align implementation, tests, samples, and docs
  when observable API or ABI changes.
- Prefer Kconfig/devicetree/board overlays over hardcoded board-specific
  constants.
- Keep generated files and build outputs out of source edits.

## Implementation Defaults

- Public API change: start in `include/`, then update implementation and tests.
- Runtime/service behavior: start in `subsys/`, then update public tests or
  samples if behavior is observable.
- External MeshCore behavior: change the MeshCore repository separately; keep
  SDK adapter changes and compatibility tests local.
- Hardware description: start in `boards/` or `dts/`, then compile a consuming
  sample or test.
- Validation/sample work: start in `tests/` or `samples/`, and avoid changing
  implementation when the user asked for tests-only coverage.

Meshbus service tests are rooted at
`tests/subsys/meshbus/services/<service>`. They define public API/public ZBus
contract scenarios on `qemu_x86` and, where declared, separate C2 service-DUT
scenarios in the same application or a lifecycle-specific child application.
Multi-device tests belong under `tests/subsys/meshbus/system`, performance
tests under `tests/subsys/meshbus/performance`, reusable non-application test
support under `tests/subsys/meshbus/common`, and lightweight evidence guidance
under `tests/subsys/meshbus/validation`. Service samples under
`samples/subsys/meshbus/services/*` are board-facing startup/manual validation
surfaces, commonly built for `idea_mesh_tracker_c2/nrf54l15/cpuapp`.

## Validation Router

Select the smallest verification that proves the change. Run Zephyr build and
Twister commands from `west topdir`, using `sdk-meshbus/<path>` arguments.

For build command selection, remote build offload, or build failures, read
`docs/agent/build.md`. For remote workspace preflight, `west remote doctor`, or
session management, read `docs/agent/workspace.md`. For Twister, ztest, sample
verification, remote Twister, serial evidence, or hardware-test policy, read
`docs/agent/testing.md`.

If validation needs hardware, flashing, debug probes, network access, or
parent-workspace mutation, stop and ask or report the exact pending command.

## Completion Checklist

Before final response, confirm scope, mention relevant nested rules, report
commands and results, and state unverified hardware or build coverage. If code
and durable guidance disagreed, update the closest rule file or call out drift.

## Commit Message Rules

When generating git commit messages in this repository, follow Zephyr's
commit message guidelines:

- Use `[area]: [summary]` as the subject format.
- Keep the subject under 72 characters.
- Add one blank line after the subject.
- Always include a non-empty body explaining what changed, why it changed,
  why this approach was chosen, and how it was verified.
- Wrap body lines around 75 characters where practical.
- Choose the area from the touched files; if unsure, inspect `git log <file>`.
- Do not add `Signed-off-by:` automatically.
