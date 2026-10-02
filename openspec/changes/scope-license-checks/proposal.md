# Proposal

## Why

Routine license checks scan unrelated files and can accidentally include ignored
website dependencies and generated output. Contributors need findings about
their changes, with a separate complete audit for shared licensing rules.

## What Changes

- Check changed files by default, including local staged, unstaged and untracked
  inputs; select complete audits explicitly and for shared metadata changes.
- Build the scan input from Git file lists, excluding ignored untracked output.
- Temporarily exclude the entire root `web/` tree in every SPDX scan mode.
- Preserve strict findings on selected inputs and actual release material checks.
- Record scope in reports and align CI, contributor and Agent guidance.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `development-validation`: Define routine and complete license scan selection.

## Impact

CI license tooling, regression tests, validation workflow and current contributor
guidance. Existing third-party notices and metadata remain intact. No dependency
updates, release packaging changes, Git publication or device work is needed.
