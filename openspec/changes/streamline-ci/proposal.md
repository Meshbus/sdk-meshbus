# Proposal

## Why

CI run 36749625503 spent 62% of cumulative Twister job time preparing its
environment. CLI jobs restore the complete west graph and Intel macOS extends
the critical path even though Linux/Windows already use cross compilation.

## What Changes

- Prepare CLI packages from verified source records and only the pinned schema
  checkout; make hosted disk cleanup conditional on required free space.
- Use a faster daily Rust profile, preserve release candidates, and cross-build
  Intel macOS on Apple Silicon while retaining native target validation/tests.
- Size Twister shards by selected work and classify CI changes by their consumer.
- Select board/product coverage from reviewed ownership and include relationships,
  retaining conservative fallbacks and complete scheduled/manual validation.
- Complete local checks before one combined commit/push, then inspect hosted CI.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `development-validation`: scoped preparation, profiles, adaptive shards and
  board/product selection with strict result accounting.

## Impact

CI workflows/actions, planning and workspace helpers, CLI packaging/profile,
CI/release tests and CI documentation. No dependency revision changes, device
operations or release publication. Preserve unrelated Alpha acceptance edits.
