# Proposal

## Why

Meshbus needs a public entry point where prospective customers can understand
its value as a foundation for mesh network applications. Its two principal
advantages are access to Zephyr's broad hardware ecosystem and native application
loading through LLEXT. Meshbus brings these together with mesh communication,
device services and application tools. Existing technical guides are distributed
across repository Markdown and RST files, making evaluation and onboarding harder.

An English and Simplified Chinese website at `https://meshbus.org/` will explain those benefits through
a distinctive pixel-art product presentation and provide searchable developer
documentation for customers ready to build.

## What Changes

- Add an independently installable Astro/Starlight static project under `web/`,
  with Pagefind search, native CSS and an isolated npm manifest/lockfile.
- Lead the homepage with customer benefits: broader hardware choice through
  Zephyr, loading compatible MBA applications without reflashing base firmware,
  and Meshbus's integration of communication, services and tools.
- Add product routes for Platform, Hardware and Applications alongside `/docs/`.
  Preserve SDK, firmware, CLI and MBA onboarding inside the documentation.
- Use the supplied stacked MESH/BUS pixel logo and the accepted mint/teal pixel
  visual direction, with readable body text and responsive, accessible layouts.
- Explain hardware adaptation and firmware/EDK compatibility accurately;
  distinguish application ideas from verified deployments or product guarantees.
- Publish existing owner-maintained Markdown guides through an explicit content
  map, with working web links, headings, code examples and source/edit links.
  Add concise web-specific orientation and sample navigation with complete Chinese translations checked against their English sources.
- Provide documentation navigation, in-page contents, local full-text search,
  light/dark appearance and usable mobile and keyboard interactions.
- Add reproducible local development, production build and preview commands,
  plus website checks in CI that cover web sources, imported documentation and
  published assets. Validate pull requests and deploy successful main-branch
  builds through GitHub Actions to GitHub Pages.
- Publish at the custom apex domain `meshbus.org`, with root-relative routes,
  canonical metadata, a sitemap, HTTPS and a `www.meshbus.org` redirect to the
  apex domain. Include domain setup and live-site verification in acceptance.
- Support English at existing root routes and Simplified Chinese at `/zh-cn/`,
  including every product page and all thirteen published documentation pages.
  Provide corresponding-page language switching and language-scoped search.
- Keep the release focused on the current repository.
  Versioned documentation, generated API reference,
  browser-based device operations, CMS/backend services, sales forms and
  Enterprise-only documentation are outside this change.

## Capabilities

### New Capabilities

- `project-website`: Customer-facing platform presentation, pixel visual identity,
  source-backed developer documentation, navigation/search and verified GitHub
  Pages delivery at `meshbus.org`.

### Modified Capabilities

None. The existing `development-validation` requirements continue to govern
selection and evidence; website inputs become a declared validation domain.

## Impact

- Primary consumers: prospective customers evaluating Meshbus for mesh products;
  supporting consumers are SDK integrators, firmware developers, CLI users and
  MBA authors.
- New web source, isolated npm manifest/lockfile, content mapping and build/check
  tools in `web/`. The proposed stack is Astro with Starlight and its static
  Pagefind search; versions and applicable notices must be reviewed during apply.
- Existing repository guides remain technical source documents. The website
  consumes selected Markdown and links to RST samples and independent dependencies.
  The user-supplied pixel logo and implementation-ready illustrations/fonts need
  durable repository assets and recorded provenance during implementation.
- Repository integration touches ignore rules, documentation check scope,
  website CI, narrowly scoped CI classification and its regressions, and
  repository development/testing instructions and license metadata as needed.
- Hosting integration includes Pages settings, DNS records, HTTPS and deployment
  evidence for `meshbus.org`; current DNS and remote configuration are unverified.
- No firmware, public C/Rust API, protocol, west dependency or release artifact
  contract changes. This revision updates planning only. Dependency installation,
  remote settings, DNS, publication and Git operations follow the applicable
  user authorization during implementation; the plan does not grant it.
