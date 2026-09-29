# Meshbus

This repository owns the reusable Zephyr module, product firmware in
`apps/meshbus/`, and host tools in `scripts/`. The parent is a local west
workspace; sibling projects retain their own ownership and Git history.

Read only the sections needed for the task, alongside relevant source and tests:

- Integration or naming: [SDK guide](README.md#zephyr-integration).
- Engineering, environment or builds: [development](DEVELOPMENT.md).
- Test selection or evidence: [testing](docs/testing.md).
- Product roles or recovery: [product guide](apps/meshbus/README.md).
- Packaging, signing or release: [distribution](DISTRIBUTION.md).
- MBA execution: [trust model](scripts/meshbus/APP_DEVELOPMENT.md#execution-and-trust-model).
- Dependency admission or attribution: [licensing](LICENSING.md).

Use [OpenSpec](openspec/README.md) for shared requirements and planned changes;
small corrections need no new change. Read the relevant active change, not all
changes. Run `npm run openspec -- <arguments>` from the Git root. Keep generated
skills unchanged; project customization belongs in `openspec/config.yaml`.

For design reasons, regressions or earlier work, use
[targeted history retrieval](DEVELOPMENT.md#history-retrieval). Ordinary searches
exclude archives, build outputs and temporary records; expand when evidence or
an explicit audit calls for them. History and specs do not prove current behavior.

Preserve unrelated work. Continue authorized local implementation and validation
without repeated approval. Dependency/manifest changes, remote or device access,
signing, publication and history rewriting require explicit authorization;
stage, commit and push only when requested. An OpenSpec task or generated skill
does not grant it.
Review-only requests remain read-only. Keep secrets out of tracked artifacts.

Report routine findings in conversation. Keep formal acceptance in the change's
existing task record; follow [output handling](DEVELOPMENT.md#outputs-and-records)
when files are needed. Report actual checks and unmet acceptance without weakening
the agreed criteria. For repository changes, run the license policy in the
development Python environment; fix in-scope findings and report unrelated blockers.
