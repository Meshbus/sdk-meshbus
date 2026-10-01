# Development validation

## Purpose

Provide contributors with focused regression feedback while retaining explicit
complete validation and trustworthy accounting of the selected checks.

## Requirements

### Requirement: Select checks by their affected domain

Daily CI SHALL select the declared tests and integration consumers of changed
components without recursively treating each tested consumer as changed source.
Mixed changes SHALL union their scopes. Unmapped firmware and shared firmware
configuration SHALL retain broad SDK and product coverage.

#### Scenario: Local service implementation change
- **WHEN** a mapped service implementation changes
- **THEN** CI selects its declared test/sample roots and product builds
- **AND** unrelated host CLI packages are not selected

#### Scenario: License or CI tooling change
- **WHEN** only license metadata not consumed by runtime code or tests, or CI
  Python tools, changes
- **THEN** source checks run and CI Python tools receive host regression checks
- **AND** firmware and CLI builds are not selected solely by those paths

#### Scenario: License text consumed by the CLI
- **WHEN** a license text compiled into the CLI or consumed by its tests changes
- **THEN** CI selects CLI and Rust validation
- **AND** unrelated firmware builds are not selected solely by that path

#### Scenario: Power feedback integration
- **WHEN** the Power implementation or public header changes
- **THEN** CI includes Indicator audio and feedback runtime scenarios that
  consume Power events

#### Scenario: Uncertain impact
- **WHEN** a changed path is outside known repository domains or the comparison
  revision cannot be resolved
- **THEN** CI selects complete validation rather than silently omitting work

### Requirement: Distinguish routine and extended scenarios

Daily indirect test selection SHALL exclude shuffle and performance scenarios.
Full validation SHALL include them. A directly changed extended test application
SHALL remain selected in daily CI. Shared settings changes SHALL include settings
performance checks. Filtering SHALL occur before freezing the expected inventory.

#### Scenario: Routine component change
- **WHEN** a component selects test roots containing extended scenarios
- **THEN** ordinary scenarios remain and unrelated shuffle/performance variants
  do not run

#### Scenario: Extended coverage is relevant
- **WHEN** validation is full or an extended test application changes directly
- **THEN** the applicable extended scenarios are included

### Requirement: Preserve strict result accounting

Selected executable instances SHALL produce passing runtime evidence. Compilation
coverage SHALL exclude exact instances already executed. Missing, duplicate,
failed or unexpectedly skipped selected instances SHALL fail validation.

#### Scenario: Missing selected report
- **WHEN** a selected instance has no successful report
- **THEN** validation fails even if every received report passed

#### Scenario: Same configuration under different names
- **WHEN** two repository scenarios have identical source, configuration and cases
- **THEN** one scenario provides the coverage rather than repeating the build
