<!-- SPDX-FileCopyrightText: 2026 FoBE Studio -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tasks

## 1. Preserve current contracts

- [x] 1.1 Merge all seven ADRs into owning guides; review every policy against its original and record destination sections below before removing the ADR tree.
- [x] 1.2 Update inbound links and REUSE coverage; check affected document targets and confirm no live guidance depends on removed ADR paths.

## 2. Rewrite instructions and output conventions

- [x] 2.1 Rewrite the root entry and OpenSpec configuration, consolidate engineering/test/workflow ownership, and review all seven task-routing scenarios below.
- [x] 2.2 Add selective history retrieval and temporary-output rules, update all live examples, and inspect the scoped search results and shell syntax for stale defaults.

## 3. Integrated acceptance

- [x] 3.1 Verify protected-file hashes and local temporary-tree metadata against the clean baseline; report entry sizes and unchanged dependencies, skills and histories.
- [x] 3.2 Pass OpenSpec current/archive validation, affected Markdown link/anchor checks, development-environment license policy and git diff --check; record actual results and limitations before archive.

## Migration map

| Original decision | Destination | Contract review |
| --- | --- | --- |
| 0001 | apps/meshbus/README.md: Device firmware and MeshCore role; Product policy and release qualification | Reviewed identity, synchronous role application, deferred durability, rollback attempt, static RAM and target qualification. |
| 0002 | DISTRIBUTION.md: introduction | Reviewed independent cycles, compatible CLI records, assembly executable versus platform publication. |
| 0003 | DISTRIBUTION.md: MCUboot products; Firmware archive contents; DFOTA | Reviewed hash-only defaults, caller-owned/shared keys, native signing trust, primary-slot checks, independent offline manifest key and exact baselines. |
| 0004 | scripts/meshbus/APP_DEVELOPMENT.md: Execution and trust model; device Session section | Reviewed independent foreground apps, no sandbox, transport authentication, integrity/provenance and resource cleanup. |
| 0007 | apps/meshbus/README.md: Mesh Probe R2 owner control and recovery | Reviewed SWD ownership, UART-only boot recovery, destructive writes, no monotonic rollback counter, trust replacement and independent DFOTA checks. |
| 0012 | LICENSING.md: Compiled dependency admission | Preserved all six admission clauses, exact-input checks, uncertain provenance, font and redistribution boundaries. |
| 0013 | LICENSING.md: introduction; Source, EDK and firmware distributions | Reviewed owned Apache-2.0 scope, third-party attribution, metadata/generator consistency, EDK materials and authorization. |

## Static routing review

| Scenario | Expected route | Result |
| --- | --- | --- |
| Documentation typo | Local document only; no unrelated policy/history | Pass: task-only entry, no change needed, document checks in testing guide. |
| Single service change | Relevant source/test metadata and focused testing guidance | Pass: development setup links to metadata-based coverage and bounded implementation loop. |
| Image authentication | Current distribution trust rules; no implied signing/device authority | Pass: distribution route and root authorization remain explicit. |
| MBA review | Execution model, transport authentication and lifecycle contract | Pass: direct entry to trust model, then metadata and Session cleanup links. |
| Design history | Specific Git/OpenSpec record, checked against current state | Pass: history procedure locates summaries first and requires identity/current-state checks. |
| Routine research | Conversation result; no independent report or memory | Pass: root default and Outputs and records agree; external output only when needed. |
| Missing acceptance | Explicit gap; incomplete task remains open | Pass: OpenSpec completion rules and apply/archive guidance preserve the gap. |

## Validation

Baseline: clean worktree at `d63c9005b7f6d4d28c14d3fde77a0f509d0f299c`.
AGENTS.md: 2,068 bytes; openspec/config.yaml: 3,012 bytes.
Protected baseline covers 22 tracked files and metadata for 9 existing local
temporary-tree entries. Raw checks stay in an external task directory.

This documentation-only change uses content, link, schema and attribution checks
instead of runtime TDD. Static routing is not an agent execution benchmark;
no firmware, hardware, hosted-CI or token-efficiency claim is made.

2026-09-30 pre-archive results:

- `npm run spec:check`: 3 items passed, 0 failed (this change,
  development-validation, simplify-repository).
- `npm run openspec -- validate --archived --strict --no-interactive`:
  existing adopt-openspec archive passed.
- Local link/anchor review: 17 changed/new Markdown files, 102 local links,
  42 Markdown fragments; no missing destinations. Six changed shell blocks
  passed `zsh -n`; device/build example commands were not executed.
- All 22 protected-file SHA-256 values match: six generated skills, their
  supporting tracked metadata/license, package files, prior changes and spec.
  All 9 existing local temporary-tree entries retain path/type/size/mtime
  metadata. The Git index remains empty; no commits or publication performed.
- No current guidance references the removed ADR tree or defaults to the old
  scratch directory; its ignore rule and historical records remain intact.
- License policy passed in the existing Zephyr Python environment, with 0
  remaining metadata findings. Raw REUSE remains noncompliant: 29 missing
  copyright entries, 28 missing license entries and one unused license text
  are covered by existing policy exceptions; no exceptions were added or rebased.
- `git diff --check` passed. Seven static routes above were reviewed against
  the final guidance; these are document inspections, not agent benchmark runs.

| Entry | Before (bytes) | After (bytes) |
| --- | ---: | ---: |
| AGENTS.md | 2,068 | 2,288 |
| openspec/config.yaml | 3,012 | 1,878 |
| Combined files | 5,080 | 4,166 |

The root entry grows by 220 bytes to route more precisely; the combined files
shrink by 914 bytes (18.0%). File bytes are not a measurement of loaded tokens
or agent performance. Existing product contracts were preserved through document
comparison; firmware implementation and hardware qualification were not re-audited.
