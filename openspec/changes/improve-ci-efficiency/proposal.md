# Proposal

## Why

Daily changes still select unrelated firmware tests, while Candidate and tag
workflows repeat builds whose exact inputs have already passed. Preparation and
licensing scope also create avoidable work during ordinary contributions.

## What Changes

- Select product profiles and reviewed component consumers explicitly.
- Narrow licensing context expansion and condition OpenSpec preparation.
- Share small Twister workspaces and balance shards using measured durations.
- Reuse complete default-product evidence and qualified Candidate artifacts.
- Compare schedulers in a separate benchmark and exercise staging without
  publishing a release.

## Capabilities

### New Capabilities

- None. Alpha publication already has an active change; this change revises its
  qualification contract without introducing a duplicate capability.

### Modified Capabilities

- `development-validation`: affected profiles, license scope, source tooling,
  measured scheduling and complete result accounting.
- `alpha-release-publication`: extend the active ci-alpha-release requirements
  to reuse default builds and qualified Candidate artifacts.

## Impact

CI workflows, Python selection/execution/qualification tools and their tests and
owner documentation. No dependency version, firmware behavior or public asset
format changes. Existing website work remains outside these commits. Hosted
acceptance includes Candidate and staging, with no tag or Release creation.
