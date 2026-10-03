# Spec Delta

## Purpose

Provide an English and Simplified Chinese website at meshbus.org where prospective customers can
evaluate Meshbus as a mesh application platform and developers can find current
source-backed guidance through an accessible, searchable static site.

## Verification scope

On 2026-10-02 the owner explicitly waived browser verification of reduced-motion
and JavaScript-disabled behavior for this change. The corresponding behavior
requirements and scenarios below remain in force; the waiver is not evidence
that those checks passed. All other local and live acceptance remains required.

## ADDED Requirements

### Requirement: Present Meshbus as a platform for mesh applications

The homepage in each language SHALL serve prospective customers by presenting Meshbus
as a foundation for mesh network applications. It SHALL explain broader hardware
choice through Zephyr, compatible native application loading through LLEXT
without reflashing base firmware, and Meshbus's integration of mesh communication,
device services and tools. The site SHALL provide Platform, Hardware and
Applications pages at `/platform/`, `/hardware/` and `/applications/`, with
working links to `/docs/`, getting started and the source repository.

#### Scenario: Customer evaluates the platform
- **WHEN** a prospective customer opens the homepage
- **THEN** its main content explains the hardware and application benefits and
  Meshbus's own integration value
- **AND** its primary action opens the platform overview, with hardware,
  application and documentation paths reachable from the shared navigation

#### Scenario: Customer investigates hardware choice
- **WHEN** a visitor opens the Hardware page
- **THEN** it explains reuse of Zephyr board support, drivers and APIs, and the
  need for Meshbus target integration and validation
- **AND** upstream board counts or illustrations are not presented as a Meshbus
  compatibility or qualification guarantee

#### Scenario: Customer investigates application loading
- **WHEN** a visitor opens the Applications page
- **THEN** it explains runtime loading of compatible MBA applications on
  supported profiles, with a target-firmware EDK and one foreground application
- **AND** it does not promise universal binary portability, uninterrupted
  stateful replacement, automatic mesh-wide deployment or resident service loading

#### Scenario: Customer reads application examples
- **WHEN** a product page presents an illustrative application scenario
- **THEN** it identifies the scenario as an idea unless actual deployment
  evidence supports a case-study claim

#### Scenario: Visitor reads product and MBA descriptions
- **WHEN** a visitor reads about firmware roles or MBA applications
- **THEN** the descriptions preserve the separation of firmware identity from
  MeshCore role and the owner-trusted native execution model of MBA applications
- **AND** the website does not imply universal board qualification or application
  isolation that the current project does not provide

### Requirement: Preserve the approved pixel visual identity

The website SHALL use the supplied stacked MESH/BUS pixel logo and a coherent
mint, teal, forest and warm-paper visual identity. Product pages SHALL express
hardware diversity and application loading through pixel illustrations, squared
controls and hard offset shadows. Pixel typography SHALL be confined to short
headings and labels; body text and code SHALL remain readable. Text, links and
controls SHALL be real web elements rather than embedded in a full-page prototype
image. Documentation SHALL share the brand while keeping decoration secondary
to reading.

#### Scenario: Visitor moves from product to documentation
- **WHEN** a visitor follows a product-page link into the documentation
- **THEN** the logo and theme remain consistent while the document has readable
  body text, code and navigation

#### Scenario: Reviewer inspects the implemented design
- **WHEN** the homepage is compared with the accepted pixel concept
- **THEN** it retains the stacked logo, mint/teal palette, pixel hardware and
  application imagery, square controls and hard shadows
- **AND** its headings and paragraphs are selectable text and its actions are
  independently focusable links or controls

### Requirement: Provide a complete bilingual developer documentation entry

The documentation SHALL expose readable English and Simplified Chinese guidance for workspace setup
and SDK integration, product firmware and target selection, CLI usage, MBA/EDK
development, engineering and testing, distribution, contributions and licensing.
It SHALL provide an orientation page and sample navigation. Public header and
MBA metadata references SHALL lead to their current owning documents or source.
RST sample guides and independently maintained modules SHALL be identified as
source or external references when they are not rendered in the website.

#### Scenario: SDK developer begins integration
- **WHEN** a developer follows Getting Started and the SDK documentation
- **THEN** workspace setup, module integration, dependency ownership and relevant
  sample sources are reachable through working links

#### Scenario: Developer follows a product or host workflow
- **WHEN** a developer selects Firmware, CLI or MBA documentation
- **THEN** the selected guide is readable within the website and retains its
  documented commands, prerequisites and capability limitations

#### Scenario: Reader follows a sample reference
- **WHEN** a reader selects an RST sample or an independent module reference
- **THEN** the link opens the identified owning source or project rather than
  presenting an empty documentation page or unconverted RST as HTML

### Requirement: Keep published guidance tied to its owning source

Website builds SHALL consume the selected technical guides from the same
repository checkout as the website. Maintainers SHALL update each imported guide
at its owning path without editing a second English website copy. Published
guides SHALL expose their source path and a working source/edit link. Missing
required sources or duplicate published routes SHALL fail the website build.

#### Scenario: Maintainer updates an imported guide
- **WHEN** a selected owning guide changes and the website is built again
- **THEN** its English page reflects that change and publication requires the
  Chinese translation to be reviewed against the updated source

#### Scenario: Required guide cannot be published
- **WHEN** a required source is missing or two inputs claim the same page route
- **THEN** validation fails with the affected input and route identified
- **AND** stale generated content is not accepted as a successful build

### Requirement: Preserve document links, assets and code examples

Published documentation SHALL preserve headings, tables, code examples and
referenced assets. Links between published guides SHALL resolve to website
pages with valid section anchors. Links to repository material outside the
published set SHALL resolve to the identified repository source rather than a
nonexistent local web route. Missing local targets, assets or published section
anchors SHALL fail website validation. Code-copy controls SHALL copy the example
text without website decorations or injected line numbers.

#### Scenario: Reader follows an imported relative link
- **WHEN** an imported guide links to another published guide and a section
- **THEN** the web link opens the corresponding rendered page at that section

#### Scenario: Reader follows a source-only reference
- **WHEN** an imported guide links to an existing header, configuration file,
  RST guide or other repository file outside the published set
- **THEN** the website presents a working source link for that file

#### Scenario: Broken reference reaches validation
- **WHEN** a published page contains an unresolved local link, asset or section
- **THEN** the website check fails and identifies the referring page and target

#### Scenario: Reader copies a command
- **WHEN** a reader copies a displayed command example
- **THEN** the clipboard contains the displayed command text with its intended
  whitespace and no UI labels or line numbers

### Requirement: Make documentation navigable and searchable

Documentation pages SHALL provide grouped navigation, a current-page indicator
and in-page section navigation where headings exist. The production website
SHALL offer full-text search over published product pages in the current language and
documentation using assets
delivered with the site, without a hosted search account or API key. Search SHALL
provide working result links and a clear empty-result state. Core page reading
and navigation on product and documentation pages SHALL remain available when
JavaScript is disabled.

#### Scenario: Customer searches for a platform benefit
- **WHEN** a reader searches the production website for Zephyr or LLEXT
- **THEN** relevant product and technical documentation results lead to working
  pages or sections

#### Scenario: Reader searches across developer areas
- **WHEN** a reader searches the built website for documented SDK, CLI or MBA
  terms such as `CONFIG_MBS`, `meshbus connect` or `exported-symbols`
- **THEN** relevant published documentation appears and its result links open
  the matching page or section

#### Scenario: Search has no matching content
- **WHEN** the query has no matching published documentation
- **THEN** the search UI displays an explicit no-results state

#### Scenario: Reader disables JavaScript
- **WHEN** the reader opens a documentation page with JavaScript disabled
- **THEN** its content, document links and main navigation remain readable and
  usable without requiring search or theme controls

### Requirement: Support mobile, keyboard and theme use

The homepage and documentation SHALL remain readable at viewport widths of
375, 768 and 1440 CSS pixels without page-wide horizontal overflow; wide code
blocks or tables SHALL scroll within their containers. Navigation, search,
theme selection and code-copy controls SHALL expose accessible names, visible
keyboard focus and usable keyboard interactions. The site SHALL provide light
and dark appearances, honor system preference initially and retain a visitor's
explicit theme selection. Essential text and controls SHALL meet WCAG AA color
contrast; decorative movement SHALL respect reduced-motion preference.

#### Scenario: Reader navigates on a narrow viewport
- **WHEN** the reader opens the homepage or a long guide at 375 CSS pixels wide
- **THEN** the page remains readable and documentation navigation is usable
  without scrolling the entire page horizontally

#### Scenario: Reader uses the keyboard
- **WHEN** the reader tabs through navigation and opens and closes search
- **THEN** focus remains visible, controls have meaningful accessible names,
  Escape closes search and focus returns to its invoking control

#### Scenario: Reader selects a theme
- **WHEN** the reader chooses light or dark appearance and reloads the website
- **THEN** the explicit choice is retained and content remains readable

### Requirement: Deliver static output for the canonical domain

Maintainers SHALL be able to install locked website dependencies and run
documented development, check, build and production-preview commands from the
repository using `web/` as the website project. Production output SHALL consist
of static pages and local assets, including the search index, without a runtime
application server. Production routes, assets and search SHALL work at the root
of `https://meshbus.org/` without a repository-name prefix. Every page SHALL have
a matching language declaration, a meaningful title and description. Content
page canonical URLs and the sitemap SHALL use the corresponding paths under
`https://meshbus.org/`. The output SHALL include a not-found page with recovery
links that remain valid when served for nested missing URLs.

#### Scenario: Maintainer previews the production output
- **WHEN** a maintainer runs the documented build and preview commands
- **THEN** the homepage, documentation deep links, assets and search work from
  the generated static output

#### Scenario: Visitor follows a custom-domain deep link
- **WHEN** a visitor opens or reloads a product or documentation URL directly
  under `https://meshbus.org/`
- **THEN** the expected page, assets, navigation and search work without a
  repository-name prefix or client-side route recovery

#### Scenario: Maintainer inspects public metadata
- **WHEN** the production output is checked
- **THEN** content canonical URLs and sitemap entries use meshbus.org with the
  correct route and contain no localhost or repository-prefix deployment URLs

#### Scenario: Visitor encounters the bundled not-found page
- **WHEN** a static host serves the website's not-found page for an unknown URL
- **THEN** the visitor receives a clear recovery link to the project or docs

### Requirement: Publish checked builds through GitHub Pages

The site SHALL be published through GitHub Actions to GitHub Pages at
`https://meshbus.org/`. Pull requests SHALL validate without publishing. Main
pushes affecting website inputs and manual runs on main SHALL deploy only the
artifact produced by successful website checks for that run. Failed checks or
runs on other refs SHALL NOT replace the published site. Deployment records
SHALL identify the source revision and published URL. The public site SHALL
enforce HTTPS and redirect `www.meshbus.org` to the canonical apex origin.

#### Scenario: Pull request validates the website
- **WHEN** a pull request changes a website input
- **THEN** website checks run without publishing or granting Pages write access

#### Scenario: A main build passes
- **WHEN** an eligible main-branch run passes all required website checks
- **THEN** its checked static artifact is deployed and the source revision,
  workflow result and publication URL are available to the maintainer

#### Scenario: Validation fails or another ref is selected
- **WHEN** website validation fails or a manual run targets a ref other than main
- **THEN** that run does not deploy to the production site

#### Scenario: Visitor uses an alternate origin
- **WHEN** a visitor opens HTTP or the www variant of a valid public page
- **THEN** the visitor reaches the corresponding HTTPS meshbus.org route

#### Scenario: Live acceptance is incomplete
- **WHEN** DNS, certificates, deployment access or live-site checks remain pending
- **THEN** the acceptance record identifies the unmet condition and leaves
  deployment acceptance incomplete, even if local build checks pass

### Requirement: Verify affected website inputs

Repository validation SHALL check the website when web sources, its declared
content inputs or its reused brand inputs change. Website checks SHALL detect
missing required pages, broken generated links and anchors, missing assets and
an absent production search index. Website-only changes SHALL receive source
and website validation without selecting unrelated firmware or CLI builds.
Mixed changes SHALL retain the union of affected validation domains.

#### Scenario: Imported documentation changes
- **WHEN** an imported technical guide changes without a web source edit
- **THEN** CI selects the website build and rendered-output checks

#### Scenario: Only website source changes
- **WHEN** changes are limited to website sources and its dedicated workflow
- **THEN** source and website checks run and firmware or CLI builds are not
  selected solely because of those paths

#### Scenario: Website and firmware change together
- **WHEN** a change includes website files and mapped firmware source
- **THEN** website checks and the existing affected firmware coverage are both
  retained

### Requirement: Provide complete English and Chinese editions

The site SHALL publish English at existing root routes and Simplified Chinese
at corresponding `/zh-cn/` routes, covering all product pages, navigation,
orientation pages and every selected technical guide. The language control
SHALL list supported languages in a dropdown and link to the corresponding page. Each edition SHALL use localized UI,
metadata and language-scoped search. Pages SHALL expose matching language tags,
canonical URLs and reciprocal language alternatives. The root not-found page
SHALL offer recovery in both languages.

#### Scenario: Reader changes language
- **WHEN** a reader selects another language on a product or documentation page
- **THEN** the corresponding translated page opens with localized navigation
- **AND** documented section anchors remain valid in both editions

#### Scenario: Reader searches Chinese documentation
- **WHEN** a reader searches the Chinese edition for a Chinese phrase or code identifier
- **THEN** matching Chinese product and documentation pages appear with working links
- **AND** English search remains scoped to English pages

#### Scenario: A translation is missing or outdated
- **WHEN** a required translation is absent or its source digest no longer matches
- **THEN** preparation fails with the affected source identified
- **AND** no silent English fallback is accepted as translated documentation

#### Scenario: Developer uses translated examples
- **WHEN** a technical guide is translated
- **THEN** code examples, inline identifiers and link destinations retain their source contracts
- **AND** the guide identifies its English source and editable translation catalog


### Requirement: Keep shared navigation stable across page types

Product and documentation pages SHALL use the same fixed site header, control
sizes, font metrics and responsive breakpoints. Search and theme initialization
SHALL reserve their final layout space. Mobile documentation navigation SHALL
be distinguishable from the website menu, and section targets SHALL remain
visible below fixed navigation.

#### Scenario: Visitor enters documentation
- **WHEN** a visitor moves between product and documentation pages at the same viewport
- **THEN** the logo, search, language and theme controls retain positions and sizes within 1 CSS pixel
- **AND** the header remains accessible while scrolling

#### Scenario: Reader navigates a phone-sized guide
- **WHEN** a reader opens the site menu, document directory or in-page contents
- **THEN** each control has a distinct purpose and a usable keyboard path
- **AND** closing a menu or search restores appropriate focus

### Requirement: Enhance product explanations with native scroll storytelling

Product storytelling SHALL follow native page scrolling, keep text available
before scripts initialize, and retain the approved pixel identity. Rich sticky
scenes SHALL apply only at widths >=1100 px and heights >=700 px; narrower or
shorter viewports and reduced-motion preference SHALL use normal document flow.
Platform layers SHALL also support explicit keyboard and pointer selection.
Documentation SHALL remain a stable reading surface without scroll storytelling.

#### Scenario: Visitor reads a product sequence
- **WHEN** a visitor scrolls the hardware/services/application or application workflow sections
- **THEN** the relevant visual stage is emphasized without intercepting page scrolling
- **AND** inactive scenes stop updating

#### Scenario: Visitor resizes a product page
- **WHEN** the viewport crosses the sticky-scene thresholds
- **THEN** all steps remain visible and reachable without clipped text or stranded fixed content

#### Scenario: Visitor explores a continuous scene
- **WHEN** a visitor scrolls forward or backward between product stages
- **THEN** the same platform layers, devices or application package change continuously with scroll position and return to their earlier state on reverse scrolling
- **AND** layer selection and the current explanation remain synchronized
- **AND** smaller screens show a complete static overview alongside all normal-flow steps
