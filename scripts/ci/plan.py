# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Stateless change selection; Twister and product metadata own build matrices."""
import argparse
import json
import os
from pathlib import Path
import subprocess

import impact
import product_impact

ROOT = Path(__file__).resolve().parents[2]


def openspec_input(path):
    return path in ('package.json', 'package-lock.json', '.agents/skills/.openspec-target',
                    '.agents/skills/OPENSPEC-LICENSE') or path.startswith(
                        ('openspec/', '.agents/skills/openspec-'))


def category(path):
    # The dedicated web workflow builds the static site and its content inputs.
    if path.startswith('web/') or path == '.github/workflows/web.yml':
        return 'docs'
    # OpenSpec and the root npm package are development workflow inputs only.
    # Source checks validate them without restoring a firmware workspace.
    if openspec_input(path):
        return 'docs'
    if Path(path).name == '.gitignore' or path in (
            '.editorconfig', '.clang-format', '.gitlint', '.checkpatch.conf'):
        return 'docs'
    if path.endswith(('.md', '.rst')):
        return 'docs'
    # EDK validation embeds LICENSE; export and Rust tests consume both texts.
    if path in ('LICENSE', 'LICENSES/Apache-2.0.txt'):
        return 'cli'
    if path in ('REUSE.toml', '.github/license-policy.toml') or path.startswith(
            ('LICENSE', 'docs/licensing/')):
        return 'docs'
    if path in ('.github/actions/cargo-cache/action.yml', 'scripts/ci/cargo-xwin.sh',
                'scripts/ci/cli_workspace.py', 'scripts/ci/artifact.py'):
        return 'cli'
    if path in ('.github/workflows/alpha-release.yml', '.github/workflows/candidates.yml',
                'scripts/ci/alpha.py', 'scripts/ci/publish.py', 'scripts/ci/baseline.py',
                'scripts/ci/promotion.py'):
        return 'release'
    # Unit tests alone cannot qualify changes to the real execution/selection path.
    if path in ('scripts/ci/plan.py', 'scripts/ci/impact.py', 'scripts/ci/test_plan.py',
                'scripts/ci/workspace.py', 'scripts/ci/run.py', 'scripts/ci/checks.py',
                'scripts/ci/product_impact.py', 'scripts/ci/timings.py', 'scripts/ci/benchmark.py'):
        return 'shared'
    if path.startswith('scripts/ci/') and path.endswith('.py'):
        return 'tools'
    if path in ('west.yml', 'CMakeLists.txt', 'Kconfig') or path.startswith(
            ('.github/', 'scripts/ci/', 'zephyr/', 'cmake/', 'modules/')):
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
    if path.startswith(('include/', 'subsys/', 'drivers/', 'lib/', 'dts/', 'boards/', 'apps/')):
        return 'firmware'
    return 'unknown'


def select(paths, full=False):
    categories = {category(p) for p in paths}
    full = full or bool(categories & {'shared', 'unknown'}) or not paths
    scoped, board_names, product_names, consumer_roots = set(), set(), set(), set()
    all_products = full or bool(categories & {'release'})
    requests = [dict(products=None, profiles=['default'])] if all_products else []
    for path in paths:
        if category(path) != 'firmware':
            continue
        scope = product_impact.resolve(path, ROOT)
        if full:
            if scope and scope.get('profiles') in (['dev'], ['prod']):
                requests.append(dict(products=scope['products'], profiles=scope['profiles']))
            continue
        if scope is None:
            all_products = True
            requests.append(dict(products=None, profiles=['default']))
            continue
        board_roots, _ = product_impact.board_tests(scope['boards'], ROOT) if scope['boards'] else ([], [])
        if not board_roots and scope['products'] == [] and not scope['roots']:
            all_products = True
            requests.append(dict(products=None, profiles=['default']))
            continue
        scoped.add(path)
        board_names.update(scope['boards'])
        consumer_roots.update(scope['roots'])
        if scope['products'] is None:
            all_products = True
        else:
            product_names.update(scope['products'])
        if scope['products'] is None or scope['products']:
            requests.append(dict(products=scope['products'], profiles=scope.get('profiles', ['default'])))
    board_roots, platforms = product_impact.board_tests(board_names, ROOT) if board_names else ([], [])
    # Samples without Twister metadata (including MBA apps) may be product
    # fixtures; retain product validation when their owner cannot be resolved.
    unowned_sample = any(category(p) == 'samples' and impact.direct_root(p, ROOT) == 'samples'
                        for p in paths)
    all_products = all_products or unowned_sample
    if unowned_sample:
        requests.append(dict(products=None, profiles=['default']))
    products = all_products or bool(product_names)
    cli = full or bool(categories & {'cli', 'release'})
    host = full or bool(categories & {'cli', 'release', 'tools'})
    selection = (dict(test_roots=['tests'], compile_roots=['tests', 'samples'],
                      sdk_selection=dict(components=[], fallback_paths=[], full=True)) if full else
                 impact.select([p for p in paths if p not in scoped and
                                category(p) in ('firmware', 'tests', 'samples')], ROOT))
    selection['test_roots'] = impact.compact(set(selection['test_roots']) |
                                             {r for r in consumer_roots if r.startswith('tests')})
    unrestricted = (selection['compile_roots'] if full else
                    impact.compact(set(selection['compile_roots']) | consumer_roots))
    if not full:
        selection['compile_roots'] = impact.compact(set(unrestricted) | set(board_roots))
    sdk = bool(selection['test_roots'] or selection['compile_roots'])
    extended_roots = {impact.direct_root(p, ROOT) for p in paths if category(p) == 'tests'}
    if any(p.startswith('subsys/settings/') for p in paths):
        extended_roots.add('tests/subsys/settings/performance')
    checkpatch = full or any(p.endswith(('.c', '.h', '.cpp', '.hpp')) for p in paths)
    # Full runs assess current findings, dependency changes are expanded above.
    audit = full or any(p in ('scripts/meshbus/Cargo.lock', 'scripts/meshbus/Cargo.toml') for p in paths)
    linux = ['x86_64-unknown-linux-gnu', 'x86_64-pc-windows-msvc'] if cli else []
    mac = [{'target': 'aarch64-apple-darwin', 'runner': 'macos-15'}] if cli else []
    if full and cli:
        linux += ['aarch64-unknown-linux-gnu', 'aarch64-pc-windows-msvc']
        mac += [{'target': 'x86_64-apple-darwin', 'runner': 'macos-15'}]
    runners = {'x86_64-unknown-linux-gnu': 'ubuntu-24.04',
               'aarch64-unknown-linux-gnu': 'ubuntu-24.04-arm',
               'x86_64-pc-windows-msvc': 'windows-2025',
               'aarch64-pc-windows-msvc': 'windows-11-arm'}
    return dict(full=full, categories=sorted(categories), host=host, rust=cli,
                sdk=sdk, products=products, product_builds=products, product_requests=requests,
                openspec=full or any(openspec_input(p) for p in paths),
                cli=cli, audit=audit, west_audit=full,
                fonts=full or products, checkpatch=checkpatch, cli_profile='release' if full else 'ci',
                workspace=sdk or products or cli or audit or checkpatch,
                heavy=host or sdk or products or cli or audit or checkpatch,
                **selection, extended_roots=impact.compact(extended_roots),
                product_names=None if all_products else sorted(product_names),
                board_compile_roots=board_roots, board_platforms=platforms,
                unrestricted_compile_roots=unrestricted,
                linux_targets=linux, mac_targets=mac,
                native_targets=[{'target': t, 'runner': runners[t]} for t in linux] +
                [dict(item, runner='macos-15-intel' if item['target'].startswith('x86_64')
                      else 'macos-15') for item in mac])


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
                # Actions scalar outputs are passed directly to shell arguments.
                encoded = value if isinstance(value, str) else json.dumps(value, separators=(",", ":"))
                stream.write(f'{key}={encoded}\n')
    if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(summary, 'a') as stream:
            stream.write('### Validation scope\n\n```json\n' + json.dumps(plan, indent=2) + '\n```\n')


if __name__ == '__main__':
    main()
