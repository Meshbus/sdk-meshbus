# Optional Engineering Plans

Plans are lightweight continuity records for work that spans sessions or needs
an explicitly reviewed scope. They are not required for ordinary user requests
and do not replace direct user authorization.

## When To Use A Plan

Create or update a plan only when the user asks for one, or when a long task
would materially benefit from a resumable record and the user agrees. Small
changes should proceed from the current task without plan ceremony.

Plan files live under `docs/agent/plans/` and are loaded only when named by the
task. Use one file per effort; do not create separate charter, phase, status
folder, or execution-log files unless the user requests that structure.

## Minimal Format

```md
# <Task>

Status: DRAFT | ACTIVE | BLOCKED | DONE

## Goal

## Scope

## Non-Goals

## Decisions

## Work

- [ ] <next concrete item>

## Validation

## Notes
```

Use the statuses as records, not permission gates:

- `DRAFT`: scope is still being discussed.
- `ACTIVE`: accepted work is in progress.
- `BLOCKED`: the blocking condition and required decision are recorded.
- `DONE`: completed work, validation, and residual risk are summarized.

An explicit user request can start or change work regardless of the recorded
status. If a plan conflicts with the latest user direction, update the plan or
report the mismatch; do not reject an otherwise clear task solely because a
file was not moved or a marker was stale.

## Maintenance

Keep decisions and remaining work concise. Do not duplicate commit history,
full command transcripts, generated logs, or information already authoritative
in source and tests. Preserve unrelated plan files and record unverified
hardware or follow-up work explicitly.
