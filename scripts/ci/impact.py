# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Select metadata roots using reviewed component and consumer relationships."""
from fnmatch import fnmatchcase
from pathlib import Path
import tomllib

ROOT = Path(__file__).resolve().parents[2]


def compact(roots):
    """An ancestor -T already discovers all metadata below it."""
    return sorted(root for root in set(roots)
                  if not any(parent.as_posix() in roots for parent in Path(root).parents))


def direct_root(name, root):
    boundary = Path(name).parts[0]
    for directory in Path(name).parents:
        if any((root / directory / metadata).is_file()
               for metadata in ('testcase.yaml', 'sample.yaml')):
            return directory.as_posix()
        if directory == Path(boundary):
            # Deleted suites and shared helpers have no reliable local owner.
            return boundary
    raise ValueError(f'not a test/sample path: {name}')


def load_policy(root):
    policy = tomllib.loads((root / '.github/ci-impact.toml').read_text())
    for name, component in policy['components'].items():
        if not component['paths'] or not component['roots']:
            raise ValueError(f'empty impact rule: {name}')
        for consumer in component.get('consumers', []):
            if consumer not in policy['components']:
                raise ValueError(f'unknown impact consumer: {name} -> {consumer}')
        for target in component['roots']:
            path = Path(target)
            if path.is_absolute() or '..' in path.parts or path.parts[0] not in ('tests', 'samples'):
                raise ValueError(f'invalid impact root: {target}')
            directory = root / path
            if not any(directory.rglob('testcase.yaml')) and not any(directory.rglob('sample.yaml')):
                raise ValueError(f'impact root has no test metadata: {target}')
    return policy


def select(paths, root=ROOT):
    policy = load_policy(root)
    roots, components, fallback = set(), set(), []
    for name in paths:
        if name.startswith(('tests/', 'samples/')):
            target = direct_root(name, root)
            roots.add(target)
            if target in ('tests', 'samples'):
                fallback.append(name)
                if target == 'samples':
                    # Non-Twister samples can be inputs to SDK test fixtures.
                    roots.add('tests')
            continue
        # Build/configuration edits can change dependencies and enabled tests.
        if (Path(name).name.startswith('Kconfig') or Path(name).suffix == '.cmake'
                or Path(name).name == 'CMakeLists.txt'
                or any(fnmatchcase(name, pattern) for pattern in policy['shared'])):
            roots.update(('tests', 'samples'))
            fallback.append(name)
            continue
        matches = [key for key, rule in policy['components'].items()
                   if any(fnmatchcase(name, pattern) for pattern in rule['paths'])]
        if not matches:
            roots.update(('tests', 'samples'))
            fallback.append(name)
        for key in matches:
            components.add(key)
    # Follow reviewed reverse dependencies, including conditional consumers.
    # Cycles are normal (e.g. FS/LLEXT); each component is visited only once.
    pending, affected = list(components), set()
    while pending:
        key = pending.pop()
        if key in affected:
            continue
        affected.add(key)
        rule = policy['components'][key]
        roots.update(rule['roots'])
        pending.extend(rule.get('consumers', []))
    return dict(test_roots=compact({r for r in roots if r.startswith('tests')}),
                compile_roots=compact(roots),
                sdk_selection=dict(components=sorted(components), consumers=sorted(affected - components),
                                   fallback_paths=sorted(fallback)))
