# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import timings


class Timings(unittest.TestCase):
    image = 'ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'a' * 64

    def row(self, build=10, execution=1):
        return dict(layer='runtime', name='clock', platform='native_sim/native', toolchain='host/gnu',
                    build_time=build, execution_time=execution)

    def fixture_run(self, run_id=1):
        return dict(id=run_id, run_attempt=1, repository={'full_name': timings.REPOSITORY}, head_branch='main',
                    head_sha='b' * 40, path='.github/workflows/ci.yml', event='push',
                    status='completed', conclusion='success')

    def record(self, run_id=1, build=10):
        return dict(schema=1, repository=timings.REPOSITORY, run_id=str(run_id), run_attempt=1,
                    builder_image=self.image, source_revision='b' * 40, benchmark=False,
                    instances=[self.row(build)])

    def zipped(self, record, name='timings.json'):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            archive.writestr(name, json.dumps(record))
        return stream.getvalue()

    def github(self, count=4, builds=(10, 20, 90, 200)):
        class Fake:
            base = 'https://api.github.com/repos/' + timings.REPOSITORY
        fake = Fake()
        self.calls = []
        def request(url, **kwargs):
            self.calls.append(url)
            if '/actions/workflows/' in url:
                return {'workflow_runs': [self.fixture_run(i) for i in range(1, count + 1)]}
            if '/actions/runs/' in url and '/artifacts?' in url:
                run_id = int(url.split('/actions/runs/')[1].split('/')[0])
                return {'artifacts': [{'id': run_id, 'name': 'ci-timings', 'expired': False}]}
            if '/actions/artifacts/' in url:
                artifact_id = int(url.split('/actions/artifacts/')[1].split('/')[0])
                if url.endswith('/zip'):
                    return self.zipped(self.record(artifact_id, builds[artifact_id - 1]))
                return {'id': artifact_id, 'name': 'ci-timings', 'expired': False, 'workflow_run': {'id': artifact_id}}
            if '/actions/runs/' in url:
                return self.fixture_run(int(url.rsplit('/', 1)[1]))
            raise AssertionError(url)
        fake.request = request
        return fake

    def test_history_uses_at_most_three_records_and_median(self):
        result = timings.fetch_history(self.github(), self.image)
        self.assertEqual(len(result['sources']), 3)
        self.assertEqual(result['weights'][0]['seconds'], 21)
        self.assertFalse(any('/runs/4/' in url for url in self.calls))

    def test_builder_mismatch_and_no_artifacts_are_safe_fallbacks(self):
        result = timings.fetch_history(self.github(), 'another-image')
        self.assertEqual(result['weights'], [])
        self.assertTrue(result['fallback'])
        self.assertEqual(len(result['rejected']), 4)
        result = timings.fetch_history(self.github(count=0), self.image)
        self.assertEqual(result['weights'], [])

    def test_untrusted_runs_are_not_downloaded(self):
        for key, value in (('head_branch', 'feature'), ('path', '.github/workflows/ci-benchmark.yml'),
                           ('event', 'pull_request'), ('conclusion', 'failure')):
            with self.subTest(key=key), patch.object(self, 'fixture_run', return_value=Timings.fixture_run(self) | {key: value}):
                result = timings.fetch_history(self.github(), self.image)
                self.assertEqual(result['weights'], [])
                self.assertEqual(len(self.calls), 1)

    def test_archive_traversal_extra_files_and_size_fail(self):
        for name in ('../timings.json', '/timings.json', 'other.json'):
            with self.assertRaises(ValueError):
                timings.unpack(self.zipped(self.record(), name))
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            archive.writestr('timings.json', '{}')
            archive.writestr('extra', '')
        with self.assertRaises(ValueError):
            timings.unpack(stream.getvalue())
        with self.assertRaises(ValueError):
            timings.unpack(b'x' * (timings.MAX_BYTES + 1))

    def test_durations_and_duplicate_rows_fail(self):
        for value in (-1, float('nan'), float('inf'), True, 1000000, 'invalid'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                timings.validate_rows([self.row(value)], 'build_time')
        with self.assertRaises(ValueError):
            timings.validate_rows([self.row(), self.row()], 'build_time')
        self.assertEqual(timings.seconds('0.00'), 0)

    def test_pinned_history_checks_checksum_and_attempt(self):
        record = self.record()
        checksum = timings.digest(json.dumps(record).encode())
        result = timings.fetch_history(self.github(), self.image, '1', checksum)
        self.assertEqual(len(result['sources']), 1)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            timings.fetch_history(self.github(), self.image, '1', '0' * 64)
        github = self.github()
        with patch.object(github, 'request', return_value=self.zipped(record | {'run_attempt': 2})):
            with self.assertRaisesRegex(ValueError, 'identity'):
                timings.artifact_record(github, {'id': 1, 'name': 'ci-timings', 'expired': False}, self.fixture_run(), self.image)

    def test_history_io_failure_falls_back_but_pinned_benchmark_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            snapshot = Path(temp)
            (snapshot / 'plan.json').write_text('{}')
            github = self.github()
            with patch.object(github, 'request', side_effect=OSError('unavailable')):
                result = timings.history(snapshot, self.image, github)
                self.assertEqual(result['weights'], [])
                self.assertIn('unavailable', result['fallback'])
                (snapshot / 'plan.json').write_text(json.dumps({'benchmark': {'history_artifact': '1', 'history_sha256': 'a' * 64}}))
                with self.assertRaisesRegex(ValueError, 'pinned benchmark'):
                    timings.history(snapshot, self.image, github)

    def test_corrupt_or_absent_local_history_keeps_equal_grouping(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'history.json'
            self.assertEqual(timings.load_history(path)['weights'], [])
            for value in ('not json', '{"schema":2,"weights":[]}',
                          json.dumps({'schema': 1, 'weights': [self.row() | {'seconds': -1}]})):
                path.write_text(value)
                self.assertEqual(timings.load_history(path)['weights'], [])

    def test_job_metrics_include_setup_and_steps(self):
        github = self.github()
        jobs = [{'name': 'validation / Mixed', 'status': 'completed',
                 'started_at': '2026-10-02T01:00:00Z', 'completed_at': '2026-10-02T01:04:00Z',
                 'steps': [{'name': 'workspace', 'started_at': '2026-10-02T01:00:00Z',
                            'completed_at': '2026-10-02T01:01:00Z'}]}]
        with patch.object(github, 'request', return_value={'jobs': jobs}):
            result = timings.job_metrics(github, '1', 1, ['Mixed'])
            self.assertEqual(result['runner_minutes'], 4)
            self.assertEqual(result['wall_seconds'], 240)
            self.assertEqual(result['jobs'][0]['steps'][0]['seconds'], 60)
            with self.assertRaises(ValueError):
                timings.job_metrics(github, '1', 1, ['Mixed', 'Missing'])

    def test_collect_requires_coverage_before_producing_hints(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            snapshot, reports = root / 'snapshot', root / 'reports'
            snapshot.mkdir()
            (snapshot / 'runtime.json').write_text(json.dumps({'testsuites': [self.row()]}))
            (snapshot / 'compile.json').write_text(json.dumps({'testsuites': []}))
            with self.assertRaises(ValueError):
                timings.collect(snapshot, reports, root / 'timings.json')
            self.assertFalse((root / 'timings.json').exists())

    def test_collect_records_independent_layers_and_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            snapshot, reports = root / 'snapshot', root / 'reports'
            snapshot.mkdir()
            source = {'source_revision': 'b' * 40, 'manifest_sha256': 'c' * 64}
            (snapshot / 'source.json').write_text(json.dumps(source))
            (snapshot / 'image.txt').write_text(self.image)
            (snapshot / 'plan.json').write_text('{}')
            matrix = {'schema': 2, 'include': [{'id': 'mixed-0', 'name': 'Mixed', 'tasks': [
                {'layer': 'runtime', 'shard': 0}, {'layer': 'compile', 'shard': 0}]}]}
            (snapshot / 'shards.json').write_text(json.dumps(matrix))
            inventories = {}
            for layer in ('runtime', 'compile'):
                suite = self.row() | {'name': layer, 'status': 'passed' if layer == 'runtime' else 'not run'}
                if layer == 'compile':
                    suite['execution_time'] = 0
                report = {'testsuites': [suite]}
                inventories[layer] = report
                for filename in (f'{layer}.json', f'{layer}-0.json'):
                    (snapshot / filename).write_text(json.dumps(report))
                folder = reports / 'zephyr-mixed-0' / f'{layer}-0'
                folder.mkdir(parents=True)
                (folder / 'twister.json').write_text(json.dumps(report))
                (folder / 'task-timing.json').write_text(json.dumps({'layer': layer, 'shard': 0, 'seconds': 5}))
            (snapshot / 'twister-inventory.json').write_text(json.dumps(timings.inventory(inventories)))
            predictions = timings.predictions(snapshot, matrix, {'weights': []})
            (snapshot / 'scheduling.json').write_text(json.dumps(predictions))
            with patch.dict(timings.os.environ, GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2'), \
                    patch.object(timings, 'job_metrics', return_value={'runner_minutes': 2, 'wall_seconds': 120}):
                result = timings.collect(snapshot, reports, root / 'out/timings.json', self.github())
            self.assertEqual(result['run_attempt'], 2)
            self.assertEqual(result['source_revision'], source['source_revision'])
            self.assertEqual(len(result['tasks']), 2)
            self.assertEqual({row['layer'] for row in result['instances']}, {'runtime', 'compile'})
            self.assertFalse(result['benchmark'])
            self.assertIsNone(result['scheduling']['jobs'][0]['predicted_seconds'])


if __name__ == '__main__':
    unittest.main()
