# OpenSpec workflow

Meshbus uses the upstream `spec-driven` schema and Codex's generated OpenSpec
skills. Shared plans and requirements are versioned with the code.

## Setup

Use Node.js 20.19.0 or newer. From the Meshbus Git root:

```sh
npm ci --ignore-scripts --no-audit --no-fund
npm run openspec -- --version
npm run openspec -- list
```

The root npm package is development tooling only. `package-lock.json` pins the
CLI and its dependency graph; Zephyr and Rust builds do not consume it. Use
`npm run openspec -- <arguments>` wherever upstream examples say `openspec`.
Set `OPENSPEC_TELEMETRY=0` in your environment to disable upstream usage telemetry.

Codex reads the checked-in `.agents/skills/openspec-*` skills. Reload the session
if they are not listed. In Codex CLI/IDE use the skill names below; in the desktop
app select the corresponding skill from Skills.

## Standard workflow

| Action | Codex skill | Result |
| --- | --- | --- |
| Explore an uncertain idea | `$openspec-explore` | Read-only investigation and discussion |
| Propose a named change | `$openspec-propose` | Proposal, applicable spec deltas, design and tasks |
| Revise the agreed plan | `$openspec-update-change` | Updated change artifacts |
| Implement reviewed work | `$openspec-apply-change` | Implementation and task progress |
| Synchronize requirements | `$openspec-sync-specs` | Spec deltas applied to main specs |
| Finalize completed work | `$openspec-archive-change` | Spec synchronization when needed, then dated archive |

Proposal and implementation are separate actions in the upstream workflow.
Review the proposed behavior and scope, then explicitly apply the change.
Routine in-scope fixes during apply do not require another proposal. Revise the
change when a decision alters its requirements or acceptance.

```text
openspec/
  config.yaml                  project context and artifact rules
  specs/<capability>/spec.md    current agreed requirements
  changes/<change>/
    .openspec.yaml             schema and change metadata
    proposal.md                why and what
    specs/<capability>/spec.md  requirement deltas
    design.md                  implementation decisions, when needed
    tasks.md                   executable work and verification
  changes/archive/             completed change records
```

Add capability specs as behavior is introduced or deliberately documented from
source. Initialization does not claim to specify the entire existing firmware.
For a change with no spec-level behavior change, such as tooling or a pure
refactor, use the upstream `skip_specs: true` change metadata instead of inventing
product requirements. Small corrections can use focused tests directly when no
shared plan or requirement change is needed.

## Implementation and review

Specs describe agreed behavior, not proof of current implementation. Establish
acceptance before implementation and follow the
[implementation loop](../docs/testing.md#implementation-loop) and
[coverage selection](../docs/testing.md#choosing-coverage) for the relevant work.
Resolve source/spec discrepancies by revising the agreed change, not weakening
its acceptance to match an incorrect implementation.

Coordinate one owner per active change through the team's issue or PR. Split
independent tasks explicitly and avoid concurrent edits to the same files.
Each change keeps one `tasks.md`; task checkboxes are progress, not a locking
mechanism. Review overlapping spec deltas against the latest shared baseline
before synchronization and integration.

Review the proposal, scenarios, tests and actual diff together. Record concise
verification outcomes, source/build identity and unresolved acceptance under
`## Validation` in the existing `tasks.md`, using shareable CI/artifact references
when available. Records must be understandable without private logs; follow
[output handling](../DEVELOPMENT.md#outputs-and-records) for raw data and deliverables.
Repository operations follow [AGENTS.md](../AGENTS.md); a checked task grants no
additional authorization.

Build, simulation, physical-device and release claims remain separate. Follow
[testing guidance](../docs/testing.md) for the evidence each claim needs.
Mark a task complete only after its stated acceptance passes. Fix in-scope
failures, rerun affected checks, and reuse valid evidence; optional follow-ups
remain separate. If blocked, report the exact unmet criteria and blocker without
marking them complete. Archive only completed work; pending required acceptance
remains open.

Retain completed change folders for [targeted history retrieval](../DEVELOPMENT.md#history-retrieval).
Do not read all archives during ordinary work or rewrite past results and paths
to match current guidance. Current domain guides hold lasting constraints and
their necessary rationale; archives preserve how a particular change arrived there.

## Validation

```sh
npm run spec:check
npm run openspec -- validate --archived --strict --no-interactive
```

CI runs these in Source checks. They validate artifact structure and archived
task completion, not implementation correctness, TDD history or review approval.
When there are no specs or active changes yet, the CLI reports nothing to validate;
add requirements with the first behavioral change.

## Updating the integration

Upgrade the exact CLI version in `package.json` and regenerate its lockfile as
an intentional development-tool update. Then regenerate the Codex skills:

```sh
npm run openspec -- init --tools codex --profile core --no-animation
```

Review all generated changes and the upstream license, run the checks above,
and run the repository license policy. Keep generated skills byte-for-byte
upstream; put project customization in `config.yaml`. The upstream MIT notice
is retained in [.agents/skills/OPENSPEC-LICENSE](../.agents/skills/OPENSPEC-LICENSE).

Official references: [setup](https://openspec.dev/docs/setup),
[workflow](https://openspec.dev/docs/quickstart), and
[project configuration](https://openspec.dev/docs/configuration/config-yaml).
