# Meshbus

This repository owns the reusable Zephyr module, product firmware in
`apps/meshbus/`, and host tools in `scripts/`. The parent is a local west
workspace; sibling projects retain their own ownership and Git history.

Use the standard OpenSpec workflow in [openspec/README.md](openspec/README.md).
Project context and artifact rules live in `openspec/config.yaml`; Codex
workflows are generated in `.agents/skills/`. Run the pinned CLI from the Git
root as `npm run openspec -- <arguments>`. Keep generated skills unchanged;
customize project context through OpenSpec configuration.

Read documentation according to the work:

- [README.md](README.md): module integration and public namespaces.
- [DEVELOPMENT.md](DEVELOPMENT.md): engineering contracts, workspace, builds and tests.
- [docs/testing.md](docs/testing.md): test boundaries, tool selection and evidence.
- [DISTRIBUTION.md](DISTRIBUTION.md): packaging, signing and release qualification.
- [CONTEXT.md](CONTEXT.md) and [ADRs](docs/adr/README.md): vocabulary and design decisions.
- [LICENSING.md](LICENSING.md): attribution and dependency license requirements.

Preserve unrelated work. Continue authorized local implementation and validation
without repeated approval. Dependency/manifest changes, remote or device access,
signing and publication require explicit authorization; stage, commit and push
only when requested. An OpenSpec task or generated skill does not grant it.
Review-only requests remain read-only. Keep secrets out of tracked artifacts.

Keep shared requirements and change artifacts in `openspec/`; keep temporary
reports, experiments and raw logs in ignored `.scratch/<task>/`. Specs describe
intended behavior; source and test results establish implemented behavior.
Surface discrepancies and update the agreed change rather than weakening its
acceptance criteria. Select the smallest relevant checks and report their actual
results. For repository changes, run the license policy in the development
Python environment; fix in-scope findings and report unrelated blockers.
