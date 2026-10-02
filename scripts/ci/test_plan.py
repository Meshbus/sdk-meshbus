# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Freeze Twister instances once; partition disjoint plans and verify coverage."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import statistics
import subprocess

def filter_extended(report, selection, root):
    """Keep extended cases only when requested by full or direct selection."""
    if selection['full']:
        return report
    import yaml
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


def partition(report, count=None, weights=None):
    suites = active(report)
    if not suites:
        return []
    # Environment preparation costs more than a handful of warm builds.
    # Preserve full-run parallelism while batching small impact selections.
    if count is None:
        count = min(4, math.ceil(len(suites) / 12))
    identities = [key(s) for s in suites]
    if len(set(identities)) != len(identities):
        raise ValueError('duplicate Twister instances')
    # Round-robin distributes adjacent scenarios from expensive test families.
    ordered = sorted(suites, key=key)
    count = min(count, len(ordered))
    if count < 1:
        raise ValueError('Twister shard count must be positive')
    groups = [ordered[i::count] for i in range(count)]
    valid = {identity: value for identity, value in (weights or {}).items()
             if isinstance(value, (int, float)) and not isinstance(value, bool)
             and math.isfinite(value) and value > 0}
    known = {identity: value for identity, value in valid.items() if identity in identities}
    if known:
        default = statistics.median(valid.values())
        def cost(suite):
            return known.get(key(suite), default)
        balanced, totals = [[] for _ in range(count)], [0.0] * count
        for suite in sorted(ordered, key=lambda s: (-cost(s), key(s))):
            slot = min(range(count), key=lambda i: (totals[i], i))
            balanced[slot].append(suite)
            totals[slot] += cost(suite)
        previous = max(sum(cost(s) for s in group) for group in groups)
        if max(totals) < previous:
            groups = [sorted(group, key=key) for group in balanced]
    return [dict(report, testsuites=group) for group in groups]


def job_tasks(matrix):
    """Normalize old snapshots while requiring unique, nonempty execution jobs."""
    jobs = matrix['include']
    if not jobs or matrix.get('schema', 1) not in (1, 2):
        raise ValueError('empty or unsupported Twister matrix')
    ids, names, tasks = set(), set(), set()
    normalized = []
    for row in jobs:
        legacy = matrix.get('schema', 1) == 1
        job_id = f"{row['layer']}-{row['shard']}" if legacy else row['id']
        selected = [{'layer': row['layer'], 'shard': row['shard']}] if legacy else row['tasks']
        if not isinstance(job_id, str) or not job_id or job_id in ids or row['name'] in names or not selected:
            raise ValueError('duplicate or invalid Twister job')
        ids.add(job_id)
        names.add(row['name'])
        for task in selected:
            identity = (task['layer'], task['shard'])
            if (identity[0] not in ('runtime', 'compile') or type(identity[1]) is not int
                    or identity[1] < 0 or identity in tasks):
                raise ValueError('duplicate or invalid Twister task')
            tasks.add(identity)
        normalized.append(dict(id=job_id, name=row['name'], tasks=selected))
    return normalized


def verify_snapshot(snapshot, reports):
    for layer in ('runtime', 'compile'):
        verify(json.loads((snapshot / f'{layer}.json').read_text()),
               list(reports.glob(f'**/{layer}-*/twister.json')), layer == 'runtime')
    if (snapshot / 'shards.json').is_file():
        expected_tasks = {(task['layer'], task['shard'])
                          for job in job_tasks(json.loads((snapshot / 'shards.json').read_text()))
                          for task in job['tasks']}
        found = Counter()
        for layer in ('runtime', 'compile'):
            for report in reports.glob(f'**/{layer}-*/twister.json'):
                found[(layer, int(report.parent.name.removeprefix(layer + '-')))] += 1
        if found != Counter({task: 1 for task in expected_tasks}):
            raise ValueError('Twister task report coverage mismatch')


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
    import timings
    scheduler = selection.get('scheduler', 'balanced')
    if scheduler not in ('legacy', 'balanced') or (scheduler == 'legacy' and not selection.get('benchmark')):
        raise ValueError('legacy scheduling requires an isolated benchmark')
    image = (snapshot / 'image.txt').read_text().strip() if (snapshot / 'image.txt').is_file() else None
    history = timings.load_history(snapshot / 'history.json', image)
    matrix = []
    inventories = {}
    runtime_keys = set()
    for layer, label in (('runtime', 'Run'), ('compile', 'Build')):
        roots = (selection['test_roots'] if layer == 'runtime' else
                 selection.get('unrestricted_compile_roots', selection['compile_roots']))

        def discover(selected_roots, filename, platforms=()):
            # Without -T Twister searches its default tree, not an empty set.
            if not selected_roots:
                return {'testsuites': []}
            command = ['west', 'twister', '--integration', '--enable-slow', '--report-filtered', '-j', '2',
                       '-O', str(snapshot.parent / 'ci-output' / f'plan-{filename}'),
                       '--save-tests', str(snapshot / f'{filename}.json')]
            for root in selected_roots:
                command += ['-T', str(workspace / 'meshbus' / root)]
            for platform in platforms:
                command += ['-p', platform]
            command += ['--filter', 'runnable'] if layer == 'runtime' else ['--build-only']
            subprocess.run(command, cwd=workspace, check=True)
            return json.loads((snapshot / f'{filename}.json').read_text())

        report = discover(roots, layer)
        if layer == 'compile' and selection.get('board_compile_roots'):
            platforms = selection['board_platforms']
            boards = discover(selection['board_compile_roots'], 'board-compile', platforms)
            for discovered in (report, boards):
                identities = [key(s) for s in active(discovered)]
                if len(identities) != len(set(identities)):
                    raise ValueError('duplicate Twister instances')
            # Merge with ordinary component coverage without restricting its platforms.
            suites = {key(s): s for s in report['testsuites']}
            for suite in active(boards):
                if suite['platform'] in platforms:
                    suites[key(suite)] = suite
            report['testsuites'] = list(suites.values())
        report = filter_extended(report, selection, workspace / 'meshbus')
        if layer == 'runtime':
            runtime_keys = {key(s) for s in active(report)}
        else:
            # Runtime already compiles these exact instances using the same
            # sources, image, toolchain, scenario settings and frozen manifest.
            report['testsuites'] = [s for s in report['testsuites'] if key(s) not in runtime_keys]
        (snapshot / f'{layer}.json').write_text(json.dumps(report, indent=2) + '\n')
        inventories[layer] = report
        weights = timings.layer_weights(history, layer) if scheduler == 'balanced' else {}
        shards = partition(report, weights=weights)
        for index, shard in enumerate(shards):
            (snapshot / f'{layer}-{index}.json').write_text(json.dumps(shard, indent=2) + '\n')
            matrix.append({'id': f'{layer}-{index}', 'name': f'Twister {label} ({index + 1})',
                           'tasks': [{'layer': layer, 'shard': index}]})
    if not matrix:
        raise ValueError('selection contains no runnable or buildable Twister instances')
    if (scheduler == 'balanced' and len(matrix) == 2
            and {row['tasks'][0]['layer'] for row in matrix} == {'runtime', 'compile'}
            and sum(len(active(report)) for report in inventories.values()) <= 12):
        matrix = [{'id': 'mixed-0', 'name': 'Twister Run + Build (1)',
                   'tasks': [row['tasks'][0] for row in matrix]}]
    result = {'schema': 2, 'include': matrix}
    job_tasks(result)
    (snapshot / 'shards.json').write_text(json.dumps(result, indent=2) + '\n')
    inventory = timings.inventory(inventories)
    (snapshot / 'twister-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    scheduling = timings.predictions(snapshot, result, history)
    scheduling.update(scheduler=scheduler, history_sources=history.get('sources', []),
                      history_sha256=timings.digest(timings.encoded(history.get('weights', []))),
                      instance_counts={layer: len(active(report)) for layer, report in inventories.items()})
    (snapshot / 'scheduling.json').write_text(json.dumps(scheduling, indent=2) + '\n')
    if selection.get('benchmark'):
        import benchmark
        benchmark.verify_inventory(selection, inventory)


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
        verify_snapshot(args.snapshot, args.reports)
