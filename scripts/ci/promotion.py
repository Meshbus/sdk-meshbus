# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Qualify and fetch immutable Candidate inputs; never create tags or Releases."""
import argparse
import io
import json
import os
from pathlib import Path
import re
import stat
import sys
import zipfile

import baseline
import publish

alpha = publish.alpha
art = alpha.art
REPOSITORY = alpha.REPOSITORY
CANDIDATES = '.github/workflows/candidates.yml'
ALPHA = '.github/workflows/alpha-release.yml'
IMAGE = r'ghcr.io/meshbus/sdk-meshbus-builder@sha256:[0-9a-f]{64}'


class Unavailable(ValueError):
    """A retained qualification is absent, expired or from an older schema."""


def expected_artifacts():
    return {'source-snapshot', 'verified-firmware-candidate'} | {
        prefix + target for prefix in ('cli-', 'native-validation-') for target in art.CLIENTS}


def descriptor(item):
    art.require(type(item['id']) is int and item['id'] > 0 and
                re.fullmatch(r'sha256:[0-9a-f]{64}', item.get('digest') or '') and
                type(item['size_in_bytes']) is int and 0 < item['size_in_bytes'] <= alpha.MAX_ASSET,
                'invalid Candidate artifact identity/digest')
    return {key: item[key] for key in ('id', 'name', 'digest', 'size_in_bytes')}


def members(data, maximum=512 * 1024 * 1024):
    """Read regular, bounded archive members without following archive links."""
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        art.require(len(entries) <= 10000 and sum(item.file_size for item in entries) <= maximum,
                    'Candidate archive exceeds bound')
        names = [item.filename.rstrip('/') for item in entries]
        art.require(len(set(names)) == len(names), 'duplicate Candidate archive member')
        result = {}
        for item, name in zip(entries, names):
            art.relative(name)
            mode = (item.external_attr >> 16) & 0o170000
            art.require(mode in (0, stat.S_IFREG, stat.S_IFDIR), 'Candidate archive contains a special file')
            if not item.is_dir():
                result[name] = archive.read(item)
        return result


def download(github, item, run_id=None):
    wanted = descriptor(item)
    current = github.request(github.base + f'/actions/artifacts/{wanted["id"]}')
    if current['expired']:
        raise Unavailable('Candidate artifact expired: ' + wanted['name'])
    art.require(descriptor(current) == wanted, 'Candidate artifact metadata changed')
    if run_id is not None:
        art.require(current['workflow_run']['id'] == int(run_id), 'Candidate artifact run conflict')
    data = github.request(github.base + f'/actions/artifacts/{wanted["id"]}/zip',
                          binary=True, accept='application/vnd.github+json')
    art.require('sha256:' + art.digest(data) == wanted['digest'], 'Candidate artifact digest conflict')
    return data


def run_identity(run, sha, *, completed=True):
    art.require(run['repository']['full_name'] == REPOSITORY and run['head_sha'] == sha and
                type(run['run_attempt']) is int and run['run_attempt'] >= 1,
                'Candidate repository/source/attempt conflict')
    if completed:
        art.require(run['path'] == CANDIDATES and run['head_branch'] == 'main' and
                    run['event'] == 'workflow_dispatch' and run['status'] == 'completed' and
                    run['conclusion'] == 'success', 'Candidate is not a trusted successful main run')
    else:
        art.require(run['path'] in (CANDIDATES, ALPHA) and run['event'] in ('workflow_dispatch', 'push'),
                    'unexpected Candidate caller')


def validate(receipt, run, sha, manifest_hash, tag):
    if receipt.get('schema') != 1:
        raise Unavailable('unsupported Candidate qualification schema')
    art.require(receipt['repository'] == REPOSITORY and receipt['workflow'] == run['path'] and
                receipt['run_id'] == str(run['id']) and receipt['run_attempt'] == run['run_attempt'] and
                receipt['source_revision'] == sha and receipt['manifest_sha256'] == manifest_hash and
                receipt['version'] == alpha.version(tag), 'Candidate qualification identity conflict')
    art.require(re.fullmatch(IMAGE, receipt['builder_image']) and
                re.fullmatch(r'[0-9a-f]{64}', receipt['frozen_manifest_sha256']) and
                receipt['strict'] is True and receipt['cli_profile'] == 'release',
                'Candidate build qualification conflict')
    inputs = receipt['artifacts']
    art.require({row['name'] for row in inputs} == expected_artifacts() and
                len(inputs) == len(expected_artifacts()) and
                len({row['id'] for row in inputs}) == len(inputs), 'incomplete Candidate artifact inventory')
    for item in inputs:
        descriptor(item)
    return receipt


def qualify(github, snapshot, root, run_id, attempt):
    """Seal the exact retained IDs only after all Candidate requirements pass."""
    alpha.require_jobs(json.loads(os.environ['NEEDS_JSON']), ['validation', 'products', 'assemble'])
    source = json.loads(art.read(snapshot / 'source.json'))
    run = github.request(github.base + f'/actions/runs/{run_id}')
    run_identity(run, source['source_revision'], completed=False)
    art.require(run['run_attempt'] == attempt, 'Candidate qualifying attempt changed')
    plan = json.loads(art.read(snapshot / 'plan.json'))
    art.require(plan['full'] is True and not plan.get('benchmark') and plan['cli_profile'] == 'release',
                'Candidate qualification requires a complete release plan')
    tag = source_tag(root)
    alpha.checkout_identity(root, tag, source['source_revision'])
    items = baseline.pages(github, f'/actions/runs/{run_id}/artifacts', 'artifacts')
    selected = [item for item in items if item['name'] in expected_artifacts() and not item['expired']]
    receipt = {'schema': 1, 'repository': REPOSITORY, 'workflow': run['path'],
               'run_id': str(run_id), 'run_attempt': attempt, 'source_revision': source['source_revision'],
               'manifest_sha256': source['manifest_sha256'], 'version': alpha.version(tag),
               'builder_image': (snapshot / 'image.txt').read_text().strip(),
               'frozen_manifest_sha256': art.digest(art.read(snapshot / 'west-frozen.yml')),
               'strict': True, 'cli_profile': 'release',
               'artifacts': sorted((descriptor(item) for item in selected), key=lambda item: item['name'])}
    return validate(receipt, run, source['source_revision'], source['manifest_sha256'], tag)


def qualification(github, run, sha, manifest_hash, tag):
    run_identity(run, sha)
    items = baseline.pages(github, f'/actions/runs/{run["id"]}/artifacts', 'artifacts')
    candidates = [item for item in items if item['name'] == f'candidate-provenance-{run["run_attempt"]}'
                  and not item['expired']]
    if not candidates:
        raise Unavailable('no retained qualification for the latest Candidate attempt')
    art.require(len(candidates) == 1, 'duplicate Candidate qualification')
    receipt_item = descriptor(candidates[0])
    content = members(download(github, receipt_item, run['id']), 1024 * 1024)
    art.require(set(content) == {'candidate-provenance.json'}, 'unexpected Candidate qualification files')
    receipt = validate(json.loads(content['candidate-provenance.json']), run, sha, manifest_hash, tag)
    inventory = {item['id']: item for item in items}
    for item in receipt['artifacts']:
        if item['id'] not in inventory or inventory[item['id']]['expired']:
            raise Unavailable('required Candidate artifact is missing or expired')
        art.require(descriptor(inventory[item['id']]) == item, 'Candidate artifact inventory conflict')
    return {'mode': 'reused', 'run_id': str(run['id']), 'run_attempt': run['run_attempt'],
            'receipt_id': str(receipt_item['id']), 'image': receipt['builder_image']}


def select(github, sha, manifest_hash, tag, run_id=None):
    if run_id:
        return qualification(github, github.request(github.base + f'/actions/runs/{run_id}'), sha, manifest_hash, tag)
    runs = baseline.pages(github, f'/actions/workflows/candidates.yml/runs?head_sha={sha}&branch=main&status=success',
                          'workflow_runs')
    for run in sorted(runs, key=lambda row: row['id'], reverse=True):
        # Other event types and unfinished reruns are not promotion candidates.
        if run['event'] != 'workflow_dispatch' or run['status'] != 'completed' or run['conclusion'] != 'success':
            continue
        try:
            return qualification(github, run, sha, manifest_hash, tag)
        except Unavailable:
            continue
    return {'mode': 'fresh', 'run_id': '', 'run_attempt': '', 'receipt_id': '', 'image': ''}


def fetch(github, receipt_id, run_id, sha, manifest_hash, tag, destination, mode):
    run = github.request(github.base + f'/actions/runs/{run_id}')
    run_identity(run, sha, completed=mode == 'reused')
    if mode == 'fresh':
        art.require(str(run_id) == os.environ['GITHUB_RUN_ID'] and
                    run['run_attempt'] == int(os.environ['GITHUB_RUN_ATTEMPT']), 'fresh Candidate run conflict')
    item = github.request(github.base + f'/actions/artifacts/{receipt_id}')
    art.require(item['workflow_run']['id'] == int(run_id) and
                item['name'] == f'candidate-provenance-{run["run_attempt"]}', 'qualification artifact run conflict')
    content = members(download(github, item, run_id), 1024 * 1024)
    art.require(set(content) == {'candidate-provenance.json'}, 'unexpected Candidate qualification files')
    receipt = validate(json.loads(content['candidate-provenance.json']), run, sha, manifest_hash, tag)
    for item in receipt['artifacts']:
        if item['name'] == 'source-snapshot':
            output = destination / 'snapshot'
        elif item['name'] == 'verified-firmware-candidate':
            output = destination / 'firmware-candidate'
        elif item['name'].startswith('cli-'):
            output = destination / 'cli-parts' / item['name']
        else:
            output = destination / 'cli-native' / item['name']
        art.clean_destination(output)
        for name, data in members(download(github, item, run_id)).items():
            path = output / art.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    snapshot = destination / 'snapshot'
    source = json.loads(art.read(snapshot / 'source.json'))
    art.require(source == {'source_revision': sha, 'manifest_sha256': manifest_hash} and
                (snapshot / 'image.txt').read_text().strip() == receipt['builder_image'] and
                art.digest(art.read(snapshot / 'west-frozen.yml')) == receipt['frozen_manifest_sha256'],
                'downloaded Candidate snapshot conflict')
    evidence = {key: receipt[key] for key in ('repository', 'run_id', 'run_attempt', 'source_revision',
                'manifest_sha256', 'frozen_manifest_sha256', 'builder_image')}
    evidence.update(mode=mode, receipt_id=str(receipt_id),
                    run_url=f'https://github.com/{REPOSITORY}/actions/runs/{run_id}')
    art.write_json(snapshot / 'candidate-reuse.json', evidence)
    return evidence


def source_tag(root):
    fields = dict(re.findall(r'^([A-Z_]+)\s*=\s*(.*?)\s*$', (root / 'apps/meshbus/VERSION').read_text(), re.M))
    tag = 'v' + '.'.join(str(int(fields[key])) for key in ('VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL'))
    return tag + ('-' + fields['EXTRAVERSION'] if fields['EXTRAVERSION'] else '')


def outputs(values):
    if path := os.environ.get('GITHUB_OUTPUT'):
        with open(path, 'a') as stream:
            for key, value in values.items():
                art.require('\n' not in str(value), 'invalid promotion output')
                stream.write(f'{key}={value}\n')
    print(json.dumps(values, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('qualify', 'select', 'fetch'))
    parser.add_argument('--snapshot', type=Path, default=Path('snapshot'))
    parser.add_argument('--output', type=Path, default=Path('.'))
    parser.add_argument('--run-id')
    parser.add_argument('--receipt-id')
    parser.add_argument('--mode', choices=('fresh', 'reused'))
    args = parser.parse_args()
    for value in (args.run_id, args.receipt_id):
        art.require(not value or re.fullmatch(r'[1-9][0-9]*', value), 'invalid Candidate run/artifact ID')
    root = Path(__file__).resolve().parents[2]
    sha = os.environ['GITHUB_SHA']
    tag = os.environ.get('RELEASE_TAG') or source_tag(root)
    if args.command == 'qualify' and '-alpha.' not in tag:
        alpha.require_jobs(json.loads(os.environ['NEEDS_JSON']), ['validation', 'products', 'assemble'])
        outputs({'eligible': 'false'})
        print('Non-Alpha Candidate completed; no Alpha promotion qualification is recorded.')
        return
    alpha.checkout_identity(root, tag, sha)
    github = publish.GitHub(os.environ['GITHUB_TOKEN'])
    manifest_hash = art.digest(art.read(root / 'west.yml'))
    if args.command == 'qualify':
        receipt = qualify(github, args.snapshot, root, os.environ['GITHUB_RUN_ID'],
                          int(os.environ['GITHUB_RUN_ATTEMPT']))
        args.output.mkdir(parents=True, exist_ok=True)
        art.write_json(args.output / 'candidate-provenance.json', receipt)
        outputs({'eligible': 'true'})
    elif args.command == 'select':
        selected = select(github, sha, manifest_hash, tag, args.run_id)
        outputs(dict(selected, tag=tag))
    else:
        art.require(args.receipt_id and args.run_id and args.mode, 'fetch requires receipt, run and mode')
        outputs(fetch(github, args.receipt_id, args.run_id, sha, manifest_hash, tag, args.output, args.mode))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f'Candidate qualification failed: {error}', file=sys.stderr)
        raise SystemExit(1) from error
