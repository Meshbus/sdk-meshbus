# Domain Docs

How engineering skills consume this repository's domain documentation.

## When domain context is needed

For work involving product terminology or architectural decisions, read the
relevant parts of root `CONTEXT.md` and the ADRs affecting the target area.
Reuse content already read unless it or the task scope changes.

Proceed silently when either location does not exist. Create domain documents
lazily through domain-modeling when terminology or architectural decisions
need to be recorded.

## Layout

This repository uses a single domain context:

```text
/
├── CONTEXT.md
└── docs/
    └── adr/
```

Paths above are relative to the Meshbus source repository. Product firmware
and reusable module code share this domain context and root agent guidance.

## Vocabulary

Use terms as defined in `CONTEXT.md` in issue titles, specifications, tests,
and implementation notes. Reconsider unknown synonyms or record a real
vocabulary gap through domain-modeling.

## ADR conflicts

Surface any conflict with an existing ADR explicitly. Do not silently replace
an accepted decision.
