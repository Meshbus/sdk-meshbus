# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Isolated fixed-case scheduling experiments, excluded from qualification."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

import timings

CASES = ('full', 'clock-small')
SCHEDULERS = ('legacy', 'balanced')


def configure(snapshot, case='', scheduler='balanced', source='', image='',
              history_artifact='', history_sha256='', inventory_sha256='', environment=None):
    env = os.environ if environment is None else environment
    if not case:
        if scheduler != 'balanced' or any((source, history_artifact, history_sha256, inventory_sha256)):
            raise ValueError('ordinary validation rejects benchmark selection and scheduler overrides')
        return None
    expected = timings.REPOSITORY + '/.github/workflows/ci-benchmark.yml@refs/heads/main'
    if (case not in CASES or scheduler not in SCHEDULERS
            or env.get('GITHUB_WORKFLOW_REF') != expected
            or env.get('GITHUB_EVENT_NAME') != 'workflow_dispatch'
            or env.get('GITHUB_REPOSITORY') != timings.REPOSITORY
            or env.get('GITHUB_REF') != 'refs/heads/main'):
        raise ValueError('benchmark requires the dedicated main dispatch workflow')
    if not re.fullmatch(r'[0-9a-f]{40}', source) or source != env.get('GITHUB_SHA'):
        raise ValueError('benchmark source must match the triggering exact commit')
    if subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() != source:
        raise ValueError('benchmark checkout differs from pinned source')
    if not re.fullmatch(r'ghcr\.io/meshbus/sdk-meshbus-builder@sha256:[0-9a-f]{64}', image):
        raise ValueError('benchmark requires an immutable Builder digest')
    if bool(history_artifact) != bool(history_sha256):
        raise ValueError('benchmark history needs both artifact ID and checksum')
    if history_artifact and not re.fullmatch(r'[1-9][0-9]*', history_artifact):
        raise ValueError('invalid benchmark history artifact ID')
    for value in (history_sha256, inventory_sha256):
        if value and not re.fullmatch(r'[0-9a-f]{64}', value):
            raise ValueError('invalid benchmark checksum')
    import plan
    selected = plan.select([]) if case == 'full' else plan.select([
        'tests/subsys/clock/testcase.yaml', 'samples/subsys/clock/sample.yaml'])
    for name in ('host', 'rust', 'products', 'product_builds', 'cli', 'audit', 'west_audit', 'fonts', 'checkpatch', 'openspec'):
        selected[name] = False
    selected.update(sdk=True, workspace=True, heavy=True, scheduler=scheduler,
                    product_names=[], product_requests=[], linux_targets=[], mac_targets=[], native_targets=[],
                    benchmark={'case': case, 'source_revision': source, 'builder_image': image,
                               'history_artifact': history_artifact, 'history_sha256': history_sha256,
                               'inventory_sha256': inventory_sha256}, changed_paths=None, audit_current=False)
    (snapshot / 'plan.json').write_text(json.dumps(selected, indent=2) + '\n')
    if output := env.get('GITHUB_OUTPUT'):
        with open(output, 'a') as stream:
            for name, value in selected.items():
                stream.write(f'{name}={value if isinstance(value, str) else json.dumps(value, separators=(",", ":"))}\n')
    return selected


def verify_inventory(plan, inventory):
    requested = plan['benchmark']
    instances = inventory['instances']
    if (inventory.get('schema', 1) != 1
            or timings.digest(timings.encoded(sorted(instances, key=timings.identity))) != inventory['sha256']):
        raise ValueError('benchmark inventory checksum is invalid')
    if requested['inventory_sha256'] and inventory['sha256'] != requested['inventory_sha256']:
        raise ValueError('benchmark inventory differs from pinned selection')
    layers = {row['layer'] for row in instances}
    if layers != {'runtime', 'compile'} or (requested['case'] == 'clock-small' and len(instances) > 12):
        raise ValueError('benchmark case must contain both layers within its size limit')
    if requested['case'] not in CASES:
        raise ValueError('unknown benchmark case')


def compare(legacy, balanced):
    if len(legacy) != 2 or len(balanced) != 2:
        raise ValueError('comparison requires two warm observations per scheduler')
    records = legacy + balanced
    first = records[0]
    if not first.get('benchmark'):
        raise ValueError('comparison requires benchmark measurements')
    reference = (first['source_revision'], first['builder_image'], first['inventory']['sha256'],
                 first['history_sha256'], first['benchmark']['case'])
    for scheduler, group in (('legacy', legacy), ('balanced', balanced)):
        for record in group:
            identity = (record['source_revision'], record['builder_image'], record['inventory']['sha256'],
                        record['history_sha256'], record['benchmark']['case'])
            if identity != reference or record['scheduler'] != scheduler:
                raise ValueError('benchmark comparisons require identical source, Builder, inventory and history')
            verify_inventory(record, record['inventory'])
            for field in ('runner_minutes', 'wall_seconds'):
                timings.seconds(record['job_metrics'][field])
    if len({(r['run_id'], r['run_attempt']) for r in records}) != len(records):
        raise ValueError('benchmark observations must be distinct runs')
    result = {'case': reference[-1], 'source_revision': reference[0], 'builder_image': reference[1],
              'inventory_sha256': reference[2], 'history_sha256': reference[3]}
    for name, group in (('legacy', legacy), ('balanced', balanced)):
        result[name] = {field: statistics.median(r['job_metrics'][field] for r in group)
                        for field in ('runner_minutes', 'wall_seconds')}
        result[name]['runs'] = [r['run_id'] for r in group]
    limit = max(60, result['legacy']['wall_seconds'] * .1)
    result['wall_regression_limit_seconds'] = limit
    result['wall_passed'] = result['balanced']['wall_seconds'] - result['legacy']['wall_seconds'] <= limit
    result['runner_passed'] = (result['case'] != 'clock-small'
                               or result['balanced']['runner_minutes'] < result['legacy']['runner_minutes'])
    result['passed'] = result['wall_passed'] and result['runner_passed']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    setup = sub.add_parser('configure')
    setup.add_argument('--snapshot', required=True, type=Path)
    setup.add_argument('--case', default='')
    setup.add_argument('--scheduler', default='balanced')
    setup.add_argument('--source', default='')
    setup.add_argument('--image', default='')
    setup.add_argument('--history-artifact', default='')
    setup.add_argument('--history-sha256', default='')
    setup.add_argument('--inventory-sha256', default='')
    report = sub.add_parser('compare')
    report.add_argument('--legacy', type=Path, nargs=2, required=True)
    report.add_argument('--balanced', type=Path, nargs=2, required=True)
    report.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'configure':
        result = configure(args.snapshot, args.case, args.scheduler, args.source, args.image,
                           args.history_artifact, args.history_sha256, args.inventory_sha256)
    else:
        result = compare([timings.read_json(p) for p in args.legacy], [timings.read_json(p) for p in args.balanced])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if args.mode == 'compare' and not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
