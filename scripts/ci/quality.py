# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Independent strict quality gates, with redacted reports retained on failure."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT.parent / 'ci-output'


def run(*args, cwd=ROOT, **kwargs):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True, **kwargs)


def comparison_base(base):
    requested = os.environ.get('DIFF_BASE') or base or 'HEAD^'
    # Tag creation/correction can lack a resolvable previous ref. Check the
    # target commit's patch as in manual CI; a new branch stays conservative.
    candidates = [requested]
    if os.environ.get('GITHUB_REF_TYPE') == 'tag' and requested != 'HEAD^':
        candidates.append('HEAD^')
    for candidate in candidates:
        if candidate == '0' * 40:
            continue
        resolved = subprocess.run(
            ['git', 'rev-parse', '--verify', '--quiet', '--end-of-options',
             f'{candidate}^{{commit}}'], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if resolved.returncode == 0:
            return resolved.stdout.strip()
    print('No comparison commit is available; checking the complete current tree.', file=sys.stderr)
    return None


def quality(kind, base, current=False):
    OUT.mkdir(exist_ok=True)
    if kind == 'licenses':
        reference = base or os.environ.get('DIFF_BASE')
        selection = ['--full'] if current else ['--base', reference] if reference else []
        run(sys.executable, 'scripts/ci/license_policy.py', '--output', OUT, *selection)
        return
    diff_base = None if current else comparison_base(base)
    if kind == 'fonts':
        run(sys.executable, ROOT.parent / 'modules/lib/u8g2/scripts/font_inventory.py', '--check')
    elif kind == 'secrets':
        run('gitleaks', 'dir', '.', '--redact', '--report-format', 'json',
            '--report-path', OUT / 'secrets-tree.json')
        if base:
            run('gitleaks', 'git', '.', '--redact', '--log-opts', f'{base}..HEAD',
                '--report-format', 'json', '--report-path', OUT / 'secrets-commits.json')
    elif kind in ('style', 'checkpatch'):
        if kind == 'style':
            run('actionlint', '-color')
        if base and kind == 'style':
            run(sys.executable, 'scripts/ci/checks.py', 'commits', base)
        if diff_base is None:
            diff_base = subprocess.check_output(
                ['git', 'hash-object', '-w', '-t', 'tree', '--stdin'],
                input='', cwd=ROOT, text=True).strip()
        changed = subprocess.check_output(['git', 'diff', '--name-only', '--diff-filter=ACMR',
                                           f'{diff_base}..HEAD'], cwd=ROOT, text=True).splitlines()
        python = [p for p in changed if p.endswith('.py')]
        if python and kind == 'style':
            run('ruff', 'check', '--select', 'E4,E7,E9,F', *python)
        cfiles = [p for p in changed if p.endswith(('.c', '.h', '.cpp', '.hpp'))]
        if cfiles and kind == 'checkpatch':
            patch = subprocess.check_output(['git', 'diff', f'{diff_base}..HEAD', '--', *cfiles], cwd=ROOT)
            run('perl', ROOT.parent / 'zephyr/scripts/checkpatch.pl', '--no-tree', '-', input=patch)
    elif kind == 'west-vulnerabilities':
        run(sys.executable, 'scripts/ci/west_vulnerabilities.py', '--workspace', ROOT.parent,
            '--output', OUT, '--base', diff_base or '0' * 40)
    elif kind == 'vulnerabilities':
        # Download once, then use precisely the same advisory snapshot for both trees.
        run('trivy', 'image', '--download-db-only')
        scan = ['trivy', 'fs', '--scanners', 'vuln', '--skip-db-update', '--skip-java-db-update',
                '--format', 'json']
        run(*scan, '--output', OUT / 'vulnerabilities-current.json', 'scripts/meshbus')
        base_dir = ROOT.parent / 'vulnerability-base'
        if diff_base is None:
            (OUT / 'vulnerabilities-base.json').write_text('{}\n')
        else:
            run('git', 'worktree', 'add', '--detach', base_dir, diff_base)
            run(*scan, '--output', OUT / 'vulnerabilities-base.json', 'scripts/meshbus', cwd=base_dir)
        run(sys.executable, 'scripts/ci/vulnerabilities.py', OUT / 'vulnerabilities-current.json',
            OUT / 'vulnerabilities-base.json', OUT / 'vulnerabilities-delta.json')
    elif kind == 'environment':
        run('trivy', 'image', '--download-db-only')
        run('trivy', 'rootfs', '--scanners', 'vuln', '--skip-db-update', '--format', 'json',
            '--skip-dirs', '/work', '--skip-dirs', '/__w', '--skip-dirs', '/github',
            '--skip-dirs', '/opt/west-workspace',
            '--skip-dirs', '/proc', '--skip-dirs', '/sys', '--skip-dirs', '/dev',
            '--output', OUT / 'environment-vulnerabilities.json', '/')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('licenses', 'fonts', 'secrets', 'style', 'checkpatch',
                                       'vulnerabilities', 'west-vulnerabilities', 'environment'))
    parser.add_argument('--base', default=os.environ.get('PR_BASE') or None)
    parser.add_argument('--current', action='store_true', help='Assess all current findings against an empty baseline')
    args = parser.parse_args()
    quality(args.kind, args.base, args.current)
