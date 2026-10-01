# Current knowledge and selective history

## Context

See proposal.md for motivation. The repository has one root agent entry,
six generated OpenSpec skills, seven ADRs and existing domain guides. Its only
capability spec covers development validation; it does not replace the ADRs.

## Goals / Non-Goals

Make current constraints discoverable through task-specific links, with one
owning guide per rule. Preserve the meaning of migrated contracts and recorded
evidence. This is a documentation migration, not a new memory system, agent
runtime, licensing decision or proof of product behavior.

## Decisions

1. Keep AGENTS.md as the small authorization and routing entry. Domain details
   remain in existing guides; a second knowledge index would duplicate them.
   Keep the vocabulary file available for ambiguous terms, not mandatory reading.
2. Merge ADR 0001/0007 into the product guide, 0002/0003 into distribution,
   0004 into MBA development, and 0012/0013 into licensing. Preserve rationale
   needed to understand constraints; historical numbering belongs in this
   migration record and Git, not placeholder documents.
3. Keep completed OpenSpec folders, as requested. Search by path, symbol or
   change identity before reading history, and recheck old findings against
   current source. A dedicated history skill would add metadata and maintenance
   without supplying a missing tool, so use a short development-guide procedure.
4. Engineering, test selection and workflow rules live in their owning guides.
   OpenSpec context routes to those guides; artifact and operation settings only
   add requirements specific to their own stage. Generated skills remain intact.
5. Routine findings stay in conversation; a formal change uses its one tasks.md
   for concise acceptance. Builds keep native output/cache locations. Temporary
   logs and intermediates use an external task directory, while deliverables
   have an explicit destination. Existing local files and historical paths stay.

## Risks / Trade-offs

- A lost qualification or trust qualifier could change policy: compare each
  migrated decision with its destination before removing the old file.
- Longer domain guides could shift rather than reduce reading: link to relevant
  sections, merge duplicates, and review seven task-routing scenarios.
- Old archive examples retain obsolete paths: treat them as historical evidence,
  not reusable current instructions; keep current examples independent of them.
- Static checks cannot establish agent performance: report bytes and routing
  review, not measured token savings or task-success improvements.

## Migration Plan

Capture the clean baseline and protected-file hashes outside the repository.
Move and reconcile current contracts, update inbound links and REUSE coverage,
then remove the ADR documents. Rewrite entry/configuration and current temporary
output examples. Validate links, policy preservation, protected files, static
task routes, OpenSpec and licensing before archiving this change. Do not archive
or edit another change. Rollback, if later requested, can restore these document
changes from the pre-change Git revision without touching local artifacts.
