# Tasks

## 1. Select license scan inputs

- [x] 1.1 Add real Git/REUSE regressions for local changes, ignored output, renames, complete scans and website exclusion; demonstrate the current scanner fails the new selection contract.
- [x] 1.2 Implement incremental and complete selection with licensing context, automatic expansion and explicit report scope; pass focused regressions and update the license-check guide.

## 2. Integrate contributor and CI workflows

- [x] 2.1 Route daily CI comparison bases and scheduled/manual complete checks to the scanner; verify invocation regressions and workflow syntax.
- [x] 2.2 Update Agent, OpenSpec context and contribution/licensing guidance for incremental checks and temporary website exclusion; review consistency and preserve existing notices.

## 3. Verify the integrated change

- [x] 3.1 Run affected CI tests, Python style, strict OpenSpec validation, incremental and complete repository scans and whitespace checks; record actual scope and results below.

## Validation

Verified on 2026-10-02 in the development Python environment, against the current
working tree based on `74e45aa`. Existing website and release-document changes
were preserved; no repository staging, commits or pushes were performed.

- Red: the new local-selection regression failed against the previous scanner
  because unrelated source and website findings blocked valid changes. The
  CI-base regression also failed before argument forwarding was implemented.
- `python -m unittest discover -s scripts/ci/tests -p test_license_policy.py`:
  26 tests passed, including real Git/REUSE tests for incremental selection,
  staged and untracked inputs, ignored nested output, renames/deletions, missing
  bases, attribution/sidecar context, removed license text, strict source
  findings and website exclusion in both modes.
- `python -m unittest discover -s scripts/ci/tests -p test_ci.py`:
  21 tests passed, including exact CI-base forwarding and complete audit routing.
- Ruff (`E4,E7,E9,F`) passed for the four changed Python files. The documentation
  checker passed local links/includes; it does not validate external URLs/anchors.
- Actionlint passed `validation.yml`. YAML parsing, `bash -n`, and execution of
  the license step with stubbed Python under the workflow's Bash options passed
  for both daily and complete modes.
- Strict OpenSpec validation passed all 7 current items. `git diff --check` passed.
- The default local scan correctly expanded to a complete audit because shared
  licensing inputs are modified in this worktree. It passed with 1,216 selected
  files and zero remaining policy findings. An explicit `--full` scan also
  passed: 1,208 REUSE-covered files, 66 metadata exemptions, zero remaining
  policy findings. Both excluded `web/`; the raw file report was checked for
  absence of website paths. Incremental behavior passed in real temporary Git
  repositories, including an empty selection and unchanged metadata debt.

These results establish local repository-policy behavior. Hosted CI and release
qualification were not run. Existing release material checks were not changed.
