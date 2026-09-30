# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Resolve board/profile owners and local preprocessor include consumers."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re


import impact


def owner(path, root):
    parts = Path(path).parts
    if len(parts) >= 4 and parts[0] == 'boards':
        directory = root.joinpath(*parts[:3])
        if (directory / 'board.yml').is_file():
            return 'board', parts[2]
    if len(parts) >= 6 and parts[:3] == ('apps', 'meshbus', 'boards'):
        directory = root.joinpath(*parts[:5])
        if any(not p.stem.endswith('_mcuboot') for p in directory.glob('*.conf')):
            return 'product', parts[4]
    return None


def resolve(path, root):
    if path == 'apps/meshbus/VERSION':
        return dict(products=None, boards=[], roots=[])
    if not path.startswith(('boards/', 'apps/meshbus/boards/')):
        return None
    changed = Path(path)
    # Arbitrary build/Kconfig scripts can have consumers outside their directory.
    if changed.suffix not in ('.dts', '.dtsi', '.overlay', '.conf', '.yaml') and not changed.name.endswith('_defconfig'):
        return None
    files = [p for folder in ('boards', 'dts', 'include', 'apps', 'tests', 'samples', 'subsys', 'drivers')
             for p in (root / folder).rglob('*')
             if p.is_file() and p.suffix in ('.dts', '.dtsi', '.overlay', '.h', '.c', '.cpp')]
    reverse = defaultdict(set)
    by_name = defaultdict(set)
    for file in files:
        by_name[file.name].add(file.resolve())
    for file in files:
        for include in re.findall(r'^\s*#\s*include\s*["<]([^">]+)[">]', file.read_text(), re.M):
            targets = {file.parent / include, root / include, root / 'dts' / include,
                       root / 'include' / include} | by_name[Path(include).name]
            for target in targets:
                reverse[target.resolve()].add(file.resolve())
    affected, pending = set(), [(root / path).resolve()]
    while pending:
        item = pending.pop()
        if item in affected:
            continue
        affected.add(item)
        pending.extend(reverse[item])
    boards, products, roots = set(), set(), set()
    for file in affected:
        relative = file.relative_to(root.resolve()).as_posix()
        matched = owner(relative, root)
        if matched:
            kind, name = matched
            if kind == 'board':
                boards.add(name)
                if any((root / 'apps/meshbus/boards').glob(f'*/{name}/*.conf')):
                    products.add(name)
            else:
                products.add(name)
        elif relative.startswith(('tests/', 'samples/')):
            roots.add(impact.direct_root(relative, root))
        elif relative != path:
            # A consumer outside a known board/profile/test is shared firmware.
            return None
    if not boards and not products and not roots:
        return None
    return dict(products=sorted(products), boards=sorted(boards), roots=sorted(roots))


def board_tests(boards, root):
    """Retain metadata's existing integration configurations for affected boards."""
    import yaml
    roots, platforms = set(), set()
    for folder, filename in (('tests', 'testcase.yaml'), ('samples', 'sample.yaml')):
        for metadata in (root / folder).rglob(filename):
            data = yaml.safe_load(metadata.read_text())
            common = data.get('common', {})
            for config in data['tests'].values():
                settings = common | (config or {})
                for platform in settings.get('integration_platforms', []):
                    if platform.split('/')[0].split('@')[0] in boards:
                        roots.add(metadata.parent.relative_to(root).as_posix())
                        platforms.add(platform)
    return sorted(roots), sorted(platforms)


def filter_products(matrix, names):
    if names is None:
        return matrix
    missing = set(names) - {p['id'] for p in matrix['include']}
    if missing:
        raise ValueError(f'product owners missing from discovered inventory: {sorted(missing)}')
    return {'include': [p for p in matrix['include'] if p['id'] in names]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--matrix', type=Path, required=True)
    args = parser.parse_args()
    matrix = filter_products(json.loads(args.matrix.read_text()),
                             json.loads(args.plan.read_text()).get('product_names'))
    args.matrix.write_text(json.dumps(matrix) + '\n')
