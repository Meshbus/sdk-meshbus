# Meshbus SDK Agent Guide

This file is the entry point for agents working in this Git repository. Keep
startup context small: read this file, then the nearest area rule and one task
guide at a time. Add another guide only when the work crosses that boundary.

## Source Of Truth

The user's task defines the requested outcome. Repository rules define safety
and ownership boundaries. For implementation details, prefer the closest
`AGENTS.md`, then local Kconfig, CMake, devicetree, `testcase.yaml`,
`sample.yaml`, source, and tests.

Do not preserve a stale command or inventory merely because it appears in an
agent guide. Follow the current source of truth, report the drift, and change
guidance only when it is part of the task.

## Repository And Workspace

Treat the root returned by `git rev-parse --show-toplevel` as the writable SDK
scope. The checkout may be either the west manifest repository or an imported
project in another workspace; never infer its west path from the repository
name.

When Zephyr tooling is needed:

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
```

Use discovered roots or absolute source paths in local commands. Do not
hardcode `../zephyr`, `sdk-meshbus/`, `meshbus/`, or a machine-specific
workspace path.

Parent-workspace source trees, Zephyr, sibling projects, and toolchains are
read-only context unless the user explicitly includes them in scope. Validation
may create task-owned ignored build/Twister output; do not manually edit or
attribute pre-existing generated artifacts to the task.

## Task Guides

Open only the guide currently needed:

- Workspace layout, module visibility, or remote execution:
  `docs/agent/workspace.md`
- Build selection, boards, sysbuild, or build failures:
  `docs/agent/build.md`
- Twister, tests, hardware evidence, or serial validation:
  `docs/agent/testing.md`
- Optional multi-session plan tracking: `PLANS.md`

Remote helper details belong in `scripts/remote/README.md` and command
`--help`, not in the default agent context.

## Nearest Area Rules

Read the closest matching file before editing that area:

- `include/zephyr/meshbus/AGENTS.md`: public Meshbus API and ABI
- `subsys/meshbus/services/AGENTS.md`: service runtime and persistence
- `subsys/meshbus/services/desktop/AGENTS.md`: Desktop runtime
- `subsys/meshbus/services/desktop/apps/AGENTS.md`: built-in apps
- `subsys/meshbus/services/desktop/services/AGENTS.md`: Desktop-local services
- `subsys/meshbus/services/desktop/widgets/AGENTS.md`: dashboard widgets
- `tests/subsys/meshbus/AGENTS.md`: Meshbus service tests

If no local rule exists, inspect adjacent source, metadata, tests, and samples
before choosing an implementation pattern.

MeshCore is an external module. Change external MeshCore behavior in its own
repository; keep SDK adapters and SDK tests here.

## Hard Rules

- Preserve unrelated user changes and keep patches narrow.
- Do not edit parent-workspace or sibling-project files without explicit scope.
- Do not run `west update`, flash, debug, device testing, reset, destructive
  shell commands, or remote repair/update operations without explicit user
  authorization for that action.
- Do not add dependencies, binary blobs, or toolchain requirements without
  approval.
- Never edit generated build output.
- Public API or ABI changes must keep implementation and observable tests in
  sync.
- Prefer Kconfig, devicetree, and board metadata over hardcoded hardware policy.

## Validation And Completion

Choose the smallest build, Twister suite, or text check that proves the change.
Use the platform declared by the nearest YAML rather than inventing a matrix.
Report the exact command and result, and distinguish build/QEMU evidence from
unverified hardware behavior.

Before finishing, check scope, `git diff --check`, relevant generated-file
exclusions, and the worktree so pre-existing changes are not attributed to this
task.

When asked for a commit message, use `[area]: [summary]`, keep the subject under
72 characters, and include a non-empty body explaining what changed, why, and
how it was verified. Do not add `Signed-off-by:` automatically.
