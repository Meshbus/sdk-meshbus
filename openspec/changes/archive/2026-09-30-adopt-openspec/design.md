# OpenSpec integration design

## Context

The repository contains Zephyr firmware/module code, Rust host tools and Python
CI helpers. Source checks already run independently of the firmware workspace.
See proposal.md for the intended workflow change.

## Goals / Non-Goals

**Goals:** use standard upstream artifacts and Codex skills; make tool versions
reproducible; preserve engineering contracts and evidence boundaries; validate
shared planning files in the existing CI gate.

**Non-Goals:** a custom workflow schema, a new issue tracker, bulk specification
of existing firmware, changes to runtime code, or enabling other editor integrations.

## Decisions

1. Pin `@fission-ai/openspec` 1.13.2 in a private root npm package and lock its
   dependency graph. Contributors use `npm ci` and `npm run openspec -- ...`;
   the agent entry and project context expose that invocation. This avoids a
   dependency on whichever global CLI a contributor has installed.
2. Generate the six Codex core skills with upstream `init --tools codex
   --profile core`. Keep their bytes unchanged and annotate their MIT provenance
   separately from project-owned Apache-2.0 documents.
3. Use `openspec/config.yaml` for project facts, artifact rules and operation
   guidance. Keep the standard schema. Product specs grow through actual changes;
   this tooling change declares `skip_specs: true`.
4. Use one root agent entry. Engineering contracts live in DEVELOPMENT.md and
   docs/testing.md; proposals and tasks live in OpenSpec. Temporary logs remain
   ignored and are not the team's sole acceptance record.
5. Add OpenSpec validation to Source checks, which already participates in
   Required checks. Planning-only paths select this layer; mixed source changes
   retain their normal firmware or CLI coverage. Validate archived task completion
   separately from active specs and changes.

## Risks / Trade-offs

- Generated skills carry upstream workflow semantics. Review regeneration diffs
  on upgrades; repository authorization still controls consequential operations.
- Structural validation cannot prove implementation correctness or human review.
  Tasks retain their own verification and acceptance evidence.
- Node.js is a new contributor/CI prerequisite for planning validation, but does
  not enter firmware or Rust builds.
- Branch-local task state does not prevent concurrent edits. Contributors coordinate
  ownership through their shared issue/PR and review overlapping spec deltas.
