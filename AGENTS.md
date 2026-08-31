# Meshbus SDK Entry

This is an independent Git repository for reusable services, public APIs,
drivers, protocols, UI components, samples, and tests. Product composition,
boot/release policy, host CLI, and distribution belong to the firmware repository.

In the Meshbus product workspace, the single governing Spec Kit project is in
the firmware root. Discover the workspace with `west topdir` and locate that
project's `.specify/memory/constitution.md`; read it and the applicable SDK and
verification standards before implementation or validation. New task documents
belong to its `specs/`, including SDK-only tasks. Do not initialize a second
Spec Kit project here or infer firmware/SDK Git ownership from directory nesting.

If the consuming Spec Kit project is unavailable, report the missing governance
context before implementation that relies on it; source inspection and explicitly
requested builds of existing standalone samples may still proceed from their
own documentation and metadata. Do not invent replacement project policy.

Nearest AGENTS files describe source ownership. Public headers, schemas,
Kconfig/CMake/DTS, test metadata, source, and tests supply technical facts.
Keep SDK changes separately scoped and preserve unrelated edits. No device,
remote, dependency, signing, staging, commit, or publication action is authorized
merely by reading this guide or selecting a feature.
