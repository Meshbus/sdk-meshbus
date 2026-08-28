# Meshbus Plan Workflow

This file defines the repository convention for engineering plans. It is a
workflow specification, not a plan index and not an execution log.

Do not add concrete task status, phase progress, validation commands, or
domain-specific implementation steps to this file. New development plans must
follow the structure and lifecycle defined here.

## Purpose

The plan system exists to keep long-running engineering work explicit,
reviewable, and resumable across Codex sessions.

It separates three concerns:

- stable task intent;
- phase-level discussion, execution, and archival state;
- repository rules that decide when Codex may implement work.

`PLANS.md` owns only the third concern.

## Directory Model

Task and phase documents live under `docs/agent/plans/`.

| Path | Purpose |
| --- | --- |
| `docs/agent/plans/<task>.md` | Stable task charter for a larger engineering effort. |
| `docs/agent/plans/planning/` | Phase documents still under discussion or design review. |
| `docs/agent/plans/active/` | Phase documents accepted for execution. |
| `docs/agent/plans/completed/` | Finished phase documents kept for audit and history. |

Only phase documents move through `planning/`, `active/`, and `completed/`.
The task charter remains at `docs/agent/plans/<task>.md`.

## Task Charters

A task charter describes the overall engineering effort. It should be stable
enough for future agents to understand why the work exists and how the phases
fit together.

Task charters should contain:

- background and motivation;
- goals and non-goals;
- architectural boundaries and durable decisions;
- a phase list with links to phase documents;
- current high-level state of each phase;
- cross-phase risks, dependencies, and unresolved questions.

Task charters should not contain detailed execution logs. Those belong in the
phase document that produced the evidence.

### Task Charter Template

Use this shape for new task charters:

```md
# <Task Name>

## Background

## Goals

## Non-Goals

## Boundaries

## Phases

| Phase | Status | Document | Purpose |
| --- | --- | --- | --- |
| 0 | `[PLANNING]` | `planning/<task>-phase-0-<name>.md` | <purpose> |

## Cross-Phase Notes

## Open Questions
```

## Phase Documents

A phase document is the smallest unit of discussion, execution, and archival
state. Its filename should include the parent task slug, phase number, and a
short purpose.

Recommended filename form:

```text
<task>-phase-<number>-<short-purpose>.md
```

A phase document should contain:

- parent task link;
- status marker;
- goal;
- required reading;
- scope;
- out of scope;
- plan or work breakdown;
- progress checklist;
- validation expectations;
- completion criteria;
- final notes when archived.

The phase document is the execution contract once it enters `active/`.

### Phase Document Template

Use this shape for new phase documents:

```md
# <Task Name> Phase <N>: <Phase Name>

Parent task: `../<task>.md`
Status: `[PLANNING]`

## Goal

## Required Reading

## Scope

## Out Of Scope

## Work Breakdown

- [ ] <item>

## Validation

## Completion Criteria

## Completion Notes
```

## Lifecycle

Plans move through this lifecycle:

```text
planning -> active -> completed
```

### Planning

Use `docs/agent/plans/planning/` for phase documents that are still being
discussed, researched, scoped, or reviewed.

Planning phase rules:

- Codex may discuss, refine, and update the phase document.
- Codex must not treat planning documents as authorization to implement code.
- Open questions and alternative approaches are allowed.
- The phase should not move to `active/` until the user explicitly accepts it.

### Active

Use `docs/agent/plans/active/` for phase documents that the user has accepted
for execution.

Active phase rules:

- Codex may implement the work described by the phase document.
- Codex should update status and progress markers while executing.
- The active phase document is the source of truth for scope and completion
  criteria.
- If implementation discovers a material scope change, Codex should update the
  phase document or return it to discussion instead of silently expanding work.

### Completed

Use `docs/agent/plans/completed/` for phase documents whose work is finished or
explicitly closed.

Completed phase rules:

- Move the phase document from `active/` to `completed/`.
- Record what changed, what was verified, and what remains unverified.
- Keep residual risks or follow-up work explicit.
- Update the parent task charter so its phase list matches the archived state.

No completed phase should remain in `active/`.

## Status Markers

Phase documents may use these status markers. The marker must match the
directory that contains the phase document.

Planning states, valid only under `docs/agent/plans/planning/`:

- `[PLANNING]`: under discussion; not execution-authorized.
- `[READY]`: accepted and ready to move to `active/`.

Active states, valid only under `docs/agent/plans/active/`:

- `[TODO]`: active but not yet started.
- `[IN_PROGRESS]`: active and currently being implemented.
- `[WAITING_HW]`: code/test side is ready and hardware feedback is pending.
- `[BLOCKED]`: blocked by an explicit issue recorded in the phase document.

Completed states, valid only under `docs/agent/plans/completed/`:

- `[DONE]`: completed and archived.
- `[CANCELLED]`: intentionally closed without implementation.

`[READY]` is not execution authorization by itself. Codex may execute only
after the phase document has moved to `active/`.

Status markers must match the directory state. For example, a `[DONE]` phase
must not stay in `active/`, and a `[TODO]` phase must not stay in `planning/`.

## Migration Rules

When creating a new plan:

1. Create or update the parent task charter at `docs/agent/plans/<task>.md`.
2. Create the first phase document under `docs/agent/plans/planning/`.
3. Keep the phase in `planning/` until the user accepts it.

When starting accepted work:

1. Move the accepted phase document from `planning/` to `active/`.
2. Update its status marker for execution.
3. Update the parent task charter to point at the new phase location.

When completing work:

1. Update the active phase document with final progress and validation notes.
2. Move it from `active/` to `completed/`.
3. Update its status marker.
4. Update the parent task charter.

Do not use `todo/` as a plan lifecycle directory. Work that is not yet accepted
belongs in `planning/`; accepted work belongs in `active/`.

## Codex Behavior

Codex should follow these rules when working with plans:

- Treat this file as the durable workflow contract for future plans.
- Use the parent task charter for overall context.
- Use the current phase document for exact scope.
- Only implement phase documents in `active/`.
- Keep phase progress markers current during execution.
- Preserve unrelated plan files and user edits.
- Avoid mixing workflow cleanup with implementation work unless explicitly
  requested.

If repository state disagrees with these rules, Codex should call out the drift
and either fix the plan metadata or ask for direction before implementing
ambiguous work.
