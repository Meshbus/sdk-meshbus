# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Bounded scheduling hints and measurements; never qualification evidence."""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import statistics
import urllib.error
import zipfile

REPOSITORY = 'Meshbus/sdk-meshbus'
MAX_BYTES = 4 * 1024 * 1024
MAX_ROWS = 10000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def read_json(path):
    with path.open('rb') as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError('timing input exceeds bound')
    return json.loads(data)


def identity(row):
    values = (row['layer'], row['name'], row['platform'], row['toolchain'])
    if values[0] not in ('runtime', 'compile') or any(not isinstance(v, str) or not v or len(v) > 1024 for v in values):
        raise ValueError('invalid timing identity')
    return values


def seconds(value):
    if isinstance(value, bool):
        raise ValueError('invalid timing duration')
    number = float(value)
    if not math.isfinite(number) or number < 0 or number > 24 * 60 * 60:
        raise ValueError('invalid timing duration')
    return number


def validate_rows(rows, field):
    if not isinstance(rows, list) or len(rows) > MAX_ROWS:
        raise ValueError('timing rows exceed bound')
    seen = set()
    for row in rows:
        key = identity(row)
        if key in seen:
            raise ValueError('duplicate timing identity')
        seen.add(key)
        if field == 'seconds':
            if seconds(row[field]) <= 0:
                raise ValueError('timing weight must be positive')
        else:
            seconds(row['build_time'])
            seconds(row['execution_time'])
    return rows


def inventory(reports):
    from test_plan import active, key
    rows = [dict(zip(('layer', 'name', 'platform', 'toolchain'), (layer, *key(suite))))
            for layer, report in reports.items() for suite in active(report)]
    rows.sort(key=identity)
    if len({identity(row) for row in rows}) != len(rows):
        raise ValueError('duplicate inventory identity')
    return {'schema': 1, 'sha256': digest(encoded(rows)), 'instances': rows}


def load_history(path, image=None):
    try:
        history = read_json(path)
        if history['schema'] != 1:
            raise ValueError('unsupported timing history')
        if image and history.get('builder_image') != image:
            raise ValueError('timing history Builder differs')
        validate_rows(history['weights'], 'seconds')
        return history
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as error:
        return {'schema': 1, 'weights': [], 'sources': [], 'fallback': str(error)}


def layer_weights(history, layer):
    return {identity(row)[1:]: seconds(row['seconds']) for row in history.get('weights', [])
            if row['layer'] == layer}


def predictions(snapshot, matrix, history):
    from test_plan import active, job_tasks, key
    jobs = []
    for job in job_tasks(matrix):
        tasks = []
        for task in job['tasks']:
            suites = active(read_json(snapshot / f"{task['layer']}-{task['shard']}.json"))
            weights = layer_weights(history, task['layer'])
            estimate = (sum(weights.get(key(s), statistics.median(weights.values())) for s in suites)
                        if weights else None)
            tasks.append(dict(task, instances=len(suites), predicted_seconds=estimate))
        jobs.append(dict(id=job['id'], tasks=tasks,
                         predicted_seconds=sum(t['predicted_seconds'] for t in tasks)
                         if all(t['predicted_seconds'] is not None for t in tasks) else None))
    return {'schema': 1, 'jobs': jobs}


def trusted_run(run):
    return (run['repository']['full_name'] == REPOSITORY and run['head_branch'] == 'main'
            and run['path'] == '.github/workflows/ci.yml'
            and run['event'] in ('push', 'schedule', 'workflow_dispatch')
            and run['status'] == 'completed' and run['conclusion'] == 'success')


def unpack(data):
    if len(data) > MAX_BYTES:
        raise ValueError('timing archive exceeds bound')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        files = archive.infolist()
        if (len(files) != 1 or files[0].filename != 'timings.json'
                or files[0].file_size > MAX_BYTES or files[0].is_dir()):
            raise ValueError('invalid timing archive')
        return archive.read(files[0])


def artifact_record(github, artifact, run, image, expected_hash=''):
    if not trusted_run(run) or artifact['expired'] or artifact['name'] != 'ci-timings':
        raise ValueError('untrusted timing artifact')
    if artifact.get('workflow_run', {}).get('id', run['id']) != run['id']:
        raise ValueError('timing artifact run differs')
    payload = unpack(github.request(github.base + f"/actions/artifacts/{artifact['id']}/zip",
                                   binary=True, accept='application/vnd.github+json'))
    if expected_hash and digest(payload) != expected_hash:
        raise ValueError('pinned timing artifact checksum differs')
    record = json.loads(payload)
    if (record['schema'] != 1 or record['repository'] != REPOSITORY or record.get('benchmark')
            or record['builder_image'] != image or record['source_revision'] != run['head_sha']
            or str(record['run_id']) != str(run['id']) or record['run_attempt'] != run['run_attempt']):
        raise ValueError('timing artifact identity differs')
    validate_rows(record['instances'], 'build_time')
    return record, {'artifact_id': str(artifact['id']), 'run_id': str(run['id']),
                    'run_attempt': run['run_attempt'], 'sha256': digest(payload)}


def fetch_history(github, image, artifact_id='', expected_hash=''):
    records, sources, rejected = [], [], []
    if artifact_id:
        artifact = github.request(github.base + f'/actions/artifacts/{artifact_id}')
        run = github.request(github.base + f"/actions/runs/{artifact['workflow_run']['id']}")
        record, source = artifact_record(github, artifact, run, image, expected_hash)
        records.append(record)
        sources.append(source)
    else:
        runs = github.request(github.base + '/actions/workflows/ci.yml/runs?branch=main&status=success&per_page=10')['workflow_runs']
        for run in runs[:10]:
            if not trusted_run(run):
                continue
            artifacts = github.request(github.base + f"/actions/runs/{run['id']}/artifacts?per_page=100")['artifacts']
            matches = [a for a in artifacts if a['name'] == 'ci-timings' and not a['expired']]
            if len(matches) != 1:
                continue
            try:
                record, source = artifact_record(github, matches[0], run, image)
                records.append(record)
                sources.append(source)
            except (ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
                rejected.append(f"run {run['id']}: {error}")
            if len(records) == 3:
                break
    observations = defaultdict(list)
    for record in records:
        for row in record['instances']:
            total = seconds(row['build_time']) + seconds(row['execution_time'])
            if total > 0:
                observations[identity(row)].append(total)
    weights = [dict(zip(('layer', 'name', 'platform', 'toolchain'), key),
                    seconds=statistics.median(values)) for key, values in sorted(observations.items())]
    return {'schema': 1, 'builder_image': image, 'weights': weights, 'sources': sources,
            'fallback': '' if weights else 'no compatible timing observations', 'rejected': rejected}


def history(snapshot, image, github=None):
    plan = read_json(snapshot / 'plan.json')
    pins = plan.get('benchmark', {}) or {}
    artifact_id = pins.get('history_artifact', '')
    try:
        if github is None:
            from publish import GitHub
            github = GitHub(os.environ.get('GITHUB_TOKEN', ''))
        result = fetch_history(github, image, artifact_id, pins.get('history_sha256', ''))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile, urllib.error.URLError) as error:
        # A benchmark pin is a reproducibility requirement, not a scheduling hint.
        if artifact_id:
            raise ValueError(f'pinned benchmark history unavailable: {error}') from error
        result = {'schema': 1, 'builder_image': image, 'weights': [], 'sources': [], 'fallback': str(error)}
    (snapshot / 'history.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'observations': len(result['weights']), 'sources': result['sources'],
                      'fallback': result['fallback']}))
    return result


def job_metrics(github, run_id, attempt, names):
    jobs = []
    for page in range(1, 11):
        batch = github.request(github.base + f'/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100&page={page}')['jobs']
        jobs.extend(batch)
        if len(batch) < 100:
            break
    else:
        raise ValueError('job metrics exceed bound')
    selected = [job for job in jobs if any(job['name'].endswith(' / ' + name) or job['name'] == name for name in names)]
    if (len(selected) != len(names) or any(j['status'] != 'completed' for j in selected)
            or any(sum(job['name'].endswith(' / ' + name) or job['name'] == name for job in selected) != 1
                   for name in names)):
        raise ValueError('job timing inventory is incomplete')
    def stamp(value):
        return datetime.fromisoformat(value.replace('Z', '+00:00')).timestamp()
    records = [{'name': job['name'], 'seconds': stamp(job['completed_at']) - stamp(job['started_at']),
                'started_at': job['started_at'], 'completed_at': job['completed_at'],
                'steps': [{'name': step['name'], 'seconds': stamp(step['completed_at']) - stamp(step['started_at'])}
                          for step in job.get('steps', []) if step.get('started_at') and step.get('completed_at')]}
               for job in selected]
    return {'jobs': records, 'runner_minutes': sum(r['seconds'] for r in records) / 60,
            'wall_seconds': max(stamp(r['completed_at']) for r in records) - min(stamp(r['started_at']) for r in records)}


def collect(snapshot, reports, output, github=None):
    from test_plan import job_tasks, verify_snapshot
    verify_snapshot(snapshot, reports)
    plan = read_json(snapshot / 'plan.json')
    jobs = job_tasks(read_json(snapshot / 'shards.json'))
    instances, tasks = [], []
    for layer in ('runtime', 'compile'):
        for path in sorted(reports.glob(f'**/{layer}-*/twister.json')):
            for suite in read_json(path)['testsuites']:
                instances.append(dict(layer=layer, name=suite['name'], platform=suite['platform'],
                                      toolchain=suite['toolchain'], build_time=seconds(suite.get('build_time', 0)),
                                      execution_time=seconds(suite.get('execution_time', 0))))
        for path in sorted(reports.glob(f'**/{layer}-*/task-timing.json')):
            tasks.append(read_json(path))
    validate_rows(instances, 'build_time')
    source = read_json(snapshot / 'source.json')
    image = (snapshot / 'image.txt').read_text().strip()
    history_data = load_history(snapshot / 'history.json', image)
    run_id, attempt = os.environ['GITHUB_RUN_ID'], int(os.environ['GITHUB_RUN_ATTEMPT'])
    if not re.fullmatch(r'[1-9][0-9]*', run_id) or attempt < 1:
        raise ValueError('invalid timing run identity')
    result = {'schema': 1, 'repository': REPOSITORY, 'run_id': run_id, 'run_attempt': attempt,
              'source_revision': source['source_revision'], 'builder_image': image,
              'benchmark': plan.get('benchmark', False), 'scheduler': plan.get('scheduler', 'balanced'),
              'inventory': read_json(snapshot / 'twister-inventory.json'), 'instances': sorted(instances, key=identity),
              'history_sources': history_data.get('sources', []),
              'history_sha256': digest(encoded(history_data.get('weights', []))),
              'history_fallback': history_data.get('fallback', ''), 'tasks': tasks,
              'scheduling': read_json(snapshot / 'scheduling.json')}
    try:
        if github is None:
            from publish import GitHub
            github = GitHub(os.environ.get('GITHUB_TOKEN', ''))
        result['job_metrics'] = job_metrics(github, run_id, attempt, [job['name'] for job in jobs])
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as error:
        result['job_metrics_error'] = str(error)
    output.parent.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(result, indent=2) + '\n').encode()
    if len(data) > MAX_BYTES:
        raise ValueError('collected timings exceed bound')
    output.write_bytes(data)
    print(json.dumps({'path': str(output), 'sha256': digest(data), 'instances': len(instances),
                      'job_metrics': result.get('job_metrics', result.get('job_metrics_error'))}))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    load = sub.add_parser('history')
    load.add_argument('--snapshot', type=Path, required=True)
    load.add_argument('--image', required=True)
    save = sub.add_parser('collect')
    save.add_argument('--snapshot', type=Path, required=True)
    save.add_argument('--reports', type=Path, required=True)
    save.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'history':
        history(args.snapshot, args.image)
    else:
        collect(args.snapshot, args.reports, args.output)


if __name__ == '__main__':
    main()
