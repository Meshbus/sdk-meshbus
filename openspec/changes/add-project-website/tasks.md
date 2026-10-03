# Tasks

## 1. Isolated website foundation

- [x] 1.1 Confirm apply authorization covers adding/installing web dependencies, select a Node 24-compatible Astro/Starlight pair and required build/check tools, and pin them in `web/package.json` and its lockfile; verify locked installation and retain the actual third-party license/notice findings.
- [x] 1.2 Add static Astro/Starlight configuration for `site: https://meshbus.org`, root base, directory output, repository/source refs and generated-output ignore rules without changing root OpenSpec dependencies; verify a minimal page builds, generated files stay untracked and the root OpenSpec command still works.
- [x] 1.3 Provide `npm --prefix web run dev`, `check`, `test`, `build` and `preview` scripts and installation instructions in `web/README.md`; execute the initial development/build/preview commands as documented and verify no west workspace is needed.
- [x] 1.4 Apply native SPDX comments or exact provenance-backed REUSE metadata to new source/configuration/asset inputs and preserve dependency notices; run the repository license policy in the development Python environment and resolve in-scope findings before proceeding.

## 2. Source-backed documentation and content checks

- [x] 2.1 Declare the ten owning guide-to-route mappings from `design.md` plus authored product/documentation pages and all published logo, illustration, font and copied-asset inputs; verify sources exist, routes are unique and no archive, private file or sibling-project content is implicitly exported.
- [x] 2.2 Implement fresh content preparation into the dedicated ignored Starlight content directory before dev/check/build; verify a source edit appears on rebuild without changing the owning guide and focused regressions reject a missing source, duplicate route and stale generated page.
- [x] 2.3 Implement source-relative Markdown link/image transformations, original section-anchor preservation and repository blob/tree fallback using the verified `https://github.com/Meshbus/sdk-meshbus` repository and configured source ref; verify mapped fragments, source-only references, root-relative assets and unchanged fenced command text with focused public-output regressions.
- [x] 2.4 Add English documentation overview, workflow orientation and service sample index in `web/content/`, then publish all ten mapped guides with source/edit links and grouped navigation; verify SDK, firmware, CLI, MBA, development, testing, distribution, contribution and licensing entry paths and labeled RST/external references in the preview.
- [x] 2.5 Add rendered-output validation for required pages, local links/anchors, assets and page metadata; verify successful output passes and a broken link, missing asset and invalid fragment each fail with the referring page and target identified.
- [x] 2.6 Adjust `scripts/ci/docs.py` only for website-authored route Markdown and add the relevant checker regression; verify existing repository Markdown/RST remains checked while website routes are validated by the website checker, then document source ownership and regeneration in `web/README.md`.

## 3. Customer presentation and pixel identity

- [x] 3.1 Prepare durable production assets from the supplied stacked MESH/BUS logo and accepted pixel concept, with separate hardware/application illustrations and a licensed display font; verify provenance, retained notices, optimized dimensions and crisp rendering, and document the asset sources in `web/README.md` without private attachment paths.
- [x] 3.2 Build the homepage and `/platform/`, `/hardware/`, `/applications/` pages with shared navigation and the customer journey in `design.md`; review Zephyr, LLEXT, one-foreground-MBA and EDK claims against owner guides, label application ideas, and verify every product/documentation action reaches its intended page.
- [x] 3.3 Implement the mint/teal pixel identity with real HTML text/actions, native CSS tokens, squared controls and hard shadows; compare against the accepted concept and inspect all product pages plus long guides at 375/768/1440 widths in both themes, checking contained overflow, contrast and theme persistence. Reduced-motion browser verification is waived by the owner on 2026-10-02; retain the implementation.
- [x] 3.4 Verify accessible names, visible keyboard focus, header/menu navigation, document contents and exact code-copy output; correct failures. JavaScript-disabled browser verification, including narrow viewports, is waived by the owner on 2026-10-02; retain native reading/navigation support.
- [x] 3.5 Document actual layout/component/style ownership and image/font usage in `web/README.md`; verify the named implementation files exist and the instructions keep technical contracts in their owning guides.

## 4. Search and custom-domain static output

- [x] 4.1 Enable Pagefind for product pages and documentation and require its production artifacts in output checks; verify Zephyr/LLEXT product results and `CONFIG_MBS`, firmware-role terms, `meshbus connect` and `exported-symbols` documentation results, a no-match state and keyboard Escape focus return in production preview.
- [x] 4.2 Build and serve root-path output with directory routes for meshbus.org; verify direct product/documentation deep-link reloads, navigation, assets and search, and reject accidental `/sdk-meshbus/` prefixes in published internal routes.
- [x] 4.3 Add English titles/descriptions, favicon, route-specific canonical URLs, sitemap and `404.html`; verify metadata uses `https://meshbus.org` with correct paths and no localhost URLs, and verify recovery links from a nested missing URL.
- [x] 4.4 Document installation, production preview, fixed production origin/root path and `web/dist/` in `web/README.md`; execute the commands and verify the output contains pages/assets/search without a server adapter.

## 5. Repository and CI integration

- [x] 5.1 Add `.github/workflows/web.yml` for web sources, mapped guide/asset inputs and manual runs using Node 24, locked install, focused tests, production build and rendered checks; validate workflow syntax and verify a coverage check rejects a declared content/asset input missing from its triggers.
- [x] 5.2 Classify only `web/**` and the dedicated workflow as source-only in `scripts/ci/plan.py`; add and run `scripts/ci/tests/test_planning.py` regressions proving website-only edits avoid firmware/CLI builds, mixed edits retain firmware coverage and unknown/shared paths still retain conservative selection.
- [x] 5.3 Add website contribution/check routing to `README.md`, `DEVELOPMENT.md`, `docs/testing.md` and `.github/CI.md` as needed; run repository reference checks and verify documented commands match the implemented web scripts without replacing owner-maintained technical guides.
- [x] 5.4 Add checked-artifact upload and main-only Pages deployment with scoped permissions, `github-pages` environment, deployment concurrency and reviewed Action pins; verify the workflow conditions reject PR/non-main/failed-check deployments, deploy consumes the checked build artifact, and `web/README.md` documents publication and rollback.

## 6. Integrated local acceptance

- [x] 6.1 From the finished checkout, run locked installation, website tests/checks and the meshbus.org production build with rendered validation; verify all product pages, mapped guides, regenerated content, local assets and search are present and owning sources have no unintended edits.
- [x] 6.2 Complete production-preview review of the customer evaluation and developer onboarding journeys, including viewport, theme, keyboard, deep-link and nested-404 cases; record actual observations and correct in-scope failures before publication. Exclude the two owner-waived browser checks recorded below.
- [x] 6.3 Run strict OpenSpec validation, repository documentation checks, applicable CI-planning regressions, workflow validation, the license policy in the development Python environment and whitespace checks; record commands, outcomes and source/build identity, keeping hosted acceptance separate.

## 7. GitHub Pages and meshbus.org acceptance

- [ ] 7.1 After local acceptance, use the applicable authorization to inspect Pages/DNS state, preserve unrelated records and record a rollback reference; document the provider-specific TXT verification, apex, www and HTTPS setup in `web/README.md`, verifying instructions against GitHub's current requirements.
- [ ] 7.2 Within authorized GitHub/DNS scope, verify meshbus.org ownership, set Pages to GitHub Actions with custom domain meshbus.org, configure apex/www records and enable HTTPS when ready; verify authoritative DNS results, the retained verification record and Pages domain/certificate status, leaving the task open if external access or propagation blocks it.
- [ ] 7.3 After authorized Git publication/run, verify actual hosted checks and Pages deployment succeeded for the intended source SHA; retain the workflow/deployment URLs and confirm PR validation and main publication remain distinct. A queued run or workflow syntax check is insufficient.
- [ ] 7.4 Verify the live HTTPS homepage, product routes, documentation deep links, assets, Pagefind results, canonical URLs, sitemap, nested 404 recovery and HTTP/www redirects preserving the path; record actual results and source identity below, completing the change only when local and live acceptance both pass.

## 8. English and Simplified Chinese editions

- [x] 8.1 Add shared locale routing, bilingual product templates, navigation, search controls and language switching; retain existing English URLs and provide all four Chinese product pages.
- [x] 8.2 Translate all thirteen documentation pages in full; preserve source code, identifiers, links and anchors, and reject missing or stale source-linked translation catalogs.
- [x] 8.3 Configure localized Starlight navigation, metadata, reciprocal language alternatives, sitemap and separate Pagefind indexes; provide bilingual 404 recovery.
- [x] 8.4 Add focused translation/route/metadata regressions, document the translation workflow, and pass website tests, checks and the static production build.
- [x] 8.5 Verify corresponding-page language switches, Chinese/English search, deep links and Chinese product/docs layouts at 375/768/1440 in both themes; record actual evidence and run strict OpenSpec, license and whitespace checks.


## 9. Shared layout and full-page refinement

- [x] 9.1 Unify fixed product/documentation header geometry, reserved controls, system-theme default and mobile document toolbar; verify equal control geometry within 1 CSS pixel and menu/search/anchor behavior at standard widths and breakpoint boundaries.
- [x] 9.2 Refine all four bilingual product pages with clear section hierarchy, accurate next steps and progressively enhanced native scroll stories; verify forward/reverse scrolling, explicit layer selection, resize and narrow layouts.
- [x] 9.3 Remove duplicate document titles while preserving anchors, metadata and source attribution; reorganize both editions of overview, getting started and sample navigation, and refine 404 recovery.
- [x] 9.4 Review all 34 bilingual routes and 404 at 375/768/1162/1440 in both themes; correct page-wide overflow, unreadable content, broken navigation and inappropriate spacing, retaining the two browser waivers.
- [x] 9.5 Run website tests, check, build/output validation, strict OpenSpec, repository reference checks, incremental license policy and whitespace checks; document implementation ownership and actual acceptance with current source/output identity.

## 10. Architecture cleanup

- [x] 10.1 Remove unused product CSS and consolidate current product/search style ownership without changing rendered geometry.
- [x] 10.2 Use the existing UI dictionary for product and client messages, remove obsolete entries, and include search in Astro's typed script build while preserving its behavior.
- [x] 10.3 Deduplicate source/input checks, name the documentation groups explicitly, and relax deployment checks that reject harmless formatting or reporting changes; retain meaningful deployment constraints and regressions.
- [x] 10.4 Verify rendered content/layout stability, search/theme/menu/language interactions, website tests/check/build, OpenSpec, repository references, license policy and whitespace; update implementation ownership and acceptance.

## 11. Continuous product scenes

- [x] 11.1 Replace the platform diagram with an original layered pixel-style SVG that expands and assembles with native scroll progress; preserve keyboard and pointer selection.
- [x] 11.2 Apply the same scene controller to connected hardware/services on Home and the matched EDK/build/install/run workflow on Applications; keep bilingual claims and compact static layouts.
- [x] 11.3 Verify both languages/themes, forward/reverse scrolling, layer selection, resize thresholds and narrow layouts; run website tests/check/build, OpenSpec, reference, incremental license and whitespace checks, and record actual acceptance.

## 12. Homepage ownership and visual rollback

- [x] 12.1 Restore Home's earlier continuous device scene, styles and bilingual captions after the owner rejected the replacement relationship diagram; preserve the other scene variants and existing compatibility limits.
- [x] 12.2 Explicitly identify Meshbus as a FoBE Studio project in the shared bilingual product footer.
- [x] 12.3 Verify the restored scene and bilingual footer; run website tests/check/build, OpenSpec, incremental license and whitespace checks, and record current source/output identity.

## Validation

Previous local acceptance is recorded below. Section 10 tracks the approved
architecture cleanup; it remains open until its checks pass.
Tasks 3.3, 3.4 and 6.2 rely on the recorded checks plus two explicitly waived
browser cases, not passing results for those cases. Section 7 has no hosted
acceptance. Progress before section 9: 31/35 complete.

### Original planning validation

These results describe the original plan before the customer-facing pixel and
GitHub Pages scope revision; they do not validate the revised files.

Proposal checks on 2026-10-02, against repository HEAD `74e45aa` plus the new
uncommitted change artifacts:

- `OPENSPEC_TELEMETRY=0 npm run openspec -- validate add-project-website --strict --no-interactive`: passed.
- `OPENSPEC_TELEMETRY=0 npm run openspec -- status --change add-project-website`: all four planning artifacts present; this is planning completion only.
- Direct use of `scripts/ci/docs.py`'s local-reference checker on all four new Markdown artifacts, plus whitespace/final-newline checks: passed. External URLs and anchors were not crawled.
- `python scripts/ci/license_policy.py --output <external-task-directory>` in the activated development Python environment: passed, with zero remaining metadata findings.
- `git diff --check` and worktree inspection: passed; only the new change directory was added, with no project implementation, dependency, index or Git-history changes.

No website build, browser behavior, hosted CI or deployment has been executed.
The checks above validate the planning files and applicable metadata only.

### Revised planning validation

Revision checks on 2026-10-02 against HEAD `74e45aa` plus the revised uncommitted
planning files:

- `OPENSPEC_TELEMETRY=0 npm run openspec -- validate add-project-website --strict --no-interactive`: passed.
- `OPENSPEC_TELEMETRY=0 npm run openspec -- status --change add-project-website --json`: all four planning artifacts present; 30 implementation/deployment tasks remain unchecked.
- Direct `scripts/ci/docs.py` local-reference checks on all four revised Markdown files, including untracked files, and final-newline/trailing-whitespace checks: passed. External URLs and anchors were not crawled by this check.
- `python scripts/ci/license_policy.py --output <external-task-directory>` in the activated development Python environment: passed; zero remaining metadata findings. This is the repository policy result, not a full REUSE or distribution-clearance claim.
- `git diff --check` and comparison with a pre-edit snapshot: only the four existing planning artifacts were revised; change metadata and the unrelated release-task record were preserved.

The reviewed scope now covers the customer-facing mesh application platform,
supplied pixel identity, Astro/Starlight/Pagefind and GitHub Pages at meshbus.org.
No website implementation, dependency installation, DNS/Pages mutation, hosted
run, public deployment, staging, commit or push was performed in this revision.

Implementation acceptance requires both the local checks in section 6 and live
checks in section 7. Firmware builds, hardware tests, signing and firmware/CLI
release qualification are not required for this web-only behavior. Public
deployment and domain setup are planned deliverables; writing this plan performs
neither and does not authorize dependency, remote, DNS or Git operations. Record
unavailable access or pending certificates as unmet acceptance, not a waiver.

### Implementation progress

- CI selection: two new website regressions failed on the original planner, then passed after narrowly classifying `web/**` and `.github/workflows/web.yml`. All 38 tests in `scripts/ci/tests/test_planning.py` passed in the development Python environment. This is planner evidence, not a hosted website run.

### Local implementation acceptance — 2026-10-02

Source baseline: repository HEAD `74e45aa`, plus the uncommitted website and
explicit documentation/CI integration changes. Root OpenSpec package files and
the pre-existing `simplify-release-artifacts/tasks.md` changes were compared with
the pre-implementation snapshot and preserved byte-for-byte. Nothing was staged,
committed, pushed or deployed. The supplied logo was copied unchanged.

- Dependency scope was explicitly authorized. Node 24.21.0 locked installation
  (`npm --prefix web ci --ignore-scripts --no-audit --no-fund`) passed. The final
  direct dependencies are pinned; installed notices and license declarations
  are collected into every static build. Build-tool MPL/LGPL and embedded OFL
  distinctions are recorded in `web/README.md`.
- Under Node 24.21.0: `npm --prefix web test` passed 12 tests; `npm --prefix web run
  check` reported zero errors, warnings or hints and verified 22 declared input
  paths plus font files; `npm --prefix web run build` passed, producing 18 HTML
  pages and 74 total files with rendered link/anchor/asset/metadata/sitemap and
  Pagefind checks. Final local output was about 2.9 MiB. Source artwork is
  optimized into 3 KiB logo and 36–192 KiB illustration WebP variants.
- `dev` and `preview` were launched and returned HTTP 200 without west. The
  production preview used `http://127.0.0.1:4322/`; local canonical URLs remained
  `https://meshbus.org`. Astro's agent background mode initially outlived the
  launch command unpredictably; the documented `--ignore-lock` foreground mode
  provided a stable preview.
- Regressions cover fresh regeneration/stale cleanup, changed owner text,
  missing sources/duplicate routes, relative/mapped/blob/tree/image links,
  unchanged fences, rejection of assets outside the repository, broken rendered
  links/assets/fragments and empty directories, workflow input coverage and
  deployment guard failures. The output checker found and led to correcting an
  existing CLI-guide link to a renamed licensing section in its owner file.
- In-app Chromium review: all four product pages and the long MBA/CLI guides
  were measured at 375, 768 and 1440 px in both themes (36 cases), with no page
  horizontal overflow. Representative screenshots were inspected; the full
  homepage, original logo, network illustration and application illustration
  rendered correctly. Offscreen lazy imagery was loaded on scrolling. Body
  muted-text token contrast is at least 5.01:1 across the used paper/mint pairs;
  the light accent heading pair is 4.32:1 for large text.
- Verified all ten guide routes plus orientation/sample pages and their exact
  owner edit links; product/docs navigation, narrow site menu using Enter,
  Starlight document menu, mobile page contents, section navigation, theme
  persistence and direct deep-link reload worked. Keyboard focus showed a 3 px
  teal outline with 5 px offset. CLI command copy matched the complete rendered
  code text byte-for-byte, including newlines, quotes and backslashes.
- Production Pagefind queries returned: Zephyr 15 results including hardware,
  homepage and platform; LLEXT 11 including applications/homepage; CONFIG_MBS 4;
  meshbus connect 8 including CLI; exported-symbols 1 (MBA guide); Repeater 5
  including firmware. An unmatched term showed the empty state. Escape closed
  search and returned focus to its opener. Browser testing exposed a Vite
  dynamic-import preload error; serving the authored search module directly
  fixed it. Product heading styles were scoped to prevent a large docs TOC.
- Nested `/unknown/nested/path/` served the custom 404 with working root-relative
  recovery links. The documentation recovery link reached `/docs/` and assets
  loaded. Canonicals, English metadata, favicon and root sitemap passed checks.
- All 39 Python CI-planning/docs regression tests passed in the development
  environment. Local-reference checks included 103 tracked and untracked
  Markdown/RST files and passed; only authored `web/content/*.md` routes are
  delegated, while RST and the website README remain checked. actionlint 1.7.12
  accepted the workflow. SHA-pinned Pages actions were verified against upstream
  release commits. Local checks do not establish hosted PR or deployment success.

### Earlier acceptance blockers (before owner waiver)

The browser permission check rejected raw CDP on the local preview, reporting
that permission had been declined. Explicit permission has been requested for
only temporary JavaScript disabling and reduced-motion emulation, followed by
restoration. Those browser cases remain unperformed; tasks 3.3, 3.4 and 6.2 are
not marked complete. Reduced-motion CSS and native HTML navigation exist, but
source inspection does not replace the agreed browser checks.

DNS provider/access and GitHub publication authorization are not yet supplied.
No Pages settings, DNS records, verification TXT, certificates or live redirects
have been inspected or changed. The provider-neutral domain procedure follows
GitHub's current docs, but task 7.1 awaits provider-specific inspection/rollback
records. Tasks 7.1–7.4 remain open; no archive is appropriate yet.

### Final repository checks and build identity

- Strict OpenSpec validation passed for `add-project-website`; `git diff --check`
  passed. The development-environment license policy passed with zero remaining
  metadata findings against a source-only snapshot containing every tracked and
  untracked non-ignored file. The snapshot had its own empty temporary Git root
  so REUSE emitted repository-relative paths; no project index was changed.
- The initial direct checkout scan included ignored `web/node_modules`, `.astro`
  and `dist` because REUSE 6.2.0's Git directory optimization did not enumerate
  ignores inside the wholly untracked `web/` directory. That raw failure was
  retained separately. No new policy exemptions or third-party relicensing were
  used to suppress it. The successful source snapshot scan is repository-policy
  evidence, not full REUSE or legal-distribution clearance.
- Website input SHA-256:
  `d27a647bc7d2a88f24364e6efa0096b4a90e74f190a36d2eae5fa6148e80d09c`.
  This hashes sorted relative paths, NUL, file bytes, NUL for non-ignored `web/`
  files, the ten mapped guide inputs and `.github/workflows/web.yml`.
- Static output SHA-256:
  `6c2253e8681ba0515a49a15f9f6231e9df8baf39a5c125edcb67cfcea11178f1`.
  This uses the same path/bytes convention for every file under `web/dist/`.
  These identify local uncommitted inputs/output, not a hosted source commit.

Progress at that point: 23/30 complete. Browser cases requiring the denied permission and all
four live deployment tasks remain open.


### Owner-supplied web logo assets — 2026-10-02

The project owner supplied the `meshbus-platform-assets-2026-09-12` web asset
pack. Replaced the initial JPG with its unchanged transparent stacked color SVG
and replaced the temporary favicon with its unchanged pixel M SVG. The shared
header retains the stacked layout and no longer paints a white logo background.
The external asset directory remains unchanged; only these two selected inputs
are published. Their source names and SHA-256 values are recorded in
`web/README.md`, with exact-path attribution in `REUSE.toml`.

- Node 24 website tests: 12 passed; `check`: zero errors, warnings or hints;
  `build`: 18 HTML pages, 74 files, rendered output checks passed.
- Repository license policy passed in the development Python environment on a
  fresh source-only snapshot, with zero remaining metadata findings. This uses
  the same workaround for REUSE's untracked-directory ignore discovery described
  above; the project index remains untouched.
- Updated website input SHA-256: `4b851db95e54ed34ee7b88516a56814d4434cde5491996bf3a2615a92fc7c46f`.
- Updated static output SHA-256: `dc6c92da5f5fc8d64bbb9b6e2101f1300256c469a251df696a8bfa5594e236b3`.
  These supersede the earlier local build identity using the same hash convention.
- The refreshed assets have not received a browser visual check. After the user
  unlocked the computer, the preview was restarted and returned HTTP 200, but
  Browser Use remained blocked on its previous connection-error page's data URL.
  Neither reduced-motion nor JavaScript-disabled emulation executed. Tasks 3.3,
  3.4 and 6.2 therefore remain open, along with all four hosted tasks.


### Browser connection recovery — 2026-10-02

The in-app browser connection recovered. The current SVG build was inspected
through normal browser APIs at `http://127.0.0.1:4322/`:

- The transparent stacked SVG loaded and displayed without a white tile on the
  375-pixel homepage in both themes and the MBA guide at 375/1440 pixels.
- Mobile main navigation opened and reached documentation; the documentation
  menu opened and reached the MBA guide. Explicit light selection survived
  navigation and reload.
- Inspected document widths were 360, 753 and 1425 pixels at corresponding
  viewport widths 375, 768 and 1440, with no page-wide horizontal overflow.
- Temporary viewport settings were reset; the homepage remains a browser
  deliverable. No CDP emulation was applied.

The first reduced-motion CDP command was rejected because a saved user permission
setting blocks raw CDP for the local origin. This replaces the earlier
connection-error blocker. JavaScript-disabled and reduced-motion browser cases
remain unperformed; tasks 3.3, 3.4 and 6.2 remain open. Source and output hashes
are unchanged from the owner-supplied asset update above; this turn changes only
this acceptance record. Hosted tasks remain untouched.


### Owner-approved verification scope — 2026-10-02

The owner explicitly requested that reduced-motion and JavaScript-disabled
checks no longer be performed. Both browser checks are waived for this change;
neither ran or passed. Stop retrying CDP for these cases. Keep the existing
reduced-motion styling and native reading/navigation behavior; this decision
changes verification scope, not the site's behavior requirements.

The other local acceptance results above complete revised tasks 3.3, 3.4 and
6.2. Current progress is 26/30 complete. Tasks 7.1–7.4 remain open for authorized
Pages/DNS inspection, configuration, hosted deployment and live verification.
The change remains active. No implementation, dependency, Git index, publication
or remote settings were changed by this scope revision.

Revision validation: strict OpenSpec validation and whitespace checks passed.
The default development-environment license command passed with zero remaining
metadata findings; existing shared licensing edits automatically expanded it to
1,216 selected files, excluding `web/` under the current repository policy.
Website code and built output were not changed or rebuilt for this revision.

### Full English and Simplified Chinese editions — 2026-10-02

Each language now has four product pages and thirteen complete documentation
pages. Existing English URLs remain stable; Chinese routes use `/zh-cn/`.
The thirteen reviewed catalogs translate 629 prose blocks, retaining executable
code, identifiers, link destinations and matching English heading anchors.
Source path, SHA-256 and exact original text checks reject stale or incomplete
translations before replacing generated documentation. Language switches retain
the corresponding page and section. Chinese navigation, search, document menus,
accessible labels and edit links are localized.

Local validation with Node 24:

- Clean lockfile installation succeeded. All 17 website tests passed. Astro
  check reported zero errors, warnings or hints; input/workflow checks passed.
- Production build passed: 35 HTML pages and 111 output files. Pagefind indexed
  17 pages per language. Rendered checks verified language tags, canonical and
  reciprocal alternate URLs, sitemap coverage, links/fragments/assets, matching
  documentation heading anchors and unchanged code blocks across languages.
- Normal in-app browser APIs checked all four product pages plus MBA and CLI
  documentation at 375, 768 and 1440 pixels, in both themes and languages:
  72 route/viewport/theme cases without page-wide horizontal overflow.
- Visual inspection covered the Chinese mobile homepage and MBA guide, desktop
  dark homepage and tablet dark MBA guide. Both navigation menus, localized
  documentation groups and explicit theme persistence worked. Temporary
  viewport settings were reset after inspection.
- Product switches reached corresponding pages. MBA language switches preserved
  `#execution-and-trust-model` in both directions. Chinese searches for `应用`,
  `meshbus connect` and `资源回收` returned Chinese routes; English `LLEXT`
  search returned English routes. Empty-result messaging was localized, and
  Escape closed search and restored focus to its trigger.
- `/zh-cn/missing/deep/` returned HTTP 404; its Chinese documentation recovery
  link reached `/zh-cn/docs/`. The shared 404 includes both language choices.
- Strict OpenSpec validation, repository documentation links (including the
  untracked website README and change artifacts) and whitespace checks passed.
  The default license policy passed in the development Python environment with
  zero remaining findings. Existing shared licensing edits expanded selection
  to 1,216 files; `web/` remained excluded under current repository policy.

Final local build identity, using the path/NUL/bytes/NUL convention above:

- Website inputs:
  `fb8083255b9b33301360480616f59fe38a43a050058439c2e71b3094d140d6a4`.
- Static output:
  `3bbc9f09560adcf76d1bb1aafbc8368d03cbcee267190331f67f9d16c333880f`.

These supersede earlier local hashes and identify uncommitted inputs/output.
Reduced-motion and JavaScript-disabled browser checks remain explicitly waived;
neither is reported as passing. No Git staging, commit, push, hosted CI, Pages
publication or DNS change was performed. Tasks 7.1–7.4 remain open; the change
stays active at 31/35 tasks complete.

### Header and homepage review refinements — 2026-10-02

Applied the owner's six browser comments: removed the duplicate wordmark beside
its logo; added Home as the first desktop/mobile navigation entry with exact
homepage matching; replaced the two-language toggle with a native dropdown of
named language links; widened search to 200 px on desktop and a separate full
row on small screens; used sun/moon SVG theme icons; removed decorative numbering
from the homepage capability strip, section labels and integration cards.
The language list is declared centrally and retains corresponding-page fragments.

- Astro check passed with zero diagnostics; all 17 existing tests passed.
  Production build/output checks passed: 35 HTML pages, 111 files.
- Browser inspection covered both languages' homepages and MBA guides at
  375/768/1440 in light/dark themes (24 cases), with no horizontal page overflow.
  Sun/moon visibility and active navigation matched the selected theme/route.
  Desktop and mobile screenshots confirmed the new controls and dropdown.
- Language changes preserved the MBA execution/trust section in both directions.
  Escape closed the dropdown and focused its summary. The mobile Home link
  returned from documentation to the Chinese homepage. Chinese search returned
  two documentation matches for `meshbus connect`; Escape restored trigger focus.
- Strict OpenSpec and whitespace checks passed. The development Python license
  policy passed with zero remaining findings (1,216 selected, web excluded).
  No dependencies, Git index, remote settings or deployment were changed.
- Updated input SHA-256:
  `428028c66ac8702869d37ac0a490c7fbd36a2f827052429e0a596a3520b42347`.
- Updated output SHA-256:
  `4c51e17080b81a85fc47faf382f22867861957ae14367999623890fc2cc2ebdb`.
  These supersede the bilingual build hashes using the same conventions.

The existing two browser waivers and four pending hosting tasks remain unchanged.
Temporary viewport sizing was reset, with the Chinese homepage left open.

### Homepage proportions and copy refinement — 2026-10-02

Applied the owner's next three homepage comments in both languages. The hero
illustration now stays within its column at its natural square aspect ratio,
with a 500 px desktop cap; removed the previous 112% width and negative margin.
The lead paragraph carries the platform identity, Zephyr and compatible LLEXT
loading. Removed redundant homepage eyebrow labels, illustration slogans, tiny
hero notes and card labels. Hardware/application compatibility qualifications
remain beside their relevant explanations.

The benefit strip now explains hardware choice, on-demand application loading
and reusable device capabilities, with readable supporting sentences. It stacks
on mobile rather than shrinking the copy into a narrow row.

- Astro check passed with zero diagnostics. Production build/output validation
  passed with 35 HTML pages and 111 files. No new dependencies or tests were
  needed for these layout/copy changes.
- Browser checks covered both homepages at 375/768/1162/1440 in both themes
  (16 cases): no horizontal overflow, artwork retained its square ratio and
  stayed within its container, and redundant homepage labels were absent.
  Desktop dark and mobile light screenshots were inspected. Viewport overrides
  were reset after review.
- Default development-environment license policy passed with zero remaining
  findings (1,216 selected; web excluded). Strict OpenSpec and whitespace checks
  passed. The two browser waivers and four pending hosting tasks are unchanged.
- Updated input SHA-256:
  `dd9911d8757e744f9180ddada754ddc72ca3bc063cbcfbf9b444f9ff618d6632`.
- Updated output SHA-256:
  `7a597032c4497389fa5d5b776b67804ce278228fcf1843b0bbb29a87878e7e16`.
  These supersede earlier local build identities using the same hash convention.

### Landscape artwork, linked capabilities and header order — 2026-10-02

The owner clarified that the illustration itself should have a wider composition.
Generated a transparent 1536×1024 landscape revision of the existing artwork
with the built-in image tool and saved it as `web/src/assets/mesh-network-wide.png`.
The original square asset remains available to other pages. The homepage uses
the new scene without stretching individual devices. Asset provenance and the
exact generation prompt are recorded in `web/README.md`; the new input is covered
by website workflow validation.

Replaced the full-width benefit strip with three linked cards using authored
pixel-style SVG icons and destination arrows. Cards lead to Hardware,
Applications and Platform in the active language. Main navigation now groups
beside the logo; right-side tools follow Search, Language, Theme in DOM order.

- Astro check passed with zero diagnostics and 58 declared inputs covered.
  Production build/output checks passed: 35 HTML pages and 111 output files.
- Sixteen homepage cases (English/Chinese, 375/768/1162/1440, light/dark) had no
  horizontal overflow and retained the approximately 3:2 artwork ratio.
  Desktop and mobile screenshots were inspected. All three Chinese cards were
  clicked and reached their intended destinations.
- Both MBA editions at 375/1162 retained usable header layout with no overflow;
  DOM inspection confirmed the new tool order, and search opened and closed.
- Default development-environment license policy passed with zero findings
  (1,216 selected; web excluded). Strict OpenSpec and whitespace checks passed.
  No dependency changes, staging, commits, publication or remote mutation occurred.
- Website input SHA-256:
  `507a87d14280ca5c835295662060e10735cb5e20a5ebcbef07862b199d5bfbe2`.
- Static output SHA-256:
  `837f2f01618a566d21e8f3e1d28e405bbfcbdc9a5c39657b4be751e23310f4d5`.
  These supersede earlier local hashes using the existing conventions.

Viewport overrides were reset and the Chinese homepage remains open. The two
browser waivers and four pending hosting tasks remain unchanged.

### Shared layout and full-page refinement — 2026-10-02

Implemented the approved refinement plan in both languages. Product and Starlight
pages now share `header.css`, fixed header dimensions, reserved search/theme
controls, fonts, gutters and menu behavior. The mobile document toolbar retains
the native sidebar popover and focus handling, with distinct website/document
menus. First visits use the system theme; explicit choices persist.

The homepage, platform and applications use a shared `ScrollStory` component:
native scrolling selects a diagram stage, with explicit button/keyboard selection
and a static narrow-screen layout. Sticky visuals require at least 1100×700;
IntersectionObserver gates scroll calculations. Hardware now presents three
selection checks and one consolidated next step. Product copy retains configured
hardware support, matching EDK, one foreground MBA and native-code trust limits.

Documentation has one visible H1, source attribution immediately below it,
balanced heading wrappers, contained tables/code, and task-oriented overview,
getting-started and sample pages. The title anchor and search metadata remain.
404 recovery offers both languages. Component ownership is documented in
`web/README.md`. Concurrent owner-guide edits to distribution, development and
licensing were reviewed and their Chinese catalog blocks updated; this website
work did not edit those owning guides.

Validation of the local production build:

- All 34 bilingual routes plus 404 passed the four-width (375/768/1162/1440),
  two-theme DOM layout matrix: 280 cases. No page-wide horizontal overflow,
  out-of-viewport content bounds, broken images, duplicate visible H1 or fixed
  code-block headers were found. After the final heading-wrapper adjustment,
  all 208 documentation cases passed again.
- Product/MBA header comparisons in both languages at 375/599/600/768/799/800/
  1099/1100/1151/1152/1162/1440 measured zero CSS-pixel differences in logo,
  navigation container, search, language and theme geometry. Font size and
  line-height also matched.
- Screenshots were reviewed for all 13 English documentation pages at desktop
  width and all 13 Chinese documentation pages at phone width, plus product
  pages, 404, a narrow table and active story stages. Final phone screenshots
  confirmed balanced Chinese titles and readable recovery cards.
- Search returned localized English/Chinese results and localized empty state;
  Escape restored trigger focus. Language changes retained the MBA fragment.
  Website/language menus were mutually exclusive; Escape restored focus.
  The native mobile document menu opened/closed, and mobile contents selection
  closed its menu and placed the heading below the header/toolbar. Skip-to-content
  reached `_top`. Nested Chinese 404 recovery reached `/zh-cn/docs/`.
- Story selection, forward/reverse keyboard scrolling, arrow-key layer selection,
  window resizing and narrow-screen controls worked. At 1100×700 the complete
  platform visual fit below the header; at 1100×699 it used normal flow. Narrow
  application-stage selection focused the requested article. These were in-app
  browser interactions, not a physical touch-device acceptance test.
- All 18 website tests passed. Astro check returned zero errors, warnings or
  hints and validated 58 declared workflow inputs. Final production build and
  output checks passed: 35 HTML pages, 111 files, including links/fragments,
  assets and search output. Strict OpenSpec, repository reference checks
  (including untracked website/change Markdown) and whitespace checks passed.
- The final default license check in the development Python environment passed
  with zero remaining findings: incremental selection of 17 files, six Markdown
  exemptions, and `web/` excluded under repository policy. Earlier shared-policy
  edits had expanded a previous successful scan to 1,232 files; the final result
  records the current working-tree scope. No new dependencies were added.

Final identity using the existing sorted path/NUL/bytes/NUL convention:

- Website inputs (81 files):
  `653800307582a7d1444e0abdc7b792efa48c280167f22f29bc043b978f71578e`.
- Static output (111 files):
  `4762a7d4d6d221a721c0a811a856df624ecc8bf0bdcd579640024c2c24745210`.

Reduced-motion and JavaScript-disabled browser tests remain explicitly waived;
compatible fallback code remains, and neither test is reported as passing.
No staging, commit, push, remote configuration or publication was performed by
this task. Hosting acceptance 7.1–7.4 remains open: 36/40 tasks complete. Browser
viewport overrides were reset and the Chinese homepage was left open.

### Architecture cleanup — 2026-10-03

Completed the six approved cleanup recommendations without new dependencies:

- Removed ten unused product style groups and consolidated final product styles
  under the Product layout. Search styles have one owner in `header.css`.
  The three affected stylesheets total 816 lines, down from 974; shared
  `site.css` is 101 lines, down from 645. Chinese font precedence is preserved.
- Migrated 79 inline bilingual expressions into the existing UI dictionary,
  removed 85 obsolete entries, and supplied client status/theme messages from
  their components. The dictionary contains 181 current entries. One application
  diagram accessible label now shares the existing homepage-link translation;
  visible product copy is unchanged.
- Moved search into Astro's typed script build and removed `public/search.js`.
  Browser testing caught an unresolved Vite preload marker when Astro inlined
  the dynamic Pagefind import. Processed assets now stay external via
  `assetsInlineLimit: 0`; the early theme script remains explicitly inline.
  Retained explicit Escape handling after confirming that a nonempty search
  input otherwise consumes Escape to clear itself without closing the dialog.
- Named `buildGuides` and `projectGuides` directly, preserving route/metadata
  values. Input checks scan ten unique guide sources once and report 45 unique
  paths rather than 58 repeated entries. Deployment checks allow condition
  whitespace and extra steps while retaining permissions, build dependency,
  publication condition and upload/deploy actions. They do not interpret shell
  commands; workflow review owns the effects of additional steps. The actual
  deployment workflow is unchanged.

Validation:

- All 21 tests passed, including harmless workflow edits and invalid publication
  configurations. Final Astro check: zero errors, warnings or hints across 37
  files. Production build/output validation: 35 pages and 119 files.
- Browser comparisons against the pre-cleanup baseline covered 52 route/width
  cases: selected header/content geometry, font metrics and visible page text
  matched exactly. The text comparison excluded the former inline story script,
  now emitted as a separate asset. Fresh query URLs bypassed cached preview HTML
  when checking rebuilt assets.
- The final 35-page, four-width, two-theme matrix passed all 280 cases: no
  page-wide overflow, off-screen content bounds, failed images, duplicate H1 or
  controls left disabled. Chinese and English searches returned 2 and 9 expected
  results for `meshbus connect` and `LLEXT`; localized empty/cleared states,
  keyboard shortcut, first-Escape close and focus return worked. Language
  switching preserved the MBA fragment; menu exclusion, focus and theme labels/
  persistence worked. Viewport overrides were reset with Chinese home open.
- Strict OpenSpec, tracked/untracked documentation references and whitespace
  checks passed. The default development-environment license policy passed:
  18 incrementally selected files, seven Markdown exemptions, zero remaining
  findings; `web/` remained excluded by repository policy.

Final local identity using the existing sorted path/NUL/bytes/NUL convention:

- Website inputs (80 files):
  `ed7979e453e6c54ce703d1f721a37ce1de161e52f27cb644be7cbd7334f57821`.
- Static output (119 files):
  `ab54e5bf20536fe47f980afd69480909a2cf1b4c2bbefff65b4876d98ff73092`.

The reduced-motion and JavaScript-disabled browser checks remain waived, not
passed. No staging, commit, push, dependency change or publication was performed.
Tasks 7.1–7.4 remain open; local cleanup brings progress to 40/44 complete.

### Continuous product scenes — 2026-10-03

- Replaced the three button-only diagrams with original layered SVG scenes in
  `StoryScene.astro`: platform layers separate and reassemble, devices connect
  through services, and a native MBA is built and loaded onto one host. Existing
  bilingual copy retains EDK matching, explicit installation/launch, one
  foreground application and native-code trust constraints. No external artwork,
  code, dependency or animation framework was imported.
- The existing controller now interpolates scroll progress with short holds at
  stage boundaries. Four bounded CSS values drive scene geometry. Intersection
  visibility gates frame scheduling; reverse scrolling restores earlier states.
  Explicit selection uses the same visible viewport center as native scrolling,
  including fixed-header offsets. All text remains in normal document flow;
  smaller screens retain the full overview and focus the selected explanation.
- Browser checks covered six affected routes at 375/768/1162/1440 px in both
  themes (48 cases): no horizontal overflow, disabled stage controls or missing
  steps. Reviewed platform, network and package visuals, platform keyboard
  Home/End focus and selection, pointer selection, and mobile focus transfer.
  Native forward/reverse scrolling through the network scene reached a partial
  load value of 0.472 and returned to 0 at the original position.
- Resize checks at 1099/1100 px width and 699/700 px height switched static/sticky
  layout correctly. At 1440 x 700, the selected platform diagram occupied
  y=116..667, leaving the controls and caption within the viewport. Browser
  error/warning capture was empty. Temporary viewport overrides were reset.
- All 21 website tests passed; Astro checked 38 files with zero errors, warnings
  or hints. Input/workflow validation passed for 45 distinct inputs. Production
  output validation passed for 35 pages and 119 files. Strict OpenSpec,
  tracked/untracked document references and whitespace checks passed. Default
  incremental license policy passed: 18 selected files, seven Markdown
  exemptions, zero remaining findings; `web/` is excluded under repository policy.

Identity (sorted relative path/NUL/bytes/NUL SHA-256):

- Website source subset: all 70 nonignored Git-listed tracked/untracked files
  under `web/`, excluding imported guide sources outside `web/`:
  `279f4d50769f5e83a71d71ec827ce8229f3738296523d01a441ee94f29b992f1`.
- Complete static output, 119 files with paths relative to `web/dist/`:
  `b07ae5a1dbc1d0a41846c9ed837819db201e84fdd41a8c8853d198c4fab0a4cc`.

The reduced-motion and JavaScript-disabled browser cases remain waived; their
fallback implementations remain. Physical touch-device behavior was not tested.
No staging, commit, push or publication was performed. Hosting tasks 7.1–7.4
remain open; progress is 43/47 complete.

### Homepage relationships and FoBE Studio ownership — 2026-10-03

The owner subsequently rejected this visual iteration. Its diagram and captions
were reverted below; this subsection's visual checks and identities are historical.

Replaced Home's three separate device icons with a single device boundary:
application calls/results, shared Meshbus APIs, Zephyr drivers and board resources.
A distinct radio link joins another node outside that boundary. Scroll progress
reveals the service connections, API calls and returned state; reverse scrolling
restores the earlier state. The narrow overview includes all relationships.
Existing board/profile, matching EDK and native MBA qualifications remain.
The shared product footer explicitly says “A FoBE Studio project” / “FoBE Studio
旗下项目”; retained the copyright and third-party notice links.

- All 21 website tests passed. Final Astro check: zero errors, warnings or hints
  across 38 files; 45 declared inputs validated. Production build/output checks
  passed for 35 pages and 119 files. No dependencies or frameworks were added.
- Final Home layout checks covered both languages, both themes and
  375/768/1162/1440 px widths (16 cases): no horizontal overflow, disabled
  controls or SVG labels extending outside the scene. Reviewed Chinese desktop
  and phone views and English phone/dark rendering. Raised dark-theme link
  contrast and distinguished results with a dashed return path.
- The six other bilingual product routes and 404 passed footer ownership and
  overflow checks at 375/1162 px (14 cases). Home's native scroll reached a call
  progress of 0.718, then returned to zero at the original position. A later
  result-stage check reached load=0.987 and run=0.189 with a visible return
  packet. Keyboard End selected/focused the application and reached load=run=1;
  Home returned selection/focus and all three flow values to the first step.
  Mobile selection transferred focus to its corresponding article.
- Sticky mode switched correctly at 1099/1100 px and 699/700 px. At 1440×700
  the full diagram, controls and caption occupied y=116..611. Browser warning
  and error capture was empty. Temporary viewport overrides were reset.
- Strict OpenSpec, local references in all five related Markdown files and
  whitespace checks passed. The development Python incremental license policy
  passed: 17 selected files, six Markdown exemptions, zero remaining findings;
  `web/` remained excluded under repository policy.

Identity uses sorted relative path/NUL/bytes/NUL SHA-256 against repository HEAD
`62210b2848e112190bbed26502c2a7c402731961` plus the local working tree:

- Website source subset (70 nonignored Git-listed files under `web/`, excluding
  imported guide sources outside `web/`):
  `2f9a81fa0e9ecfe2eaccdebfa9dbb47568bf822419fd8d2cbb69bf07caca4847`.
- Complete static output (119 files, relative to `web/dist/`):
  `c4d7a0581844f4e695a04d6db28b0b8522d9464c69b277269013273af4d6abe6`.

Reduced-motion and JavaScript-disabled browser checks remain waived, not passed.
Physical touch-device behavior was not tested. No staging, commit, push or
publication was performed. Hosting tasks 7.1–7.4 remain open: 46/50 complete.

### Owner-requested visual rollback — 2026-10-03

The owner judged the replacement relationship diagram worse than the previous
version and requested withdrawal. Restored the previous SVG scene, stylesheet,
Home captions and component documentation byte-for-byte from the pre-edit local
snapshots. Removed the replacement diagram's translations and reinstated the
original captions. Retained the separately requested FoBE Studio project identity
in the bilingual footer. The previous subsection records the rejected iteration;
its visual acceptance and build identities do not describe the current output.

- All 21 website tests passed. Astro check returned zero errors, warnings or
  hints across 38 files; input/workflow checks validated 45 inputs. Production
  build/output validation passed for 35 pages and 119 files.
- Browser inspection of both homepages at 375/1162 px confirmed the original
  board/application scene, original localized captions, absence of the rejected
  device-boundary diagram, no horizontal overflow and retained FoBE Studio
  footer identity. Reviewed the restored Chinese desktop scene and refreshed
  the open preview. Temporary viewport overrides were reset.
- Strict OpenSpec validation and development Python incremental license policy
  passed: 17 selected files, six Markdown exemptions, zero remaining findings;
  `web/` remains excluded. Whitespace and affected Markdown references passed.

Current identity (sorted relative path/NUL/bytes/NUL SHA-256):

- Website source subset, 70 nonignored Git-listed files under `web/` (excluding
  imported guides outside `web/`):
  `caa9bcc7d50ab967b48360be9245e1740d054c39dfa8a399399c2ddff7cefdf8`.
- Complete static output, 119 files relative to `web/dist/`:
  `9973a968c5797192bf068417df06a76a4e212bf9898694ee05b305779b101cda`.

No staging, commit, push or publication occurred. Hosting acceptance remains
open; the two existing browser waivers remain unchanged. Progress: 46/50.
