# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
from collections import Counter
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import benchmark
import test_plan
import timings


def suite(name, platform='native_sim/native'):
    return dict(name=name, platform=platform, toolchain='host/gnu', status='None')


class Scheduling(unittest.TestCase):
    def test_weighted_partition_preserves_every_instance_and_improves_balance(self):
        suites = [suite(str(i)) for i in range(24)]
        report = {'testsuites': suites}
        weights = {test_plan.key(s): 100 if i % 2 == 0 else 1 for i, s in enumerate(sorted(suites, key=test_plan.key))}
        old = test_plan.partition(report)
        new = test_plan.partition(report, weights=weights)
        def score(groups):
            return max(sum(weights[test_plan.key(s)] for s in g['testsuites']) for g in groups)
        self.assertLess(score(new), score(old))
        self.assertEqual(Counter(test_plan.key(s) for g in new for s in g['testsuites']),
                         Counter(test_plan.key(s) for s in suites))
        self.assertEqual(new, test_plan.partition(report, weights=weights))

    def test_bad_or_empty_weights_use_round_robin(self):
        report = {'testsuites': [suite(str(i)) for i in range(30)]}
        baseline = test_plan.partition(report)
        for value in (-1, 0, float('nan'), float('inf'), True, '5'):
            with self.subTest(value=value):
                self.assertEqual(test_plan.partition(report, weights={test_plan.key(suite('0')): value}), baseline)
        self.assertEqual(test_plan.partition(report, weights={}), baseline)

    def test_weighted_choice_is_never_worse_than_round_robin(self):
        report = {'testsuites': [suite(str(i)) for i in range(49)]}
        for factor in (3, 5, 9):
            weights = {test_plan.key(s): (i * factor) % 29 + 1 for i, s in enumerate(report['testsuites'])}
            def cost(groups):
                return max(sum(weights[test_plan.key(s)] for s in g['testsuites']) for g in groups)
            self.assertLessEqual(cost(test_plan.partition(report, weights=weights)), cost(test_plan.partition(report)))

    def generate(self, runtime, compile_suites, scheduler='balanced'):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            snapshot = root / 'snapshot'
            snapshot.mkdir()
            selection = {'full': True, 'test_roots': ['tests'], 'compile_roots': ['tests', 'samples'],
                         'scheduler': scheduler}
            if scheduler == 'legacy':
                selection['benchmark'] = {'case': 'full', 'inventory_sha256': ''}
            (snapshot / 'plan.json').write_text(json.dumps(selection))
            def discover(command, **kwargs):
                target = Path(command[command.index('--save-tests') + 1])
                suites = runtime if target.stem == 'runtime' else compile_suites
                target.write_text(json.dumps({'testsuites': suites}))
            with patch.object(test_plan.subprocess, 'run', side_effect=discover):
                test_plan.generate(root, snapshot)
            return {p.name: json.loads(p.read_text()) for p in snapshot.glob('*.json')}

    def test_small_dual_layer_shares_one_job_and_keeps_distinct_plans(self):
        records = self.generate([suite('run')], [suite('run'), suite('build', 'board/soc')])
        jobs = records['shards.json']
        self.assertEqual(jobs['schema'], 2)
        self.assertEqual(jobs['include'], [{'id': 'mixed-0', 'name': 'Twister Run + Build (1)',
                                          'tasks': [{'layer': 'runtime', 'shard': 0}, {'layer': 'compile', 'shard': 0}]}])
        self.assertEqual([s['name'] for s in records['compile-0.json']['testsuites']], ['build'])
        self.assertEqual(len(records['twister-inventory.json']['instances']), 2)

    def test_legacy_small_stays_separate_and_has_same_inventory(self):
        runtime, compile_suites = [suite('run')], [suite('run'), suite('build', 'board/soc')]
        legacy = self.generate(runtime, compile_suites, 'legacy')
        balanced = self.generate(runtime, compile_suites)
        self.assertEqual(len(legacy['shards.json']['include']), 2)
        self.assertEqual(legacy['twister-inventory.json'], balanced['twister-inventory.json'])

    def test_boundary_and_single_layers(self):
        for count, jobs in ((11, 1), (12, 2), (24, 3)):
            # One runtime plus count compile instances.
            records = self.generate([suite('run')], [suite(f'b{i}', 'board/soc') for i in range(count)])
            self.assertEqual(len(records['shards.json']['include']), jobs)
        for runtime, compile_suites in (([suite('run')], [suite('run')]), ([], [suite('build')])):
            records = self.generate(runtime, compile_suites)
            self.assertEqual(len(records['shards.json']['include']), 1)
            self.assertNotEqual(records['shards.json']['include'][0]['id'], 'mixed-0')
        with self.assertRaisesRegex(ValueError, 'no runnable'):
            self.generate([], [])

    def test_duplicate_jobs_and_tasks_fail(self):
        row = {'id': 'runtime-0', 'name': 'Run', 'tasks': [{'layer': 'runtime', 'shard': 0}]}
        self.assertEqual(test_plan.job_tasks({'schema': 2, 'include': [row]}), [row])
        for rows in ([row, row], [row, dict(row, id='other', name='Other')], [dict(row, tasks=[])],
                     [dict(row, tasks=[{'layer': 'runtime', 'shard': -1}])]):
            with self.assertRaises(ValueError):
                test_plan.job_tasks({'schema': 2, 'include': rows})

    def test_legacy_matrix_remains_readable(self):
        self.assertEqual(test_plan.job_tasks({'include': [{'layer': 'runtime', 'shard': 0, 'name': 'Run'}]}),
                         [{'id': 'runtime-0', 'name': 'Run', 'tasks': [{'layer': 'runtime', 'shard': 0}]}])

    def test_snapshot_rejects_missing_task_even_with_matching_instance_union(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            snapshot, reports = root / 'snapshot', root / 'reports'
            snapshot.mkdir()
            target = reports / 'runtime-0'
            target.mkdir(parents=True)
            passed = suite('run') | {'status': 'passed', 'execution_time': 1}
            for layer, suites in (('runtime', [passed]), ('compile', [])):
                (snapshot / f'{layer}.json').write_text(json.dumps({'testsuites': suites}))
            (target / 'twister.json').write_text(json.dumps({'testsuites': [passed]}))
            (snapshot / 'shards.json').write_text(json.dumps({'schema': 2, 'include': [
                {'id': 'runtime-1', 'name': 'Run', 'tasks': [{'layer': 'runtime', 'shard': 1}]}]}))
            with self.assertRaisesRegex(ValueError, 'task report coverage'):
                test_plan.verify_snapshot(snapshot, reports)


class Benchmark(unittest.TestCase):
    source = 'a' * 40
    image = 'ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'b' * 64

    def environment(self):
        return dict(GITHUB_WORKFLOW_REF=timings.REPOSITORY + '/.github/workflows/ci-benchmark.yml@refs/heads/main',
                    GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REPOSITORY=timings.REPOSITORY,
                    GITHUB_REF='refs/heads/main', GITHUB_SHA=self.source)

    def test_regular_validation_rejects_experiment_overrides(self):
        self.assertIsNone(benchmark.configure(Path('/unused')))
        for kwargs in ({'scheduler': 'legacy'}, {'source': self.source}, {'history_artifact': '1'}):
            with self.assertRaisesRegex(ValueError, 'ordinary validation'):
                benchmark.configure(Path('/unused'), **kwargs)

    def test_fixed_small_selection_uses_trusted_caller_and_exact_source(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(benchmark.subprocess, 'check_output', return_value=self.source):
            selected = benchmark.configure(Path(temp), 'clock-small', 'legacy', self.source, self.image,
                                           environment=self.environment())
            self.assertFalse(selected['products'])
            self.assertEqual(selected['test_roots'], ['tests/subsys/clock'])
            self.assertEqual(selected['compile_roots'], ['samples/subsys/clock', 'tests/subsys/clock'])
            for key, value in (('GITHUB_EVENT_NAME', 'push'), ('GITHUB_REPOSITORY', 'other/repo'),
                               ('GITHUB_WORKFLOW_REF', timings.REPOSITORY + '/.github/workflows/ci.yml@refs/heads/main'),
                               ('GITHUB_SHA', 'c' * 40)):
                with self.subTest(key=key), self.assertRaises(ValueError):
                    benchmark.configure(Path(temp), 'clock-small', 'legacy', self.source, self.image,
                                        environment=self.environment() | {key: value})
            with self.assertRaises(ValueError):
                benchmark.configure(Path(temp), 'clock-small', 'balanced', self.source, 'stable',
                                    environment=self.environment())

    def measurement(self, scheduler, run, runner, wall):
        instances = [{'layer': layer, 'name': layer, 'platform': 'p', 'toolchain': 't'} for layer in ('runtime', 'compile')]
        return {'benchmark': {'case': 'clock-small', 'inventory_sha256': ''}, 'scheduler': scheduler,
                'source_revision': self.source, 'builder_image': self.image, 'history_sha256': 'c' * 64,
                'inventory': {'instances': instances,
                              'sha256': timings.digest(timings.encoded(sorted(instances, key=timings.identity)))},
                'run_id': str(run), 'run_attempt': 1,
                'job_metrics': {'runner_minutes': runner, 'wall_seconds': wall}}

    def test_comparison_requires_fixed_distinct_observations_and_thresholds(self):
        old = [self.measurement('legacy', i, 10, 300) for i in (1, 2)]
        new = [self.measurement('balanced', i, 8, 350) for i in (3, 4)]
        self.assertTrue(benchmark.compare(old, new)['passed'])
        new[1]['history_sha256'] = 'e' * 64
        with self.assertRaisesRegex(ValueError, 'identical'):
            benchmark.compare(old, new)
        new = [self.measurement('balanced', i, 10, 361) for i in (3, 4)]
        result = benchmark.compare(old, new)
        self.assertFalse(result['runner_passed'])
        self.assertFalse(result['wall_passed'])
        with self.assertRaises(ValueError):
            benchmark.compare(old[:1], new)

    def test_small_inventory_cannot_be_truncated_or_single_layer(self):
        selected = {'benchmark': {'case': 'clock-small', 'inventory_sha256': ''}}
        for reports in ({'runtime': {'testsuites': [suite('one')]}},
                        {'runtime': {'testsuites': [suite(str(i)) for i in range(12)]},
                         'compile': {'testsuites': [suite('build')]}}):
            with self.assertRaises(ValueError):
                benchmark.verify_inventory(selected, timings.inventory(reports))


if __name__ == '__main__':
    unittest.main()
