# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Stateless change selection; Twister and product metadata own build matrices."""
import argparse
import json
import os
from pathlib import Path
import subprocess

import impact

ROOT = Path(__file__).resolve().parents[2]


def category(path):
    if path.endswith(('.md', '.rst')) and path != 'LICENSING.md' and not path.startswith('docs/licensing/'):
        return 'docs'
    if path in ('LICENSING.md', 'west.yml', 'CMakeLists.txt', 'Kconfig', 'REUSE.toml') or path.startswith(
            ('.github/', 'scripts/ci/', 'include/', 'zephyr/', 'cmake/', 'modules/',
             'LICENSE', 'docs/licensing/')):
        return 'shared'
    if path.startswith('scripts/meshbus/') or path == 'scripts/meshbus_cli.py':
        return 'cli'
    if path.startswith('scripts/release/') or path in ('scripts/board_profiles.py', 'scripts/west-commands.yml'):
        return 'release'
    if path.startswith('scripts/'):
        return 'tools'
    if path.startswith('tests/'):
        return 'tests'
    if path.startswith('samples/'):
        return 'samples'
    if path.startswith(('subsys/', 'drivers/', 'lib/', 'dts/', 'boards/', 'apps/')):
        return 'firmware'
    return 'unknown'


def select(paths, full=False):
    categories = {category(p) for p in paths}
    full = full or bool(categories & {'shared', 'unknown'}) or not paths
    sdk = full or bool(categories & {'firmware', 'tests', 'samples'})
    # Samples without Twister metadata (including MBA apps) may be product
    # fixtures; retain product validation when their owner cannot be resolved.
    unowned_sample = any(category(p) == 'samples' and impact.direct_root(p, ROOT) == 'samples'
                        for p in paths)
    products = full or unowned_sample or bool(categories & {'firmware', 'release'})
    cli = full or bool(categories & {'cli', 'release'})
    host = full or bool(categories & {'cli', 'release', 'tools'})
    selection = (dict(test_roots=['tests'], compile_roots=['tests', 'samples'],
                      sdk_selection=dict(components=[], consumers=[], fallback_paths=[], full=True)) if full else
                 impact.select([p for p in paths if category(p) in ('firmware', 'tests', 'samples')], ROOT))
    checkpatch = full or any(p.endswith(('.c', '.h', '.cpp', '.hpp')) for p in paths)
    # Full runs assess current findings, dependency changes are expanded above.
    audit = full or any(p in ('scripts/meshbus/Cargo.lock', 'scripts/meshbus/Cargo.toml') for p in paths)
    linux = ['x86_64-unknown-linux-gnu', 'x86_64-pc-windows-msvc'] if cli else []
    mac = [{'target': 'aarch64-apple-darwin', 'runner': 'macos-15'}] if cli else []
    if full and cli:
        linux += ['aarch64-unknown-linux-gnu', 'aarch64-pc-windows-msvc']
        mac += [{'target': 'x86_64-apple-darwin', 'runner': 'macos-15-intel'}]
    runners = {'x86_64-unknown-linux-gnu': 'ubuntu-24.04',
               'aarch64-unknown-linux-gnu': 'ubuntu-24.04-arm',
               'x86_64-pc-windows-msvc': 'windows-2025',
               'aarch64-pc-windows-msvc': 'windows-11-arm'}
    return dict(full=full, categories=sorted(categories), host=host, rust=cli,
                sdk=sdk, products=products, cli=cli, audit=audit, west_audit=full,
                fonts=full or products, checkpatch=checkpatch,
                workspace=sdk or products or cli or audit or checkpatch,
                heavy=host or sdk or products or cli or audit or checkpatch,
                **selection,
                linux_targets=linux, mac_targets=mac,
                native_targets=[{'target': t, 'runner': runners[t]} for t in linux] + mac)


def changed_paths(base=None, before=None):
    revision = base or before
    if not revision or revision == '0' * 40:
        return None
    try:
        sha = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options',
                                       f'{revision}^{{commit}}'], stderr=subprocess.DEVNULL).decode().strip()
        # PRs compare merge bases; pushes compare the exact previous tree.
        comparison = f'{sha}...HEAD' if base else f'{sha}..HEAD'
        data = subprocess.check_output(['git', 'diff', '--no-renames', '--name-only', '-z', comparison])
        return [p.decode() for p in data.split(b'\0') if p]
    except subprocess.CalledProcessError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base')
    parser.add_argument('--before')
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--audit-current', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = changed_paths(args.base, args.before)
    plan = select(paths or [], args.full or paths is None)
    plan.update(changed_paths=paths, audit_current=args.audit_current)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(plan, indent=2) + '\n')
    print(json.dumps(plan, indent=2))
    if output := os.environ.get('GITHUB_OUTPUT'):
        with open(output, 'a') as stream:
            for key, value in plan.items():
                stream.write(f'{key}={json.dumps(value, separators=(",", ":"))}\n')
    if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(summary, 'a') as stream:
            stream.write('### Validation scope\n\n```json\n' + json.dumps(plan, indent=2) + '\n```\n')


if __name__ == '__main__':
    main()
