# Development validation delta

## MODIFIED Requirements

### Requirement: Select checks by their affected domain

Daily CI SHALL select the declared tests and integration consumers of changed
components without recursively treating each tested consumer as changed source.
Mixed changes SHALL union their scopes. Unmapped firmware and shared firmware
configuration SHALL retain broad SDK and product coverage unless a reviewed board or product ownership rule resolves every affected consumer.

#### Scenario: Local service implementation change
- **WHEN** a mapped service implementation changes
- **THEN** CI selects its declared test/sample roots and product builds
- **AND** unrelated host CLI packages are not selected

#### Scenario: License or CI tooling change
- **WHEN** only license metadata not consumed by runtime code or tests, or ordinary CI
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

#### Scenario: Scoped CI infrastructure changes
- **WHEN** CLI cache or release orchestration changes without shared execution changes
- **THEN** CI selects its CLI/release consumers and host regressions
- **AND** execution planners and shared workspace changes still receive complete integration validation

#### Scenario: Board and product configuration
- **WHEN** board or product inputs have known owners and include consumers
- **THEN** CI builds the affected products and selects affected board configurations
- **AND** unresolved ownership expands coverage rather than excluding possible consumers

## ADDED Requirements

### Requirement: Prepare only required CLI sources
CLI packaging SHALL accept a verified source snapshot with a pinned schema
checkout without restoring unrelated firmware dependencies. Source conflicts,
modified schema inputs and uncommitted candidate inputs SHALL fail.

#### Scenario: Minimal CLI workspace
- **WHEN** a CLI job receives the source snapshot and matching schemas
- **THEN** it builds and packages the target without fetching the full west graph
- **AND** its provenance identifies the inherited graph and actual build inputs

### Requirement: Match build effort to validation scope
Daily CLI checks SHALL use a faster optimized profile. Complete validation and
candidates SHALL retain the release profile. Cross-built packages SHALL retain
native target execution checks. Twister partitioning SHALL scale with selected
work without dropping, duplicating or converting required runtime coverage.

#### Scenario: Daily and full builds
- **WHEN** ordinary CLI changes select representative platforms
- **THEN** CI avoids release LTO while still building and executing packages
- **AND** full validation retains all six release targets

#### Scenario: Small Twister selection
- **WHEN** a small set of instances is selected
- **THEN** fewer workers prepare environments while all selected instances remain accounted for
