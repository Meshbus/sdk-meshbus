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


class ProductReuse(Reuse):
    def setUp(self):
        super().setUp()
        self.files['shards.json'] = json.dumps({'schema': 2, 'include': [
            {'id': 'mixed-0', 'name': 'Twister Run + Build (1)', 'tasks': [
                {'layer': 'runtime', 'shard': 0}, {'layer': 'compile', 'shard': 0}]}]}).encode()
        self.files['products.json'] = json.dumps({'include': [dict(id='r1', board='r1/soc')]}).encode()
        self.files['product-builds.json'] = json.dumps({'include': [
            dict(id='r1', board='r1/soc', profile='default'), dict(id='r1', board='r1/soc', profile='prod')]}).encode()
        selected = json.loads(self.files['plan.json'])
        selected['product_builds'] = True
        self.files['plan.json'] = json.dumps(selected).encode()
        self.jobs = [{'name': 'validation / ' + name, 'status': 'completed', 'conclusion': 'success'}
                     for name in ('Twister Run + Build (1)', 'Firmware / r1 (default)',
                                  'Firmware / r1 (prod)', 'Required checks')]

    def test_complete_products_and_actual_tasks_are_recorded(self):
        result = self.record()
        self.assertEqual(result['schema'], 2)
        self.assertEqual(len(result['product_builds']), 2)
        self.assertEqual(len(result['twister_jobs'][0]['tasks']), 2)

    def test_missing_default_extra_profile_failure_and_duplicates_fail(self):
        original = copy.deepcopy(self.jobs)
        for index in (1, 2):
            for state in ('skipped', 'failure', 'cancelled'):
                self.jobs = copy.deepcopy(original)
                self.jobs[index]['conclusion'] = state
                with self.subTest(index=index, state=state), self.assertRaises(ValueError):
                    self.record()
        self.jobs = original
        self.files['product-builds.json'] = json.dumps({'include': [
            dict(id='r1', board='r1/soc', profile='prod')]}).encode()
        with self.assertRaisesRegex(ValueError, 'default-product'):
            self.record()

    def test_duplicate_or_missing_twister_task_and_benchmark_fail(self):
        original = self.files['shards.json']
        for tasks in ([{'layer': 'runtime', 'shard': 0}],
                      [{'layer': 'runtime', 'shard': 0}, {'layer': 'runtime', 'shard': 0},
                       {'layer': 'compile', 'shard': 0}]):
            matrix = json.loads(original)
            matrix['include'][0]['tasks'] = tasks
            self.files['shards.json'] = json.dumps(matrix).encode()
            with self.assertRaises(ValueError):
                self.record()
        self.files['shards.json'] = original
        self.files['plan.json'] = json.dumps(json.loads(self.files['plan.json']) | {'benchmark': {'case': 'full'}}).encode()
        with self.assertRaises(ValueError):
            self.record()

    def test_current_default_subset_is_covered_but_changed_inventory_is_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            snapshot = Path(temporary)
            for name, content in self.files.items():
                (snapshot / name).write_bytes(content)
            baseline.art.write_json(snapshot / 'twister-baseline.json', self.record())
            baseline.art.write_json(snapshot / 'product-builds.json', {'include': [
                dict(id='r1', board='r1/soc', profile='default')]})
            self.assertEqual(baseline.verify(snapshot)['schema'], 2)
            baseline.art.write_json(snapshot / 'products.json', {'include': [dict(id='r2', board='r2/soc')]})
            with self.assertRaises(ValueError):
                baseline.verify(snapshot)


if __name__ == '__main__':
    unittest.main()
