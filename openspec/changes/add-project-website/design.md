# Design

## Context

See `proposal.md` for the intended outcome. The confirmed first release is an
English and Simplified Chinese website in this repository's `web/`, published on GitHub Pages at
`https://meshbus.org/`. The homepage serves prospective customers evaluating
Meshbus as a mesh application platform; developer workflows live in `/docs/`.
The user approved the mint/teal pixel direction and identified Zephyr's hardware
ecosystem and LLEXT application loading as the primary differentiators.
Current inspection found:

- The root `package.json` pins OpenSpec development tooling only. There is no
  existing web framework, rendered documentation project or website workflow.
- Technical Markdown is distributed among root guides, `apps/meshbus/`,
  `scripts/meshbus/`, `docs/` and subsystem owners. Samples primarily use RST,
  including Zephyr-specific directives and source-tree navigation.
- `scripts/ci/docs.py` checks tracked Markdown/RST local file targets but does
  not check anchors, external URLs or generated HTML. Website route links need
  separate validation rather than repository-file resolution.
- `scripts/ci/plan.py` treats non-Markdown web files as unknown today, selecting
  full validation. Existing planning regressions are in
  `scripts/ci/tests/test_planning.py`; the source-check job already uses Node 24.
- Repository brand sources exist under `subsys/desktop/assets/logo/`. The supplied
  stacked MESH/BUS raster logo is the website's visual reference; it must not be
  silently replaced by a different repository mark. Its production asset and
  the approved concept illustrations have not yet been added to the repository.
- The current MBA contract covers one foreground, owner-trusted native
  application on a compatible Desktop/LLEXT profile, built against the target
  firmware's EDK. Zephyr board support is an integration foundation, not evidence
  that Meshbus or LLEXT works on every listed board.
- The domain and host are selected, but existing DNS records, domain ownership
  verification, Pages settings and HTTPS readiness have not been inspected.

## Goals / Non-Goals

**Goals:** Keep web development isolated from OpenSpec and Zephyr dependencies;
render owner-maintained guides from one checkout; implement the accepted pixel
identity with accessible web elements; provide customer evaluation paths and
developer onboarding in one static build; publish and verify that build at the
selected custom domain.

**Non-Goals:** Rewrite firmware guides into a new ownership model, convert all
RST, introduce a CMS or server, maintain multiple releases, or attach
the web artifact to firmware/CLI release packaging. There is no runtime server,
CMS, contact-form service or device-control UI. Local acceptance and live Pages
acceptance are separate stages; both are required to finish the full change.

## Decisions

### 1. Use an isolated Astro and Starlight static project

Create `web/package.json` and its own lockfile. Keep the root npm package as
OpenSpec tooling rather than converting it into a monorepo workspace. Use an
actively supported Astro/Starlight pair compatible with Node 24, selected and
pinned during implementation after dependency authorization and license review.

Astro supports a custom project homepage in `src/pages/index.astro`; Starlight
supplies the documentation shell, Markdown rendering, contents, themes and
Pagefind search. Use small native scripts for navigation, search, theme, copy controls and
progressively enhanced product storytelling. Build with static output and no server adapter. Use native CSS
with shared color, spacing, border and typography variables. Product pages use
Astro components; technical content uses Markdown with MDX only where a concrete
interactive or structured-content need justifies it. The first version needs
no additional React/Vue runtime or general-purpose component library.

VitePress also offers a capable documentation site, but Astro permits the
project homepage and documentation to share one build while keeping the homepage
layout independent. A custom React application would require building ordinary
documentation features. Sphinx handles RST well, but the primary owner guides
are Markdown and the first release needs a separate branded homepage. Docusaurus
can also publish to Pages, but there is no existing React component investment
to favor it here.

Official references checked for this proposal:
[Starlight pages](https://starlight.astro.build/guides/pages/),
[Markdown content](https://starlight.astro.build/guides/authoring-content/),
[site search](https://starlight.astro.build/guides/site-search/),
[Astro on Pages](https://docs.astro.build/en/guides/deploy/github/), and
[VitePress overview](https://vitepress.dev/guide/what-is-vitepress).
These establish framework capabilities, not a tested Meshbus implementation.

### 2. Separate site orientation from imported technical guides

Use these product routes, with one shared brand/navigation shell:

| Route | Reader outcome |
| --- | --- |
| `/` | Understand the mesh application platform and its two principal benefits |
| `/platform/` | See how mesh communication, device services and application tools work together |
| `/hardware/` | Understand Zephyr ecosystem reuse, target integration and qualification boundaries |
| `/applications/` | Understand LLEXT/MBA loading, firmware/EDK compatibility and application ideas |
| `/docs/` | Find the developer workflow and its technical guide |

The homepage's primary action is Explore the platform; the application-loading
action leads to `/applications/`. Header entries are Home, Platform, Hardware,
Applications and Docs. Use the stacked logo without a repeated brand wordmark,
a wider search field-style button, and sun/moon theme icons. Present homepage
capabilities with descriptive labels rather than numbered slide-like sections.
Keep the homepage free of redundant eyebrow labels and illustration slogans;
put its platform identity in the lead paragraph. Present hardware choice,
on-demand applications and reusable device capabilities as three linked cards
with pixel icons, readable supporting copy and clear destination arrows.
Recompose the hero illustration as a transparent 3:2 landscape scene; preserve
individual device proportions and contain it within a wider right column.
Group navigation beside the logo and order the right-side tools as Search,
Language and Theme. Product pages link to the relevant technical guides.
They explain benefits without duplicating commands or technical contracts.

Use `/docs/getting-started/` for workflow selection. Group the sidebar into Getting
Started, SDK, Firmware, CLI, MBA, Development and Project. Web-authored pages in
`web/content/` provide orientation and links; technical workflow detail remains
in the following selected sources:

| Owning source | Published route |
| --- | --- |
| `README.md` | `/docs/sdk/` |
| `apps/meshbus/README.md` | `/docs/firmware/` |
| `scripts/meshbus/README.md` | `/docs/cli/` |
| `scripts/meshbus/APP_DEVELOPMENT.md` | `/docs/mba/` |
| `subsys/llext/METADATA.md` | `/docs/mba/metadata/` |
| `DEVELOPMENT.md` | `/docs/development/` |
| `docs/testing.md` | `/docs/testing/` |
| `DISTRIBUTION.md` | `/docs/distribution/` |
| `CONTRIBUTING.md` | `/docs/contributing/` |
| `LICENSING.md` | `/docs/licensing/` |

Add `/docs/samples/` as a web-authored index of the existing service sample
owners, with explicitly labeled repository links to RST guides. Public API
references use the root guide's header inventory and source links rather than
introducing Doxygen generation. References to ZUI/U8g2 and other independent
projects remain external. Publishing ten selected owner guides does not mean
publishing every repository Markdown file, OpenSpec archive or agent workflow.

### 3. Prepare generated content from an explicit source map

Maintain a small source-to-route manifest inside `web/` containing source path,
route, display title and description. Product routes are authored in
`web/src/pages/` with shared Astro components and are not generated as Starlight
documents. A Node preparation command combines mapped technical guides with
web-authored documentation pages into ignored
`web/src/content/docs/` for the standard Starlight content loader. That directory
is exclusively generated; maintainers edit `web/content/` or the owning guides.

Run preparation before development, checks and production builds. A fresh
generation replaces only this dedicated generated directory, rejects duplicate
routes or missing required files, and does not write to any owning guide.
Development documentation must explain how to regenerate after editing an
imported guide; each new invocation reads current inputs. Generated pages,
copied assets, Astro caches and `dist/` are ignored and never committed.

Use Markdown AST transformations for headings and links rather than broad text
replacement. Adapt the top title to frontmatter without losing section IDs or
code examples. Resolve file links relative to the original source directory:

- A mapped Markdown destination becomes its website route; retain and validate
  its fragment against the final rendered heading IDs.
- An unmapped repository document or source file becomes a repository `blob`
  link; directories use `tree` links. Preserve fragments for source references.
- Repository-relative root paths follow the existing docs check convention.
- Relative images are copied from their resolved inputs to generated public
  assets with collision-free paths and preserved provenance.
- External URLs and fenced code text retain their original meaning. Missing
  local references produce actionable errors instead of silently escaping into
  broken repository or website links.

Configure repository URL and source ref explicitly from verified project
metadata, not from an assumed `origin` remote. Source links can use the build
revision, while edit links use the contributing branch. Do not scan or export
unselected private files. Keep the published content set deliberate.
The inspected `meshbus` remote is `https://github.com/Meshbus/sdk-meshbus` and
the current branch is `main`; use that verified repository identity for the
initial source/edit configuration.

An explicit generated projection adds a small content adapter but avoids
maintained copies and a custom loader implementation. Moving all guides into
the website would disrupt current repository consumers; manual copying would
allow commands and limits to drift.

### 4. Express the platform through the accepted pixel identity

The supplied stacked MESH/BUS logo defines the visual identity. Use mint
`#BCECDD`, teal `#16AC98`, forest `#153D32` and warm paper `#F4F8EE` as starting
tokens, adjusted where needed for contrast. Use square controls, stepped edges,
hard offset shadows and crisp pixel illustrations. Reserve a licensed,
self-hosted pixel font for short headings/labels; body text uses readable system
sans serif and code uses monospace. Review font and image provenance before
adding production assets; existing unrelated logo attribution does not prove
ownership of the supplied image or generated derivatives.

Implement headings, copy, buttons and navigation as actual HTML/CSS. Export
illustrations as separate optimized assets with appropriate alternative text;
do not use a full-page concept image as the webpage. Maintain pixel edges at
intended display sizes and avoid excessive decoration on long documentation.

Use this homepage sequence from the accepted concept:

1. Hero: "The platform for mesh applications" and "Your hardware. Your
   applications. Connected by Meshbus." Supporting text introduces Zephyr and
   LLEXT; primary and secondary links lead to Platform and Applications.
2. Hardware ecosystem: diverse generic board/peripheral illustrations connect
   to a Zephyr foundation. Explain reusable drivers/APIs and target adaptation.
3. Loadable applications: a module enters a device above Meshbus/Zephyr layers.
   "Add applications. Keep the firmware." is qualified by compatible profiles
   and matching firmware EDKs in the adjacent copy.
4. Meshbus integration: mesh communication, device services and application
   tools explain the platform's own value beyond its upstream technologies.
5. Closing actions: explore the platform or read the documentation.

Application ideas may include sensing, field communication and custom device
experiences, labeled as ideas until supported by actual case evidence. Keep
claims tied to repository contracts: Zephyr's ecosystem is not universal
Meshbus qualification; runtime loading is not uninterrupted stateful hot swap;
MBA packages are target/firmware-specific native code without an application
isolation boundary. Current support is one foreground MBA on compatible Desktop
profiles. Loading an app without reflashing the base firmware does not promise
an ABI that survives firmware changes, network-wide app deployment or arbitrary
resident service extensions. Preserve firmware-identity/MeshCore-role distinctions.

Use the same brand and shared theme tokens across product and documentation
pages. Preserve Starlight's built-in interaction patterns where possible.

Verify 375/768/1440 widths, contrast, keyboard focus, search focus return, theme
persistence on the production preview. Core reading and
navigation must remain available without JavaScript; search and theme selection
are progressive enhancements.
Keep native HTML/CSS navigation support. On 2026-10-02 the owner explicitly
waived reduced-motion and JavaScript-disabled browser checks, including narrow
navigation in that mode, after CDP permission failures. Preserve these behaviors
but record both browser checks as waived, never as passed. All other local and
hosted acceptance remains required.

### 5. Treat links and search as production-output behavior

Use Starlight's Pagefind integration and test search against `astro build`
output served by the preview command, since the development server is not
evidence that the production index exists. Search acceptance covers terms from
all main developer areas and a no-results query. Deliberately include the product
page content in the search index and verify Zephyr/LLEXT queries reach useful
product and documentation pages.

Add a rendered-output checker for required routes, local anchors, image/script/
style references and search artifacts. Check the generated website rather than
requiring `scripts/ci/docs.py` to interpret Astro routes as repository files.
Exclude website-authored route Markdown from that file checker while retaining
its existing repository scope and using the web checker for those files.
Use focused regressions for source-link conversion, missing sources, duplicate
routes and invalid anchors; avoid unit tests for framework internals or CSS.

Set Astro `site` to `https://meshbus.org`, use static output, and keep `base` at
`/` (or omit it). Use directory-style output and consistent trailing-slash links
so GitHub Pages serves deep routes directly. Set titles, descriptions,
the matching language declaration, a favicon, route-specific canonical URLs and a sitemap using the
same public origin. Bundle a `404.html` with root-relative recovery links that
also work when served for a nested missing URL.

The selected production contract is custom-domain root hosting. Remove the
previous generic `/meshbus/` acceptance target; publishing under a repository
prefix would be a separate future requirement. A local preview serves the same
root-path output. Verify rendered URLs contain neither `/sdk-meshbus/` nor a
development origin in canonical metadata or the sitemap. No SPA fallback or
runtime redirect server is required.

### 6. Validate and publish through one dedicated Pages workflow

Add `.github/workflows/web.yml` for PR/main changes to `web/**`, the exact mapped
guide paths and reused logo inputs, plus its workflow; allow manual validation.
Use Node 24 and npm's committed lockfile. Perform installation, content/type/output
checks, focused regressions and a production build without restoring a west
workspace. Keep filters consistent with declared guide, illustration, font and
copied-asset inputs through a focused coverage check. Pull requests validate
without a deployment job or Pages write permissions.

On main pushes and manual runs targeting main, publish only after all website
build checks pass. Upload that exact `web/dist/` output with the Pages artifact
action and deploy it with `actions/deploy-pages`; do not rebuild different
content in the deployment job. Grant `contents: read` to build and only the
deployment job's required `pages: write` and `id-token: write`. Use the
`github-pages` environment and a Pages deployment concurrency group so deploys
are serialized. Select current compatible Actions and pin reviewed revisions
following repository conventions. No generated-output branch or personal token
is needed. Record the source SHA, workflow run and deployment URL.

Classify only `web/**` and the dedicated workflow as source-only in the existing
planner. The web workflow owns web validation; existing Source checks still own
OpenSpec, licensing and repository hygiene. Add planning regressions for web-only
and mixed firmware/web changes. Preserve conservative fallback for unknown paths
and shared CI selection machinery. A change to `plan.py` itself continues to
select the coverage required by existing rules. Website checks do not bypass
existing source/license checks or release policy.

### 7. Bind meshbus.org and verify the deployed site

The production domain is `meshbus.org`; configure `www.meshbus.org` as a redirect
to the apex domain. Inspect existing settings and DNS before changes, preserving
unrelated records and services. Domain setup proceeds after local acceptance
and within explicit remote/publication authorization:

1. Verify the domain under the owning GitHub account/organization using GitHub's
   TXT challenge, retaining that verification record.
2. Select GitHub Actions as the repository's Pages source and set the custom
   domain to `meshbus.org` before pointing DNS at Pages.
3. Configure apex A records using GitHub's current published addresses, or a
   provider-supported ALIAS/ANAME to `meshbus.github.io`. Configure `www` as a
   CNAME to `meshbus.github.io`, without a repository path. Add IPv6 records
   only as appropriate for the chosen DNS setup; avoid wildcard records.
4. Complete the deployment, wait for DNS/certificate readiness, enable Enforce
   HTTPS and verify the apex plus the `www` redirect.

For custom Actions publishing, GitHub's Pages setting owns the domain binding;
a repository `CNAME` file is not required and is ignored by this publishing
mode. Follow GitHub's hosting contract if generator examples suggest otherwise.
The DNS provider and account permissions are execution details to discover at
deployment time; they do not change the static-site architecture.

Verify homepage and all product routes, direct documentation deep links, assets,
production search, route-specific canonical URLs, sitemap and an actual nested
404 response at `https://meshbus.org`. Check that HTTP and `www` converge to the
canonical HTTPS origin. If DNS, certificate issuance or access is unavailable,
keep the live acceptance tasks open and record the blocker.

Hosting references:
[Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages),
[custom domains](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site),
and [domain verification](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/verifying-your-custom-domain-for-github-pages).

## Risks / Trade-offs

- Imported repository syntax or heading IDs may render differently → Exercise
  the actual selected guides and validate the final HTML links and anchors.
- New content inputs may be omitted from CI filters → Compare the declared
  source/asset inputs with workflow coverage; test guide-only changes.
- Full owner guides are long → Provide clear groups and section navigation
  while keeping technical detail at its current owner.
- Framework or package notices may be missing from generated assets → Review
  actual pinned dependencies, preserve retained notices and run the repository
  license policy; do not assume the root license covers third-party output.
- Upstream hardware support may be mistaken for Meshbus qualification, or loading
  for universal hot swap → Keep the target/profile/EDK boundaries next to the
  benefits and review the product copy against current owner guides.
- Pixel typography or large images may hurt reading → Limit the display font,
  optimize separate assets and review narrow screens, keyboard use and contrast.
- DNS or certificate readiness may delay publication → Keep local and hosted
  evidence separate and leave live tasks open until the custom domain passes.

## Migration Plan

Add the isolated web project and content projection without relocating existing
guides. Build the customer pages and pixel assets, verify the production preview,
then land the scoped workflow and contributor instructions. After authorized
publication and domain setup, run the live acceptance checks. Both local and
deployed evidence are required to complete this change; no firmware or device
acceptance is needed for website behavior.

For a site regression, redeploy a previously verified source revision through
the same checks, retaining the custom-domain binding. Preserve a record of
pre-change DNS/Pages settings for an authorized hosting rollback; do not delete
verification or unrelated records as routine cleanup. Before first publication,
rollback can remove the new website integration while preserving owner guides.

### 8. Publish complete English and Simplified Chinese editions

Keep English routes unchanged and prefix Chinese routes with `/zh-cn/`. Use
Starlight locales for documentation navigation and built-in controls. Product
views share templates and a static translation catalog. Direct build dependencies
include the already-installed MIT packages `@astrojs/markdown-satteri` 0.4.2
and `github-slugger` 2.0.0 for explicit source heading IDs. A native disclosure dropdown lists each language by its own name and uses links
to open the corresponding page; retain section fragments where
possible. Local links stay within the selected language. No automatic language
redirect or runtime translation service is required.

Translate all thirteen published documentation pages, including all ten mapped
technical guides. Store reviewed Markdown prose translations under `web/content/`
as catalogs tied to their original source and SHA-256. Preparation reads the
authoritative English Markdown and substitutes prose blocks, preserving code,
inline identifiers, link destinations, tables and original section anchors.
Reject missing entries, stale source digests and changed code/link contracts;
never publish an English fallback as a completed Chinese guide. Source links
identify the original guide and translation edit links identify the catalog.

Set `lang`, localized titles/descriptions, canonical and reciprocal `hreflang`
metadata. Pagefind creates separate language indexes from HTML language values.
Search controls and result states use the current language. GitHub Pages has
one root 404 document, so provide English and Chinese recovery links there.
Use readable CJK system fonts while retaining pixel artwork, borders and colors.

Verify both complete route sets, source freshness failures, unchanged code and
valid fragments, language switching, Chinese and English search, and responsive
product/docs views in both themes. The two existing browser waivers still apply.


### 9. Unify layout and refine every page

The approved refinement keeps one fixed header geometry across product and
Starlight pages: 88 px at widths of at least 800 px, 80 px at 600–799 px and
128 px for the two-row phone header. The inner container is at most 1180 px,
with 32 px gutters on desktop and 20 px below 800 px. Shared controls use the
same font, line height and dimensions. Reserve search/theme controls during
initialization. The initial theme follows the system unless explicitly saved.
Place the mobile documentation toggle in a distinct 48 px row below the site
header, next to the in-page contents control; retain Starlight popover/focus
behavior. Header offsets also govern body placement and anchor scrolling.

Product pages use native scrolling through visible text sections with a sticky
visual at widths >=1100 px and heights >=700 px. The homepage explains hardware,
services and applications; Platform highlights four architecture layers with
keyboard/click selection; Applications follows EDK, build and install/run.
Hardware uses selection criteria and modest section emphasis. Smaller screens
and reduced-motion preference use ordinary document flow. Use native CSS and
IntersectionObserver without new animation dependencies or scroll interception;
content remains visible before enhancement and inactive scenes stop updating.

Remove redundant small labels and decorative numbering. Provide formal section
headings and consistent spacing, consolidate hardware next steps and retain
all compatibility/trust qualifications. Documentation uses the original H1 as
its single visible title, with source attribution directly below; preserve
source heading anchors, Pagefind metadata and code. Reorganize the authored
orientation pages by task and service purpose, with complete matching Chinese
catalogs. Keep technical source contracts at their existing owners.

Acceptance covers 34 bilingual content routes and 404 at 375/768/1162/1440 px
in both themes, plus shared-header breakpoint and <=1 px geometry comparisons,
keyboard/menu/search/language flows, scroll scenes and contained document
code/table overflow. Record the two existing browser waivers separately.

### 10. Keep the website implementation small

Remove unused rules from earlier layouts and give current product components
one stylesheet owner, imported by the Product layout. Keep shared controls and
Starlight adaptation explicit. Preserve the rendered design during this cleanup.

Use the existing UI dictionary for product copy and localized client messages.
Pass only the messages needed by each interaction from its component; search
uses Astro's normal typed script build while Pagefind stays a runtime import.
Retain the early inline theme initializer, asynchronous search result ordering,
focus restoration and progressive enhancement. Add no framework or dependency.

Name the two documentation groups directly and deduplicate source paths before
collecting input dependencies. Workflow checks retain the publication constraints
without requiring a single deploy step or exact whitespace. Keep validation
focused on the supported workflow contract; do not build a general Actions
expression interpreter. Source translation freshness, code/link preservation,
rendered output checks and source ownership remain required.

### 11. Explain capabilities through continuous product scenes

Use the owner-selected HeyPCB multiplayer reference for motion direction only:
retain one recognizable object while native scrolling reveals its parts and
behavior. Create original SVG geometry in the existing mint/teal pixel style;
do not import that site's artwork, code or product claims. Platform separates
hardware, Zephyr, services and application layers, then brings them together.
Home connects generic hardware to shared services and a local application.
Applications follows the matching EDK, package construction and loading on one
compatible host, retaining separate installation and launch explanations.

Extend the existing story controller with continuous bounded progress and a
short hold around each stage. Keep explicit keyboard/pointer navigation, the
1100 px/700 px sticky threshold, normal-flow text on smaller screens and static
reduced-motion presentation. Use one scene component for the three authored
SVG variants; no dependency, generic timeline framework or wheel interception.
Documentation and hardware evaluation remain quiet reading surfaces.
