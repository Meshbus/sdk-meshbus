# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Reuse full CI Twister and default-product evidence for exact inputs."""
import argparse
import io
import json
import os
from pathlib import Path
import re
import sys
import zipfile

import publish

art = publish.alpha.art
REPOSITORY = publish.alpha.REPOSITORY


def archive(data):
    """Read bounded snapshot files without extracting paths or executables."""
    with zipfile.ZipFile(io.BytesIO(data)) as zipped:
        entries = zipped.infolist()
        art.require(len(entries) <= 4096 and sum(p.file_size for p in entries) <= 16 * 1024 * 1024,
                    'baseline snapshot exceeds bound')
        names = [p.filename for p in entries]
        art.require(len(set(names)) == len(names), 'duplicate baseline archive member')
        for name in names:
            art.relative(name.rstrip('/'))
        wanted = ('source.json', 'plan.json', 'image.txt', 'west-frozen.yml', 'shards.json')
        art.require(set(wanted) <= set(names), 'incomplete baseline snapshot')
        optional = ('products.json', 'product-builds.json')
        return {name: zipped.read(name) for name in (*wanted, *optional) if name in names}


def products(inventory, builds):
    """Normalize explicit profiles and require the complete default matrix."""
    rows = inventory['include']
    normalized = sorted((row['id'], row['board']) for row in rows)
    art.require(normalized and len(set(normalized)) == len(normalized) and
                len({row[0] for row in normalized}) == len(normalized) and
                len({row[1] for row in normalized}) == len(normalized), 'invalid baseline product inventory')
    profiles = sorted((row['id'], row['board'], row['profile']) for row in builds['include'])
    art.require(profiles and len(set(profiles)) == len(profiles) and
                all((row[0], row[1]) in normalized and row[2] in ('default', 'dev', 'prod') for row in profiles) and
                {(name, board, 'default') for name, board in normalized} <= set(profiles),
                'incomplete baseline default-product matrix')
    return ([dict(id=name, board=board) for name, board in normalized],
            [dict(id=name, board=board, profile=profile) for name, board, profile in profiles])


def successful(jobs, names):
    for name in names:
        matches = [job for job in jobs if job['name'] == name]
        art.require(len(matches) == 1 and matches[0]['status'] == 'completed' and
                    matches[0]['conclusion'] == 'success', 'missing or unsuccessful baseline job: ' + name)


def record(run, files, jobs, sha, manifest_hash, image):
    art.require(run['head_sha'] == sha and run['head_branch'] == 'main' and
                run['path'] == '.github/workflows/ci.yml' and
                run['event'] in ('push', 'schedule', 'workflow_dispatch') and
                run['status'] == 'completed' and run['conclusion'] == 'success' and
                run['repository']['full_name'] == REPOSITORY, 'untrusted or unsuccessful CI baseline')
    source = json.loads(files['source.json'])
    plan = json.loads(files['plan.json'])
    art.require(source == {'source_revision': sha, 'manifest_sha256': manifest_hash}, 'baseline source conflict')
    art.require(files['image.txt'].decode().strip() == image, 'baseline builder conflict')
    art.require(plan['full'] is True and plan['sdk'] is True and
                not plan.get('benchmark') and plan.get('scheduler', 'balanced') != 'legacy' and
                plan['test_roots'] == ['tests'] and plan['compile_roots'] == ['tests', 'samples'],
                'baseline did not execute full Twister validation')
    matrix = json.loads(files['shards.json'])
    shards = matrix['include']
    modern = matrix.get('schema') == 2
    if modern:
        from test_plan import job_tasks
        shards = job_tasks(matrix)
    names = ['validation / ' + item['name'] for item in shards]
    tasks = ([task for item in shards for task in item['tasks']] if modern else shards)
    identities = [(task['layer'], task.get('shard', index)) for index, task in enumerate(tasks)]
    art.require(names and len(set(names)) == len(names) and
                {layer for layer, _ in identities} == {'runtime', 'compile'} and
                len(set(identities)) == len(identities) and
                all(type(shard) is int and shard >= 0 for _, shard in identities),
                'incomplete baseline Twister matrix')
    successful(jobs, names + ['validation / Required checks'])
    result = {'schema': 1, 'mode': 'reused', 'repository': REPOSITORY,
            'run_id': str(run['id']), 'run_attempt': run['run_attempt'],
            'run_url': f'https://github.com/{REPOSITORY}/actions/runs/{run["id"]}',
            'source_revision': sha, 'manifest_sha256': manifest_hash, 'builder_image': image,
            'frozen_manifest_sha256': art.digest(files['west-frozen.yml']), 'jobs': names}
    if modern:
        art.require(plan.get('products') is True and plan.get('product_builds') is True,
                    'baseline did not execute complete default products')
        inventory, builds = products(json.loads(files['products.json']), json.loads(files['product-builds.json']))
        product_jobs = [f'validation / Firmware / {row["id"]} ({row["profile"]})' for row in builds]
        successful(jobs, product_jobs)
        result.update(schema=2, products=inventory, product_builds=builds, product_jobs=product_jobs,
                      twister_jobs=shards)
    return result


def pages(github, path, key):
    result = []
    for page in range(1, 11):
        separator = '&' if '?' in path else '?'
        batch = github.request(github.base + path + f'{separator}per_page=100&page={page}')[key]
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise ValueError('baseline GitHub inventory exceeds bound')


def select(github, sha, manifest_hash, image):
    runs = pages(github, f'/actions/workflows/ci.yml/runs?head_sha={sha}&branch=main&status=success', 'workflow_runs')
    reasons = []
    for run in runs:
        artifacts = pages(github, f'/actions/runs/{run["id"]}/artifacts', 'artifacts')
        snapshots = [item for item in artifacts if item['name'] == 'source-snapshot' and not item['expired']]
        if len(snapshots) != 1:
            reasons.append(f'{run["id"]}: no retained source snapshot')
            continue
        try:
            files = archive(github.request(github.base + f'/actions/artifacts/{snapshots[0]["id"]}/zip',
                                           binary=True, accept='application/vnd.github+json'))
            jobs = pages(github, f'/actions/runs/{run["id"]}/attempts/{run["run_attempt"]}/jobs', 'jobs')
            result = record(run, files, jobs, sha, manifest_hash, image)
            art.require(result['schema'] == 2, 'baseline has no qualified default-product evidence')
            return result
        except (ValueError, KeyError) as error:
            reasons.append(f'{run["id"]}: {error}')
    raise ValueError('No reusable full CI Twister baseline for this source and builder. '
                     'Complete CI on this commit with the selected builder, then retry Candidate preparation. '
                     + '; '.join(reasons))


def verify(snapshot):
    baseline = json.loads(art.read(snapshot / 'twister-baseline.json'))
    source = json.loads(art.read(snapshot / 'source.json'))
    image = (snapshot / 'image.txt').read_text().strip()
    art.require(baseline['schema'] in (1, 2) and baseline['mode'] == 'reused' and
                baseline['repository'] == REPOSITORY and
                baseline['source_revision'] == source['source_revision'] and
                baseline['manifest_sha256'] == source['manifest_sha256'] and
                baseline['builder_image'] == image and
                baseline['frozen_manifest_sha256'] == art.digest(art.read(snapshot / 'west-frozen.yml')),
                'reused Twister source/dependency/builder conflict')
    art.require(re.fullmatch(r'[1-9][0-9]*', baseline['run_id']) and
                baseline['run_url'] == f'https://github.com/{REPOSITORY}/actions/runs/{baseline["run_id"]}' and
                baseline['jobs'], 'invalid reused Twister provenance')
    if baseline['schema'] == 2:
        from test_plan import job_tasks
        tasks = job_tasks({'schema': 2, 'include': baseline['twister_jobs']})
        art.require(baseline['jobs'] == ['validation / ' + row['name'] for row in tasks] and
                    {task['layer'] for row in tasks for task in row['tasks']} == {'runtime', 'compile'},
                    'invalid reused Twister task evidence')
        inventory, builds = products({'include': baseline['products']}, {'include': baseline['product_builds']})
        current_inventory, current_builds = products(json.loads(art.read(snapshot / 'products.json')),
                                                     json.loads(art.read(snapshot / 'product-builds.json')))
        art.require(inventory == current_inventory and all(row in builds for row in current_builds),
                    'reused product inventory/profile conflict')
        art.require(baseline['product_jobs'] ==
                    [f'validation / Firmware / {row["id"]} ({row["profile"]})' for row in builds],
                    'invalid reused product job evidence')
    return baseline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('select', 'verify'))
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--image')
    args = parser.parse_args()
    if args.mode == 'verify':
        baseline = verify(args.snapshot)
    else:
        sha = os.environ['GITHUB_SHA']
        art.require(re.fullmatch(r'[0-9a-f]{40}', sha), 'invalid baseline source')
        baseline = select(publish.GitHub(os.environ['GITHUB_TOKEN']), sha,
                          art.digest(art.read(args.manifest)), args.image)
        art.write_json(args.snapshot / 'twister-baseline.json', baseline)
        path = args.snapshot / 'plan.json'
        plan = json.loads(art.read(path))
        art.require(plan['full'] is True and plan['sdk'] is True, 'reuse requires a full validation plan')
        art.require(baseline['schema'] == 2, 'default-product reuse requires schema 2')
        plan.update(sdk=False, product_builds=False, twister_baseline=baseline)
        art.write_json(path, plan)
        if output := os.environ.get('GITHUB_OUTPUT'):
            with open(output, 'a') as stream:
                stream.write('sdk=false\nproduct_builds=false\n')
    print(json.dumps(baseline, indent=2))
    if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(summary, 'a') as stream:
            stream.write(f'\nTwister and default products reused from [full CI]({baseline["run_url"]}), '
                         f'attempt {baseline["run_attempt"]}; source and builder match.\n')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
