# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Freeze Twister instances once; partition disjoint plans and verify coverage."""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess

import yaml


def filter_extended(report, selection, root):
    """Keep extended cases only when requested by full or direct selection."""
    if selection['full']:
        return report
    extended_roots = [root / path for path in selection.get('extended_roots', [])]
    excluded = set()
    for metadata in (root / 'tests').rglob('testcase.yaml'):
        if any(metadata.is_relative_to(path) for path in extended_roots):
            continue
        data = yaml.safe_load(metadata.read_text())
        common_tags = set(data.get('common', {}).get('tags', []))
        for name, config in data['tests'].items():
            tags = common_tags | set((config or {}).get('tags', []))
            if tags & {'shuffle', 'performance'}:
                excluded.add(name)
    return dict(report, testsuites=[s for s in report['testsuites']
                                   if s['name'].rsplit('/', 1)[-1] not in excluded])


def key(suite):
    # A scenario uniquely owns its Kconfig/overlay/extra_args in this source tree.
    return (suite['name'], suite['platform'], suite['toolchain'])


def active(report):
    return [s for s in report['testsuites'] if s.get('status') not in ('filtered', 'skipped')]


def partition(report, count=4):
    suites = active(report)
    if not suites:
        return []
    identities = [key(s) for s in suites]
    if len(set(identities)) != len(identities):
        raise ValueError('duplicate Twister instances')
    # Round-robin distributes adjacent scenarios from expensive test families.
    ordered = sorted(suites, key=key)
    return [dict(report, testsuites=ordered[i::min(count, len(ordered))])
            for i in range(min(count, len(ordered)))]


def verify(expected, reports, runnable):
    from checks import report_twister
    wanted = Counter(key(s) for s in active(expected))
    actual = Counter()
    for path in reports:
        report_twister(path, runnable)
        suites = json.loads(path.read_text())['testsuites']
        if any(s.get('status') in ('filtered', 'skipped') for s in suites):
            raise ValueError('selected Twister instance was unexpectedly skipped')
        actual.update(key(s) for s in suites)
    if actual != wanted:
        raise ValueError(f'Twister coverage mismatch: missing={wanted - actual}, extra={actual - wanted}')


def generate(workspace, snapshot):
    selection = json.loads((snapshot / 'plan.json').read_text())
    matrix = []
    runtime_keys = set()
    for layer, label in (('runtime', 'Run'), ('compile', 'Build')):
        roots = selection['test_roots'] if layer == 'runtime' else selection['compile_roots']
        command = ['west', 'twister', '--integration', '--enable-slow', '--report-filtered', '-j', '2',
                   '-O', str(snapshot.parent / 'ci-output' / f'plan-{layer}'),
                   '--save-tests', str(snapshot / f'{layer}.json')]
        for root in roots:
            command += ['-T', str(workspace / 'meshbus' / root)]
        command += ['--filter', 'runnable'] if layer == 'runtime' else ['--build-only']
        if roots:
            subprocess.run(command, cwd=workspace, check=True)
            report = json.loads((snapshot / f'{layer}.json').read_text())
            report = filter_extended(report, selection, workspace / 'meshbus')
        else:
            # Without -T Twister searches its default tree, not an empty set.
            report = {'testsuites': []}
        if layer == 'runtime':
            runtime_keys = {key(s) for s in active(report)}
        else:
            # Runtime already compiles these exact instances using the same
            # sources, image, toolchain, scenario settings and frozen manifest.
            report['testsuites'] = [s for s in report['testsuites'] if key(s) not in runtime_keys]
        (snapshot / f'{layer}.json').write_text(json.dumps(report, indent=2) + '\n')
        shards = partition(report)
        for index, shard in enumerate(shards):
            (snapshot / f'{layer}-{index}.json').write_text(json.dumps(shard, indent=2) + '\n')
            matrix.append({'layer': layer, 'shard': index, 'name': f'Twister {label} ({index + 1})'})
    if not matrix:
        raise ValueError('selection contains no runnable or buildable Twister instances')
    (snapshot / 'shards.json').write_text(json.dumps({'include': matrix}) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('generate', 'verify'))
    parser.add_argument('--workspace', type=Path, default=Path.cwd())
    parser.add_argument('--snapshot', type=Path, default=Path('snapshot'))
    parser.add_argument('--reports', type=Path)
    args = parser.parse_args()
    if args.mode == 'generate':
        generate(args.workspace.resolve(), args.snapshot.resolve())
    else:
        for layer in ('runtime', 'compile'):
            # A single matching artifact is extracted without its artifact-name
            # directory; the producer's layer/shard directory is always present.
            verify(json.loads((args.snapshot / f'{layer}.json').read_text()),
                   list(args.reports.glob(f'**/{layer}-*/twister.json')), layer == 'runtime')
