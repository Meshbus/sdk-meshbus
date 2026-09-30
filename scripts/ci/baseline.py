# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Reuse completed full CI Twister evidence for the exact source and builder."""
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
        return {name: zipped.read(name) for name in wanted}


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
                plan['test_roots'] == ['tests'] and plan['compile_roots'] == ['tests', 'samples'],
                'baseline did not execute full Twister validation')
    shards = json.loads(files['shards.json'])['include']
    names = ['validation / ' + item['name'] for item in shards]
    art.require(names and len(set(names)) == len(names) and
                {item['layer'] for item in shards} == {'runtime', 'compile'}, 'incomplete baseline Twister matrix')
    for name in names + ['validation / Required checks']:
        matches = [job for job in jobs if job['name'] == name]
        art.require(len(matches) == 1 and matches[0]['status'] == 'completed' and
                    matches[0]['conclusion'] == 'success', 'missing or unsuccessful baseline job: ' + name)
    return {'schema': 1, 'mode': 'reused', 'repository': REPOSITORY,
            'run_id': str(run['id']), 'run_attempt': run['run_attempt'],
            'run_url': f'https://github.com/{REPOSITORY}/actions/runs/{run["id"]}',
            'source_revision': sha, 'manifest_sha256': manifest_hash, 'builder_image': image,
            'frozen_manifest_sha256': art.digest(files['west-frozen.yml']), 'jobs': names}


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
            return record(run, files, jobs, sha, manifest_hash, image)
        except (ValueError, KeyError) as error:
            reasons.append(f'{run["id"]}: {error}')
    raise ValueError('No reusable full CI Twister baseline for this source and builder. '
                     'Complete CI on this commit with the selected builder, then retry Candidate preparation. '
                     + '; '.join(reasons))


def verify(snapshot):
    baseline = json.loads(art.read(snapshot / 'twister-baseline.json'))
    source = json.loads(art.read(snapshot / 'source.json'))
    image = (snapshot / 'image.txt').read_text().strip()
    art.require(baseline['schema'] == 1 and baseline['mode'] == 'reused' and
                baseline['repository'] == REPOSITORY and
                baseline['source_revision'] == source['source_revision'] and
                baseline['manifest_sha256'] == source['manifest_sha256'] and
                baseline['builder_image'] == image and
                baseline['frozen_manifest_sha256'] == art.digest(art.read(snapshot / 'west-frozen.yml')),
                'reused Twister source/dependency/builder conflict')
    art.require(re.fullmatch(r'[1-9][0-9]*', baseline['run_id']) and
                baseline['run_url'] == f'https://github.com/{REPOSITORY}/actions/runs/{baseline["run_id"]}' and
                baseline['jobs'], 'invalid reused Twister provenance')
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
        plan.update(sdk=False, twister_baseline=baseline)
        art.write_json(path, plan)
        if output := os.environ.get('GITHUB_OUTPUT'):
            with open(output, 'a') as stream:
                stream.write('sdk=false\n')
    print(json.dumps(baseline, indent=2))
    if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(summary, 'a') as stream:
            stream.write(f'\nTwister reused from [full CI]({baseline["run_url"]}), '
                         f'attempt {baseline["run_attempt"]}; source and builder match.\n')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
