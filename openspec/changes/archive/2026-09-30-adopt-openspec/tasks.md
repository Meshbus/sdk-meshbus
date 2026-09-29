<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tasks

## 1. Standard OpenSpec setup

- [x] 1.1 Pin the CLI and lockfile, generate Codex core skills and preserve upstream notices; verify a clean `npm ci`, CLI version and regeneration parity.
- [x] 1.2 Configure the standard schema and project-specific TDD guidance; verify `instructions` includes artifact rules and apply/archive guidance.

## 2. Repository integration

- [x] 2.1 Replace the old agent hierarchy with the OpenSpec entry and engineering docs; verify local links, metadata and absence of obsolete tracked references.
- [x] 2.2 Add source-only planning selection and CI validation; first reproduce the selection failure, then pass planner regressions and validate the workflow syntax.

## 3. Acceptance

- [x] 3.1 Validate a representative change lifecycle in isolated task storage, including rejected invalid scenarios and unfinished archived tasks; run strict validation and the repository license policy.
- [x] 3.2 Review the complete diff and record validation scope, local results and outstanding hosted CI evidence before archiving this tooling change.

## Validation

Local acceptance passed on 2026-09-30 (macOS arm64, Node.js 26.10.0, npm 11.19.1):

- `npm ci --ignore-scripts --no-audit --no-fund` installed the locked development
  dependencies successfully; `npm run openspec -- --version` returned `1.13.2`.
- A fresh isolated `init --tools codex --profile core` generated all six skills
  byte-for-byte identically. Empty specs/changes validated successfully.
- `instructions` injected project context, artifact rules and apply/archive
  guidance. A fixture change validated, synchronized into a main spec and
  archived successfully. Removing its scenario or leaving an archived task
  unchecked made strict validation fail as expected.
- The new source-selection test first failed for seven planning paths that
  unnecessarily selected firmware work. After the planner fix,
  `python -m unittest discover -s scripts/ci/tests` passed all 74 tests, including
  the mixed spec/runtime change case. These are CI-tool tests, not firmware runs.
- `actionlint .github/workflows/validation.yml` passed with checksum-verified
  actionlint 1.7.12. `ruff check --select E4,E7,E9,F scripts/ci/plan.py
  scripts/ci/tests/test_planning.py` passed.
- `python scripts/ci/metadata.py` passed (125 documents, 106 unique scenarios).
  A separate scan included new untracked artifacts: local links in 82 documents
  and YAML/TOML parsing passed. No active guides reference the deleted rule paths.
- `npm run spec:check` passed for this change with upstream `skip_specs: true`.
  `python scripts/ci/license_policy.py --output .scratch/openspec-adoption/license`
  passed with no remaining metadata findings. Full REUSE compliance is not
  claimed: repository policy retains 29 existing metadata exemptions and the
  unused BSL-1.0 distribution license text documented in LICENSING.md.
- The final diff was reviewed and `git diff --check` passed.

Hosted GitHub Actions, including the Linux/Node.js 24 job, has not run for these
changes. No firmware build, device test or release qualification was performed;
this change does not modify runtime behavior. Local completion does not claim
hosted CI acceptance or human review approval.
