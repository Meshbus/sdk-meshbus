# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import baseline
import checks
import plan

SHA = 'a' * 40
MANIFEST = 'b' * 64
IMAGE = 'ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'c' * 64


class Reuse(unittest.TestCase):
    def setUp(self):
        self.run = {'head_sha': SHA, 'head_branch': 'main', 'path': '.github/workflows/ci.yml',
                    'event': 'push', 'status': 'completed', 'conclusion': 'success',
                    'repository': {'full_name': baseline.REPOSITORY}, 'id': 123, 'run_attempt': 1}
        self.files = {'source.json': json.dumps({'source_revision': SHA, 'manifest_sha256': MANIFEST}).encode(),
                      'plan.json': json.dumps(plan.select([], full=True)).encode(),
                      'image.txt': IMAGE.encode(), 'west-frozen.yml': b'manifest: {}\n',
                      'shards.json': json.dumps({'include': [
                          {'name': 'Twister Run (1)', 'layer': 'runtime'},
                          {'name': 'Twister Build (1)', 'layer': 'compile'}]}).encode()}
        self.jobs = [{'name': 'validation / ' + name, 'status': 'completed', 'conclusion': 'success'}
                     for name in ('Twister Run (1)', 'Twister Build (1)', 'Required checks')]

    def record(self):
        return baseline.record(self.run, self.files, self.jobs, SHA, MANIFEST, IMAGE)

    def test_reuse_keeps_non_twister_required_work(self):
        selected = plan.select([], full=True)
        selected.update(sdk=False, twister_baseline=self.record())
        self.assertNotIn('zephyr', checks.expected_jobs(selected))
        self.assertTrue({'host', 'products', 'native', 'cli-linux', 'cli-mac', 'quality'} <=
                        set(checks.expected_jobs(selected)))
        self.assertEqual(len(selected['native_targets']), 6)
        self.assertEqual(self.record()['run_id'], '123')

    def test_different_source_builder_or_manifest_cannot_be_reused(self):
        for sha, manifest, image in (('d' * 40, MANIFEST, IMAGE), (SHA, 'd' * 64, IMAGE),
                                    (SHA, MANIFEST, IMAGE.replace('c' * 64, 'd' * 64))):
            with self.subTest(sha=sha, manifest=manifest, image=image), self.assertRaises(ValueError):
                baseline.record(self.run, self.files, self.jobs, sha, manifest, image)

    def test_narrowed_ci_is_not_a_full_baseline(self):
        selected = json.loads(self.files['plan.json'])
        for change in ({'full': False}, {'sdk': False}, {'test_roots': ['tests/subsys/clock']},
                       {'compile_roots': ['samples']}):
            self.files['plan.json'] = json.dumps(selected | change).encode()
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.record()

    def test_only_successful_trusted_main_ci_is_eligible(self):
        original = copy.deepcopy(self.run)
        for change in ({'event': 'pull_request'}, {'head_branch': 'other'}, {'conclusion': 'failure'},
                       {'status': 'in_progress'}, {'path': '.github/workflows/candidates.yml'},
                       {'repository': {'full_name': 'fork/sdk-meshbus'}}):
            self.run = original | change
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.record()

    def test_missing_duplicate_skipped_or_failed_twister_jobs_fail(self):
        original = copy.deepcopy(self.jobs)
        for change in ('missing', 'duplicate', 'skipped', 'failure', 'cancelled'):
            self.jobs = copy.deepcopy(original)
            if change == 'missing':
                self.jobs.pop()
            elif change == 'duplicate':
                self.jobs.append(copy.deepcopy(self.jobs[0]))
            else:
                self.jobs[0]['conclusion'] = change
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.record()

    def test_snapshot_paths_and_size_are_bounded(self):
        for extra in (None, '../source.json', 'source.json'):
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w') as zipped:
                for name, content in self.files.items():
                    zipped.writestr(name, content)
                if extra:
                    zipped.writestr(extra, b'unsafe')
            if extra:
                with self.assertRaises(ValueError):
                    baseline.archive(data.getvalue())
            else:
                self.assertEqual(baseline.archive(data.getvalue()), self.files)
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w', compression=zipfile.ZIP_DEFLATED) as zipped:
            zipped.writestr('oversized', b'x' * (16 * 1024 * 1024 + 1))
        with self.assertRaises(ValueError):
            baseline.archive(data.getvalue())

    def test_current_dependency_graph_must_match_tested_graph(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = Path(temporary)
            for name, content in self.files.items():
                (snapshot / name).write_bytes(content)
            baseline.art.write_json(snapshot / 'twister-baseline.json', self.record())
            self.assertEqual(baseline.verify(snapshot), self.record())
            for name in ('west-frozen.yml', 'source.json', 'image.txt'):
                original = (snapshot / name).read_bytes()
                replacement = b'manifest: changed' if name.endswith('.yml') else original.replace(b'a', b'd')
                (snapshot / name).write_bytes(replacement)
                with self.subTest(name=name), self.assertRaises(ValueError):
                    baseline.verify(snapshot)
                (snapshot / name).write_bytes(original)

    def test_missing_baseline_fails_with_ci_instruction(self):
        class Empty:
            base = 'https://api.github.com/repos/' + baseline.REPOSITORY

            def request(self, url):
                return {'workflow_runs': []}
        with self.assertRaisesRegex(ValueError, 'Complete CI on this commit'):
            baseline.select(Empty(), SHA, MANIFEST, IMAGE)


if __name__ == '__main__':
    unittest.main()
