# Codex Self-Review Checklist

Use this before the final response, before staging, or before a PR summary.

## Scope Check

- Did the patch stay inside `sdk-meshbus/`?
- Did any change touch `../zephyr`, `../.west`, sibling west projects,
  manifests outside `sdk-meshbus/`, toolchains, caches, or generated build output?
- If anything outside `sdk-meshbus/` changed, was it explicitly requested?
- Are existing user changes preserved, including unrelated deletions such as
  files already removed before this task?

## Guidance Check

- Did you read the nearest nested `AGENTS.md` for touched areas?
- If code and durable guidance disagreed, did you update the closest rule file
  or call out the drift?
- Did root guidance stay short, with task detail kept in `docs/agent` or local
  rule files?

## Zephyr Check

- C code: nearby style, sectioning, logging, errno, and concurrency patterns are
  preserved.
- Public headers: declarations, ownership, units, buffer sizes, ABI values,
  implementation, tests, and samples are aligned.
- Kconfig: dependencies, defaults, ranges, and help text match runtime behavior.
- DTS/bindings/boards: property names, chosen nodes, overlays, and board
  ownership are correct.
- Drivers: related Kconfig, CMake, bindings, tests, and samples were checked.
- Meshbus services: settings, shell, MCUmgr, ZBus, and persistence behavior
  follow `subsys/meshbus/services/AGENTS.md`.
- Tests: qemu contract tests and hardware validation remain separate.

## Verification Check

- Was the smallest useful build, Twister run, or text check executed?
- Was `west topdir` used instead of hardcoded workspace assumptions?
- Did `west` require activating `~/.zephyr/env`?
- Did any verification require hardware, flashing, debug probes, network, or
  parent-workspace mutation?
- Are unverified hardware or integration assumptions stated clearly?

For docs-only changes, `git diff --check` is usually enough unless the docs
change commands that should be smoke-tested.

## Final Response Shape

Keep the final response short and concrete:

```text
Changed:
- <file>: <what changed>

Verification:
- <command>: <result>

Not verified:
- <hardware/build/scope reason, if any>

Notes:
- <workspace or follow-up notes, if useful>
```

Mention other dirty worktree entries only when they matter to the task or could
be mistaken for this change.
