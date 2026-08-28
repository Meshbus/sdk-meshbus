# Agent Docs Index

This directory contains task-specific guidance for Codex agents working in the
`sdk-meshbus/` repository. Do not read every file by default. Start from the root
`AGENTS.md`, then open only the file that matches the current task.

Open only the single matching route first. Add another route only when the task
crosses that boundary.

## Routing

| Task | Read |
| --- | --- |
| Workspace boundary, west layout, sandbox, or parent-directory access | `workspace.md` |
| Remote workspace preflight, `west remote doctor`, or remote session management | `workspace.md` |
| Plan workflow, task charter, phase lifecycle, or planning/active/completed migration | `../../PLANS.md` |
| Build command selection, board choice, module visibility, sysbuild, or build failure | `build.md` |
| Remote build offload or fetched build artifacts | `build.md` |
| Twister, ztest, sample verification, test creation, test failure, or hardware-test policy | `testing.md` |
| Remote Twister offload | `testing.md` |
| Final self-review, PR summary, scope check, or unverified assumptions | `review.md` |

## Local Rule Files

For code changes, also read the nearest nested `AGENTS.md`:

- `include/zephyr/meshbus/AGENTS.md`
- `subsys/meshbus/services/AGENTS.md`
- `subsys/meshbus/services/desktop/AGENTS.md`
- `subsys/meshbus/services/desktop/apps/AGENTS.md`
- `subsys/meshbus/services/desktop/widgets/AGENTS.md`
- `tests/subsys/meshbus/AGENTS.md`

If a task can be completed from `AGENTS.md`, the nearest local rule file, and
the source tree, do not read extra docs.
