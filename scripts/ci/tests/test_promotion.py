# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import copy
import io
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import promotion

SHA = 'a' * 40
MANIFEST = 'b' * 64
IMAGE = 'ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'c' * 64
TAG = 'v1.0.0-alpha.1'


def zipped(files):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as archive:
        for name, value in files.items():
            archive.writestr(name, value)
    return stream.getvalue()


class GitHub:
    base = 'https://api.github.com/repos/' + promotion.REPOSITORY

    def __init__(self):
        self.run = {'id': 123, 'run_attempt': 1, 'head_sha': SHA, 'head_branch': 'main',
                    'path': promotion.CANDIDATES, 'event': 'workflow_dispatch', 'status': 'completed',
                    'conclusion': 'success', 'repository': {'full_name': promotion.REPOSITORY}}
        self.items, self.data, self.requests = {}, {}, []
        self.snapshot = {'source.json': json.dumps({'source_revision': SHA, 'manifest_sha256': MANIFEST}),
                         'image.txt': IMAGE, 'west-frozen.yml': 'manifest: {}\n',
                         'plan.json': json.dumps({'full': True, 'cli_profile': 'release'})}
        for index, name in enumerate(sorted(promotion.expected_artifacts()), start=1):
            self.put(index, name, self.snapshot if name == 'source-snapshot' else {'fixture': name})
        self.receipt = {'schema': 1, 'repository': promotion.REPOSITORY, 'workflow': self.run['path'],
                        'run_id': '123', 'run_attempt': 1, 'source_revision': SHA, 'manifest_sha256': MANIFEST,
                        'version': '1.0.0-alpha.1', 'builder_image': IMAGE,
                        'frozen_manifest_sha256': promotion.art.digest(b'manifest: {}\n'),
                        'strict': True, 'cli_profile': 'release',
                        'artifacts': [promotion.descriptor(item) for item in self.items.values()]}
        self.seal()

    def put(self, ident, name, files):
        data = zipped(files)
        self.data[ident] = data
        self.items[ident] = {'id': ident, 'name': name, 'digest': 'sha256:' + promotion.art.digest(data),
                             'size_in_bytes': len(data), 'expired': False, 'workflow_run': {'id': 123}}

    def seal(self):
        self.put(1000, 'candidate-provenance-1', {'candidate-provenance.json': json.dumps(self.receipt)})

    def request(self, url, **kwargs):
        self.requests.append((url, kwargs))
        path = url.removeprefix(self.base).split('?')[0]
        if path == '/actions/workflows/candidates.yml/runs':
            return {'workflow_runs': [copy.deepcopy(self.run)]}
        if path == '/actions/runs/123':
            return copy.deepcopy(self.run)
        if path == '/actions/runs/123/artifacts':
            return {'artifacts': copy.deepcopy(list(self.items.values()))}
        if path.startswith('/actions/artifacts/'):
            ident = int(path.split('/')[3])
            return self.data[ident] if path.endswith('/zip') else copy.deepcopy(self.items[ident])
        raise AssertionError('unexpected API call: ' + path)


class Promotion(unittest.TestCase):
    def setUp(self):
        self.github = GitHub()

    def select(self, run_id=None):
        return promotion.select(self.github, SHA, MANIFEST, TAG, run_id)

    def test_auto_and_explicit_select_exact_candidate_builder_and_receipt(self):
        for run_id in (None, '123'):
            selected = self.select(run_id)
            self.assertEqual(selected, {'mode': 'reused', 'run_id': '123', 'run_attempt': 1,
                                       'receipt_id': '1000', 'image': IMAGE})

    def test_absent_expired_and_old_qualifications_fall_back_only_automatically(self):
        for change in ('absent', 'expired', 'old', 'missing-part', 'latest-attempt'):
            self.github = GitHub()
            if change == 'absent':
                del self.github.items[1000]
            elif change == 'expired':
                self.github.items[1000]['expired'] = True
            elif change == 'old':
                self.github.receipt['schema'] = 0
                self.github.seal()
            elif change == 'missing-part':
                del self.github.items[1]
            else:
                self.github.run['run_attempt'] = 2
            with self.subTest(change=change):
                self.assertEqual(self.select()['mode'], 'fresh')
                with self.assertRaises(promotion.Unavailable):
                    self.select('123')

    def test_explicit_source_repo_workflow_branch_and_attempt_conflicts_fail(self):
        for field, value in (('head_sha', 'd' * 40), ('path', '.github/workflows/ci.yml'),
                             ('head_branch', 'other'), ('event', 'pull_request'),
                             ('repository', {'full_name': 'fork/repository'}), ('conclusion', 'failure')):
            self.github = GitHub()
            self.github.run[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.select('123')
        self.github = GitHub()
        self.github.receipt['run_attempt'] = 2
        self.github.seal()
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.select('123')

    def test_corrupt_digest_and_api_errors_do_not_fall_back(self):
        self.github.data[1000] += b'corrupt'
        with self.assertRaisesRegex(ValueError, 'digest'):
            self.select()
        with patch.object(self.github, 'request', side_effect=urllib.error.URLError('offline')):
            with self.assertRaises(urllib.error.URLError):
                self.select()

    def test_receipt_cannot_qualify_development_or_incomplete_inputs(self):
        for update in ({'strict': False}, {'cli_profile': 'ci'}, {'builder_image': 'stable'},
                       {'version': '1.0.0-alpha.2'}, {'artifacts': []}):
            self.github = GitHub()
            self.github.receipt.update(update)
            self.github.seal()
            with self.subTest(update=update), self.assertRaises(ValueError):
                self.select()

    def test_fetch_pins_inputs_and_preserves_candidate_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = promotion.fetch(self.github, '1000', '123', SHA, MANIFEST, TAG, root, 'reused')
            self.assertEqual(evidence['run_id'], '123')
            self.assertEqual(evidence['receipt_id'], '1000')
            self.assertEqual(evidence['mode'], 'reused')
            self.assertTrue((root / 'firmware-candidate/fixture').is_file())
            self.assertEqual(len(list((root / 'cli-parts').iterdir())), 6)
            self.assertEqual(len(list((root / 'cli-native').iterdir())), 6)
            self.assertEqual(json.loads((root / 'snapshot/candidate-reuse.json').read_text()), evidence)
        self.assertTrue(all('method' not in args for _, args in self.github.requests))

    def test_fetch_rejects_cross_run_artifact_and_changed_snapshot(self):
        self.github.items[1]['workflow_run']['id'] = 456
        with tempfile.TemporaryDirectory() as temporary, self.assertRaisesRegex(ValueError, 'run conflict'):
            promotion.fetch(self.github, '1000', '123', SHA, MANIFEST, TAG, Path(temporary), 'reused')
        self.github = GitHub()
        ident = next(item['id'] for item in self.github.items.values() if item['name'] == 'source-snapshot')
        self.github.snapshot['image.txt'] = IMAGE.replace('c' * 64, 'd' * 64)
        self.github.put(ident, 'source-snapshot', self.github.snapshot)
        self.github.receipt['artifacts'] = [promotion.descriptor(self.github.items[item['id']])
                                             for item in self.github.receipt['artifacts']]
        self.github.seal()
        with tempfile.TemporaryDirectory() as temporary, self.assertRaisesRegex(ValueError, 'snapshot conflict'):
            promotion.fetch(self.github, '1000', '123', SHA, MANIFEST, TAG, Path(temporary), 'reused')

    def test_fresh_fetch_only_accepts_current_alpha_run_and_attempt(self):
        self.github.run.update(path=promotion.ALPHA, status='in_progress', conclusion=None)
        self.github.receipt['workflow'] = promotion.ALPHA
        self.github.seal()
        with tempfile.TemporaryDirectory() as temporary, \
                patch.dict('os.environ', GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='1'):
            evidence = promotion.fetch(self.github, '1000', '123', SHA, MANIFEST, TAG, Path(temporary), 'fresh')
            self.assertEqual(evidence['mode'], 'fresh')
        with tempfile.TemporaryDirectory() as temporary, \
                patch.dict('os.environ', GITHUB_RUN_ID='456', GITHUB_RUN_ATTEMPT='1'), self.assertRaises(ValueError):
            promotion.fetch(self.github, '1000', '123', SHA, MANIFEST, TAG, Path(temporary), 'fresh')

    def test_archive_rejects_traversal_duplicates_symlinks_and_size(self):
        for name in ('../escape', '/absolute', 'dir\\file'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                promotion.members(zipped({name: b'bad'}))
        with self.assertRaisesRegex(ValueError, 'bound'):
            promotion.members(zipped({'large': b'12345'}), maximum=4)
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            link = zipfile.ZipInfo('link')
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(link, 'target')
        with self.assertRaisesRegex(ValueError, 'special file'):
            promotion.members(stream.getvalue())

    def test_qualification_requires_successful_complete_candidate(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = Path(temporary)
            for name, data in self.github.snapshot.items():
                (snapshot / name).write_text(data)
            needs = {name: {'result': 'success'} for name in ('validation', 'products', 'assemble')}
            with patch.dict('os.environ', NEEDS_JSON=json.dumps(needs)), \
                    patch.object(promotion, 'source_tag', return_value=TAG), \
                    patch.object(promotion.alpha, 'checkout_identity'):
                result = promotion.qualify(self.github, snapshot, snapshot, '123', 1)
                self.assertEqual(len(result['artifacts']), 14)
            needs['products']['result'] = 'skipped'
            with patch.dict('os.environ', NEEDS_JSON=json.dumps(needs)), self.assertRaises(ValueError):
                promotion.qualify(self.github, snapshot, snapshot, '123', 1)

    def test_non_alpha_candidate_keeps_success_without_promotion_receipt(self):
        needs = {name: {'result': 'success'} for name in ('validation', 'products', 'assemble')}
        with patch.object(sys, 'argv', ['promotion.py', 'qualify']), \
                patch.dict('os.environ', GITHUB_SHA=SHA, RELEASE_TAG='', NEEDS_JSON=json.dumps(needs)), \
                patch.object(promotion, 'source_tag', return_value='v1.0.0'), \
                patch.object(promotion, 'outputs') as outputs, \
                patch.object(promotion.publish, 'GitHub') as github:
            promotion.main()
        outputs.assert_called_once_with({'eligible': 'false'})
        github.assert_not_called()


class Workflow(unittest.TestCase):
    def test_manual_staging_has_no_tag_preflight_or_publication(self):
        import yaml
        root = Path(__file__).resolve().parents[3]
        workflow = yaml.safe_load((root / '.github/workflows/alpha-release.yml').read_text())
        self.assertIn('workflow_dispatch', workflow[True])
        self.assertEqual(workflow['permissions'], {'contents': 'read', 'actions': 'read'})
        jobs = workflow['jobs']
        self.assertIn("github.event_name == 'push'", jobs['publish']['if'])
        preflight = next(step for step in jobs['preflight']['steps'] if step.get('id') == 'check')
        self.assertEqual(preflight['if'], "github.event_name == 'push'")
        self.assertIn("needs.candidate.result == 'skipped'", jobs['stage']['if'])
        self.assertIn("needs.candidate.result == 'success'", jobs['stage']['if'])
        self.assertIn("needs.select.result == 'success'", jobs['stage']['if'])
        self.assertEqual([name for name, job in jobs.items() if job.get('permissions') == {'contents': 'write'}], ['publish'])
        commands = '\n'.join(step.get('run', '') for step in jobs['stage']['steps'])
        self.assertIn('vulnerabilities --current', commands)
        self.assertIn('west-vulnerabilities --current', commands)
        self.assertIn('promotion.py fetch', commands)


if __name__ == '__main__':
    unittest.main()
