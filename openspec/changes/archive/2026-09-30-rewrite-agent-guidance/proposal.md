# Task-scoped repository guidance

## Why

Current policies are split between operating guides and architecture records,
while temporary-report conventions encourage retaining task-local context as
project knowledge. Contributors and agents need current contracts on demand,
with history available without making it part of every task.

## What Changes

- Route repository instructions by task and give each rule one owning guide.
- Merge the seven current architecture decisions into their domain guides,
  preserve their constraints and rationale, and remove the separate ADR tree.
- Keep OpenSpec archives and Git history searchable on demand; add a short
  history-retrieval procedure without introducing another skill or knowledge bank.
- Report routine findings in conversation; keep formal acceptance in the change's
  existing task record and use tool outputs or external temporary directories.
- Update live examples and attribution metadata while preserving existing
  histories, local temporary files and generated OpenSpec skills.

## Capabilities

### New Capabilities

None. This documentation and agent-guidance change declares `skip_specs: true`.

### Modified Capabilities

None. Existing development-validation requirements and product behavior remain
unchanged.

## Impact

Repository instructions, OpenSpec configuration, contributor/product/MBA/license
guides, current command examples and REUSE metadata. No public API, protocol,
licensing policy, dependency, generated skill, global agent configuration or
personal memory changes. No device or remote operations, signing, staging,
commits or publication. The user approved this plan and its implementation,
including archival after acceptance, in the requesting conversation.
