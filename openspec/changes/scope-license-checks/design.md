# Design

## Context

The checker currently runs `reuse lint --json` over the working directory.
REUSE 6.2.0 can miss Git ignored paths beneath an entirely untracked directory.
The existing evaluator already preserves raw findings and applies explicit
metadata exceptions. See proposal.md for the agreed scope.

## Goals / Non-Goals

Select deterministic scan inputs without changing the developer's index or
installing dependencies. Keep current SPDX attribution and package checks.

## Decisions

- Enumerate tracked and non-ignored untracked files with Git's NUL-delimited
  file lists. Compare the current worktree with HEAD locally or an explicit
  CI base. An unavailable base expands to a complete audit.
- Export selected current file bytes and their licensing context to a temporary
  directory, then run the existing REUSE CLI there. This retains the scanner's
  report format and avoids depending on its faulty Git directory optimization
  or private Python APIs. Preserve relative paths and remove the temporary
  directory automatically.
- Include standard license texts, global attribution configuration and sidecar
  metadata as context. Shared licensing changes (including sidecars) and checker
  changes expand to a complete audit. REUSE retains its standard treatment of
  symlinks, empty files and license metadata files.
- Exclude root `web/` before exporting any scan inputs, including context, in
  every mode. Keep existing website notices and metadata in the actual checkout.
- Treat unused-license-text findings as advisory only in incremental scans:
  a subset cannot prove repository-wide non-use. All other selected findings
  retain existing policy. Complete audits retain the named distribution-text
  exception and existing failure behavior.
- Source CI passes its comparison base; scheduled/manual complete validation
  requests a complete audit. Reports record mode, reason and selected paths.

## Risks / Trade-offs

- Temporary exports add local file copying -> copy only Git-selected inputs and
  licensing context; exclude installed dependencies and output directories.
- Incorrect selection could conceal failures -> use real temporary Git/REUSE
  integration tests for untracked directories, renames, global changes, invalid
  licenses, removed texts and website exclusion.
- Website SPDX coverage is absent -> state the temporary exclusion explicitly;
  retain attribution files and contribution review.
