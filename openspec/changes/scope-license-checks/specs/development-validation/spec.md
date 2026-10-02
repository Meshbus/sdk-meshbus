# Spec Delta

## ADDED Requirements

### Requirement: Scope license checks to relevant repository inputs

Routine license validation SHALL inspect changed repository files, including
staged, unstaged and non-ignored untracked files during local development.
Deleted files SHALL be omitted. Ignored untracked dependencies and build outputs
SHALL NOT enter the scan. Selected source metadata and license failures SHALL
remain blocking under the existing repository exemptions.

#### Scenario: Routine local or PR change
- **WHEN** a contributor runs the default local check or CI checks a commit range
- **THEN** only changed existing files and required licensing context are scanned
- **AND** unrelated unchanged source metadata gaps do not block the change

#### Scenario: Complete audit or shared licensing change
- **WHEN** complete validation is requested, the comparison base is unavailable,
  or shared attribution, license texts or license checker rules change
- **THEN** all eligible repository files are audited
- **AND** the report states the selected scope and reason

#### Scenario: License text is unused within a subset
- **WHEN** an incremental scan does not include users of an existing license text
- **THEN** that unused-text finding remains advisory
- **AND** missing texts, invalid license expressions and scanner errors still fail

### Requirement: Temporarily exclude website SPDX inputs

Repository SPDX checks SHALL exclude the complete root `web/` tree in routine
and complete modes, including source, configuration, assets and installed or
generated files. Existing notices SHALL be preserved. Actual release material
checks SHALL retain their existing contracts.

#### Scenario: Website files contain SPDX findings
- **WHEN** files under `web/` have missing or invalid SPDX data
- **THEN** repository SPDX scans do not inspect those files or report their findings
- **AND** files outside `web/` continue to follow the selected scope
