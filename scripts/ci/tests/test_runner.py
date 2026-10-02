# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run as runner


class Runner(unittest.TestCase):
    def test_product_configuration_argument_reaches_sysbuild(self):
        for profile in ('default', 'dev', 'prod'):
            with self.subTest(profile=profile), patch.object(runner, 'run') as execute:
                runner.product('mesh_probe_r2/nrf54l15/cpuapp', False, profile)
                command = execute.call_args.args
                self.assertEqual(command[:3], ('west', 'build', '--sysbuild'))
                self.assertIn(runner.WORKSPACE / 'product-build' / profile, command)
                extra = f'-Dmeshbus_EXTRA_CONF_FILE={runner.ROOT / "apps/meshbus" / f"prj.{profile}.conf"}'
                if profile == 'default':
                    self.assertNotIn('--', command)
                else:
                    self.assertEqual(command[-2:], ('--', extra))
        with self.assertRaises(ValueError), patch.object(runner, 'run') as execute:
            runner.product('board', True, 'prod')
        execute.assert_not_called()

    def test_product_cli_profile_is_separate_from_rust_profile(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(runner, 'OUT', Path(temp) / 'out'), \
                patch.object(sys, 'argv', ['run.py', 'product', '--target', 'board', '--product-profile', 'dev']), \
                patch.object(runner, 'product') as execute:
            runner.main()
            execute.assert_called_once_with('board', False, 'dev')

    def test_mixed_job_runs_remaining_tasks_after_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'snapshot').mkdir()
            (root / 'snapshot/shards.json').write_text(json.dumps({'schema': 2, 'include': [
                {'id': 'mixed-0', 'name': 'Mixed', 'tasks': [{'layer': 'runtime', 'shard': 0},
                                                          {'layer': 'compile', 'shard': 0}]}]}))
            for failure in (subprocess.CalledProcessError(1, 'twister'), ValueError('missing report')):
                with patch.object(runner, 'WORKSPACE', root), \
                        patch.object(runner, 'twister', side_effect=[failure, None]) as execute:
                    with self.assertRaisesRegex(ValueError, 'tasks failed'):
                        runner.twister_job('mixed-0')
                    self.assertEqual(execute.call_count, 2)
                    self.assertEqual(execute.call_args.args, ('compile', 0))
            with patch.object(runner, 'WORKSPACE', root), patch.object(runner, 'twister') as execute:
                runner.twister_job('mixed-0')
                self.assertEqual(execute.call_count, 2)
                with self.assertRaisesRegex(ValueError, 'unknown'):
                    runner.twister_job('missing')

    def test_failed_twister_still_writes_elapsed_task_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'snapshot').mkdir()
            (root / 'snapshot/plan.json').write_text(json.dumps({'test_roots': ['tests'], 'compile_roots': ['samples']}))
            def execute(*args, **kwargs):
                if args[0] == 'west':
                    raise subprocess.CalledProcessError(1, 'twister')
            with patch.object(runner, 'WORKSPACE', root), patch.object(runner, 'OUT', root / 'out'), \
                    patch.object(runner, 'run', side_effect=execute):
                with self.assertRaises(subprocess.CalledProcessError):
                    runner.twister('compile', 0)
            record = json.loads((root / 'out/compile-0/task-timing.json').read_text())
            self.assertFalse(record['passed'])
            self.assertEqual(record['layer'], 'compile')
            self.assertGreaterEqual(record['seconds'], 0)


if __name__ == '__main__':
    unittest.main()
