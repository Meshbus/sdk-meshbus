# Adopt OpenSpec

## Why

Contributors need a shared, versioned way to describe requirements, review
changes and track implementation. Meshbus is adopting the standard OpenSpec
workflow instead of maintaining a separate repository-specific agent process.

## What Changes

- Add the pinned OpenSpec CLI, upstream `spec-driven` configuration and Codex
  core skills, with their original license and reproducible setup.
- Replace layered agent instructions and local tracker/triage guidance with a
  short root entry and the standard OpenSpec change lifecycle.
- Keep public API, runtime and test contracts in ordinary engineering documents.
- Configure behavior-oriented TDD guidance and separate device/release evidence.
- Validate OpenSpec in Source checks and keep planning-only changes out of
  firmware/CLI build selection.

## Capabilities

### New Capabilities

None. This change introduces development tooling, not product behavior.

### Modified Capabilities

None. Change metadata sets `skip_specs: true`; no firmware requirement is inferred
from existing code or historical task notes.

## Impact

Affected areas are root tooling, `.agents/`, `openspec/`, engineering documentation,
license attribution and source CI. OpenSpec is a host development dependency only.
Firmware, public interfaces, west dependencies, device operations and release
artifacts are outside this change.
