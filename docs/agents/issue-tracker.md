# Issue tracker: Local Markdown

Issues and specs for this repo live as Markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`
- The spec is `.scratch/<feature-slug>/spec.md`
- Implementation issues are one file per ticket at
  `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`
- Record `Category:` and triage `Status:` near the top of each issue using
  `docs/agents/triage-labels.md`
- Track execution separately with `Progress: open | claimed | resolved`
- Record decisions and relevant evidence under a `## Comments` heading
- State the deliverable, checkable acceptance criteria, and out-of-scope work;
  include required execution evidence and authorization limits where applicable
- When a decision changes, identify the superseded clauses and link the current
  requirement from earlier task entrypoints; retain dated evidence as history
- Resolve only when required acceptance passes. Record blockers and unmet
  criteria separately from optional follow-ups; use the completion guidance in
  `DEVELOPMENT.md`

## Publishing

When a skill says to publish to the issue tracker, create the corresponding
file under `.scratch/<feature-slug>/` within the user's requested scope.
This local publishing convention does not authorize a Git commit or an
external publication.

## Fetching

When a skill says to fetch a ticket, read the referenced Markdown file. The
user normally supplies its path or issue number. Numbers are local to a feature
directory; use the active feature or full path to resolve them. Ask for the
feature only when the number remains ambiguous.

## Wayfinding

A wayfinding effort uses one map and one child file per ticket:

- Map: `.scratch/<effort>/map.md`
- Ticket: `.scratch/<effort>/issues/<NN>-<slug>.md`
- Type: record `research`, `prototype`, `grilling`, or `task` in a `Type:` line
- Progress: record `open`, `claimed`, or `resolved` in a `Progress:` line;
  retain `Status:` for triage
- Blocking: record dependencies in a `Blocked by: NN, NN` line
- Frontier: select the first numbered ticket with `Progress: open` whose
  blockers are all resolved
- Claim: set `Progress: claimed` before starting work
- Resolve: append the result under `## Answer`, set `Progress: resolved`, and add
  a result pointer to the map
