# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
from contextlib import chdir, redirect_stdout
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import docs
import checks
import subprocess
import metadata
import impact
import plan
import run as ci_run
import test_plan
import yaml


def suite(name, platform='native_sim/native', status='None'):
    return dict(name=name, platform=platform, toolchain='host/gnu', status=status, execution_time=1)


class Planning(unittest.TestCase):
    def test_component_selection_does_not_expand_test_consumers_recursively(self):
        selected = plan.select(['subsys/clock/clock.c'])
        self.assertIn('tests/subsys/clock', selected['test_roots'])
        self.assertIn('tests/subsys/mgmt/config_handlers', selected['test_roots'])
        self.assertNotIn('tests/subsys/settings/performance', selected['test_roots'])
        self.assertNotIn('tests/subsys/fs', selected['test_roots'])
        self.assertEqual(plan.select(['include/clock/clock.h'])['test_roots'],
                         selected['test_roots'])

    def test_metadata_and_ci_python_have_no_product_build_dependency(self):
        for path in ('REUSE.toml', 'LICENSING.md', 'LICENSES/MIT.txt',
                     '.github/license-policy.toml', 'scripts/ci/docs.py'):
            with self.subTest(path=path):
                selected = plan.select([path])
                self.assertFalse(selected['full'])
                self.assertFalse(selected['sdk'])
                self.assertFalse(selected['products'])
                self.assertFalse(selected['cli'])
                self.assertEqual(selected['host'], path.endswith('.py'))

    def test_cli_license_inputs_select_cli_without_firmware(self):
        for path in ('LICENSE', 'LICENSES/Apache-2.0.txt'):
            with self.subTest(path=path):
                selected = plan.select([path])
                for flag in ('host', 'rust', 'cli'):
                    self.assertTrue(selected[flag], flag)
                for flag in ('full', 'sdk', 'products'):
                    self.assertFalse(selected[flag], flag)

    def test_power_selects_direct_indicator_feedback_consumers(self):
        expected = ['tests/subsys/indicator/audio', 'tests/subsys/indicator/feedback']
        for path in ('subsys/power/power.c', 'include/power/power.h'):
            with self.subTest(path=path):
                selected = plan.select([path])
                for layer in ('test_roots', 'compile_roots'):
                    self.assertEqual([root for root in selected[layer]
                                      if root.startswith('tests/subsys/indicator')], expected)
                    self.assertNotIn('tests', selected[layer])
                self.assertEqual(selected['sdk_selection']['components'], ['power'])

    def test_extended_scenarios_are_selected_by_relevance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / 'tests/owner'
            folder.mkdir(parents=True)
            (folder / 'testcase.yaml').write_text(
                'tests:\n  normal: {}\n  shuffled:\n    tags: [shuffle]\n'
                '  benchmark:\n    tags: [performance]\n')
            report = {'testsuites': [suite(n) for n in ('normal', 'shuffled', 'benchmark')]}
            routine = {'full': False, 'extended_roots': []}
            self.assertEqual([s['name'] for s in test_plan.filter_extended(report, routine, root)['testsuites']],
                             ['normal'])
            for selection in ({'full': True}, routine | {'extended_roots': ['tests/owner']}):
                self.assertEqual(test_plan.filter_extended(report, selection, root), report)
        self.assertEqual(plan.select(['tests/subsys/desktop/integration/src/test_input.c'])['extended_roots'],
                         ['tests/subsys/desktop/integration'])
        self.assertIn('tests/subsys/settings/performance',
                      plan.select(['subsys/settings/settings.c'])['extended_roots'])

    def test_openspec_changes_need_source_checks_without_firmware_builds(self):
        for path in ('openspec/config.yaml', 'openspec/changes/add-clock/.openspec.yaml',
                     'openspec/specs/development-workflow/spec.md',
                     '.agents/skills/.openspec-target',
                     '.agents/skills/openspec-propose/SKILL.md',
                     '.agents/skills/OPENSPEC-LICENSE', 'package.json', 'package-lock.json'):
            with self.subTest(path=path):
                selected = plan.select([path])
                for flag in ('full', 'host', 'sdk', 'products', 'cli', 'workspace', 'heavy'):
                    self.assertFalse(selected[flag], flag)
                self.assertEqual(checks.expected_jobs(selected), ['plan', 'lightweight'])
        # A specification must not hide the runtime scope of a mixed change.
        mixed = plan.select(['openspec/config.yaml', 'subsys/clock/clock.c'])
        self.assertTrue(mixed['sdk'])
        self.assertTrue(mixed['products'])
        self.assertFalse(mixed['cli'])

    def test_docs_need_no_image_workspace_or_builds(self):
        for path in ('README.md', '.github/CI.md', 'scripts/meshbus/README.md'):
            selected = plan.select([path])
            for flag in ('host', 'sdk', 'products', 'cli', 'workspace', 'heavy', 'audit'):
                self.assertFalse(selected[flag], (path, flag))
            self.assertEqual(checks.expected_jobs(selected), ['plan', 'lightweight'])

    def test_firmware_and_cli_are_separate_consumers(self):
        firmware = plan.select(['subsys/clock/clock.c'])
        self.assertTrue(firmware['sdk'])
        self.assertTrue(firmware['products'])
        self.assertFalse(firmware['cli'])
        self.assertFalse(firmware['host'])
        cli = plan.select(['scripts/meshbus/src/main.rs'])
        self.assertTrue(cli['host'])
        self.assertTrue(cli['rust'])
        self.assertFalse(cli['sdk'])
        self.assertFalse(cli['products'])
        self.assertEqual(len(cli['native_targets']), 3)
        self.assertEqual(len(plan.select(['README.md'], full=True)['native_targets']), 6)

    def test_tests_narrow_both_layers_and_deleted_suites_fall_back(self):
        selected = plan.select(['tests/subsys/clock/src/main.c', 'README.md'])
        self.assertEqual(selected['test_roots'], ['tests/subsys/clock'])
        self.assertEqual(selected['compile_roots'], ['tests/subsys/clock'])
        self.assertFalse(selected['products'])
        self.assertFalse(selected['cli'])
        deleted = plan.select(['tests/deleted-suite/testcase.yaml'])
        self.assertEqual(deleted['test_roots'], ['tests'])

    def test_test_selection_is_independent_of_working_directory(self):
        with tempfile.TemporaryDirectory() as temporary, chdir(temporary):
            selected = plan.select(['tests/subsys/clock/src/main.c'])
            self.assertEqual(selected['test_roots'], ['tests/subsys/clock'])
            self.assertEqual(selected['compile_roots'], ['tests/subsys/clock'])
            self.assertEqual(plan.select(['tests/deleted-suite/testcase.yaml'])['test_roots'], ['tests'])

    def test_component_selects_own_tests_and_consumers_without_cli(self):
        selected = plan.select(['subsys/clock/clock.c'])
        self.assertEqual(selected['sdk_selection']['components'], ['clock'])
        for target in ('tests/subsys/clock', 'tests/subsys/mgmt/config_handlers',
                       'tests/subsys/meshcore', 'tests/subsys/llext'):
            self.assertIn(target, selected['test_roots'])
        self.assertIn('samples/subsys/clock', selected['compile_roots'])
        self.assertNotIn('tests', selected['test_roots'])
        self.assertNotIn('tests/drivers/input/tca8418', selected['test_roots'])
        self.assertTrue(selected['products'])
        self.assertFalse(selected['cli'])

    def test_driver_source_and_binding_select_same_contracts(self):
        source = plan.select(['drivers/input/input_tca8418.c'])
        binding = plan.select(['dts/bindings/mfd/ti,tca8418.yaml'])
        self.assertEqual(source['test_roots'], binding['test_roots'])
        self.assertIn('tests/drivers/input/tca8418', binding['test_roots'])
        self.assertIn('samples/subsys/input', source['compile_roots'])
        self.assertTrue(source['products'])

    def test_shared_and_unmapped_firmware_expand_sdk_not_cli(self):
        for name in ('include/unknown/header.h', 'subsys/clock/time.c', 'subsys/settings/settings.c',
                     'subsys/new_service/service.c', 'subsys/clock/CMakeLists.txt',
                     'drivers/CMakeLists.txt', 'boards/fobe/new_board/board.yml',
                     'apps/meshbus/unknown.conf'):
            selected = plan.select([name])
            self.assertEqual(selected['test_roots'], ['tests'], name)
            self.assertEqual(selected['compile_roots'], ['samples', 'tests'], name)
            self.assertEqual(selected['sdk_selection']['fallback_paths'], [name])
            self.assertTrue(selected['products'])
            self.assertFalse(selected['cli'])

    def test_direct_samples_skip_runtime_and_products(self):
        for name in ('samples/subsys/clock/src/main.c', 'samples/subsys/clock/sample.yaml',
                     'samples/subsys/clock/CMakeLists.txt'):
            selected = plan.select([name, 'README.md'])
            self.assertEqual(selected['test_roots'], [])
            self.assertEqual(selected['compile_roots'], ['samples/subsys/clock'])
            self.assertTrue(selected['sdk'])
            self.assertFalse(selected['products'])
            self.assertFalse(selected['cli'])

    def test_mixed_domains_union_scopes_and_compact_nested_roots(self):
        paths = ['subsys/gnss/heading.c', 'subsys/clock/clock.c',
                 'samples/subsys/telemetry/src/main.c',
                 'tests/subsys/clock/service_dut/src/main.c',
                 'scripts/meshbus/src/main.rs']
        selected = plan.select(paths)
        wanted = set(plan.select(paths[:1])['compile_roots'])
        wanted.update(plan.select(paths[1:2])['compile_roots'])
        wanted.add('samples/subsys/telemetry')
        self.assertEqual(selected['compile_roots'], sorted(wanted))
        self.assertTrue(selected['cli'])
        self.assertEqual(selected['sdk_selection']['components'], ['clock', 'gnss'])

    def test_unowned_samples_and_shared_test_helpers_expand(self):
        selected = plan.select(['samples/deleted/sample.yaml'])
        self.assertEqual(selected['compile_roots'], ['samples', 'tests'])
        self.assertEqual(selected['test_roots'], ['tests'])
        self.assertTrue(selected['products'])
        selected = plan.select(['tests/common/fixture.h'])
        self.assertEqual(selected['test_roots'], ['tests'])
        self.assertFalse(selected['products'])

    def test_invalid_policy_root_fails_instead_of_silently_omitting_tests(self):
        policy = {'shared': [], 'components': {
            'bad': {'paths': ['subsys/bad/*'], 'roots': ['tests/missing']}}}
        with patch.object(impact.tomllib, 'loads', return_value=policy):
            with self.assertRaisesRegex(ValueError, 'no test metadata'):
                plan.select(['subsys/bad/bad.c'])

    def test_direct_integration_owners_do_not_become_changed_components(self):
        selected = plan.select(['subsys/dfu/img_util/flash_img.c'])
        self.assertEqual(selected['test_roots'], ['tests/subsys/dfu', 'tests/subsys/firmware'])
        mixed = plan.select(['subsys/dfu/img_util/flash_img.c', 'subsys/fs/fs.c'])
        self.assertIn('tests/subsys/llext', mixed['test_roots'])
        self.assertEqual(mixed['sdk_selection']['components'], ['dfu', 'fs'])

    def test_shared_unknown_and_missing_diff_expand_to_full(self):
        for paths in ([], ['unknown.file'], ['west.yml'], ['.github/workflows/ci.yml']):
            selected = plan.select(paths)
            self.assertTrue(selected['full'], paths)
            self.assertTrue(selected['audit'], paths)
            self.assertEqual(len(selected['native_targets']), 6)

    def test_tools_and_release_selection(self):
        tools = plan.select(['scripts/remote/remote.py'])
        self.assertTrue(tools['host'])
        self.assertFalse(tools['workspace'])
        self.assertFalse(tools['rust'])
        release = plan.select(['scripts/release/licensing.py'])
        self.assertTrue(release['cli'])
        self.assertTrue(release['products'])
        self.assertFalse(release['sdk'])
        self.assertTrue(plan.select(['scripts/meshbus/Cargo.lock'])['audit'])
        self.assertFalse(plan.select(['scripts/meshbus/src/main.rs'])['audit'])

    def test_push_uses_exact_tree_and_pr_uses_merge_base(self):
        sha = 'a' * 40
        for kwargs, comparison in (({'before': sha}, sha + '..HEAD'),
                                   ({'base': sha}, sha + '...HEAD')):
            with patch.object(plan.subprocess, 'check_output', side_effect=[sha.encode(), b'README.md\0']) as call:
                self.assertEqual(plan.changed_paths(**kwargs), ['README.md'])
                self.assertEqual(call.call_args.args[0][-1], comparison)
                self.assertIn('--no-renames', call.call_args.args[0])
        for value in (None, '0' * 40):
            self.assertIsNone(plan.changed_paths(before=value))
        with patch.object(plan.subprocess, 'check_output', side_effect=subprocess.CalledProcessError(1, 'git')):
            self.assertIsNone(plan.changed_paths(before=sha))

    def test_required_gate_enforces_selected_jobs_and_all_failures(self):
        selected = plan.select(['README.md'])
        expected = checks.expected_jobs(selected)
        needs = {key: {'result': 'success'} for key in expected}
        needs['prepare'] = {'result': 'skipped'}
        checks.aggregate(needs, expected)
        for result in ('failure', 'cancelled', 'skipped'):
            with self.assertRaises(ValueError):
                checks.aggregate(needs | {'plan': {'result': result}}, expected)
        selected = plan.select(['subsys/clock/clock.c'])
        expected = checks.expected_jobs(selected)
        needs = {key: {'result': 'success'} for key in expected}
        for key in expected:
            with self.assertRaises(ValueError):
                checks.aggregate(needs | {key: {'result': 'skipped'}}, expected)
        with self.assertRaises(ValueError):
            checks.aggregate(needs | {'unselected': {'result': 'failure'}}, expected)

    def test_small_selections_do_not_create_one_job_per_instance(self):
        for size, workers in ((1, 1), (4, 1), (12, 1), (13, 2), (60, 4)):
            report = {'testsuites': [suite(str(i)) for i in range(size)]}
            self.assertEqual(len(test_plan.partition(report)), workers)

    def test_ci_helpers_select_their_actual_consumers(self):
        for path in ('.github/actions/cargo-cache/action.yml', 'scripts/ci/cargo-xwin.sh',
                     'scripts/ci/cli_workspace.py'):
            selected = plan.select([path])
            self.assertTrue(selected['cli'], path)
            self.assertFalse(selected['sdk'], path)
        for path in ('scripts/ci/plan.py', 'scripts/ci/impact.py', 'scripts/ci/test_plan.py',
                     'scripts/ci/workspace.py', 'scripts/ci/run.py'):
            self.assertTrue(plan.select([path])['full'], path)
        for path in ('.github/workflows/alpha-release.yml', '.github/workflows/candidates.yml'):
            selected = plan.select([path])
            self.assertTrue(selected['cli'])
            self.assertTrue(selected['products'])
            self.assertFalse(selected['sdk'])

    def test_macos_cross_build_keeps_native_intel_execution(self):
        selected = plan.select(['README.md'], full=True)
        target = 'x86_64-apple-darwin'
        self.assertIn({'target': target, 'runner': 'macos-15'}, selected['mac_targets'])
        self.assertIn({'target': target, 'runner': 'macos-15-intel'}, selected['native_targets'])
        self.assertEqual(selected['cli_profile'], 'release')
        self.assertEqual(selected['compile_roots'], ['tests', 'samples'])
        self.assertEqual(plan.select(['scripts/meshbus/src/main.rs'])['cli_profile'], 'ci')

    def test_actions_profile_output_reaches_cli_argument_parser(self):
        for full, profile in ((False, 'ci'), (True, 'release')):
            with self.subTest(full=full), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                output = root / 'actions-output'
                argv = ['plan.py', '--output', str(root / 'plan.json')]
                if full:
                    argv.append('--full')
                with patch.object(sys, 'argv', argv), \
                        patch.object(plan, 'changed_paths', return_value=['scripts/meshbus/src/main.rs']), \
                        patch.dict(os.environ, GITHUB_OUTPUT=str(output), GITHUB_STEP_SUMMARY=''), \
                        redirect_stdout(io.StringIO()):
                    plan.main()
                outputs = dict(line.split('=', 1) for line in output.read_text().splitlines())
                self.assertEqual(outputs['cli'], 'true')
                self.assertEqual(json.loads(outputs['linux_targets'])[0], 'x86_64-unknown-linux-gnu')
                # Actions passes scalar outputs unchanged through PROFILE to --profile.
                argv = ['run.py', 'cli', '--target', 'x86_64-unknown-linux-gnu',
                        '--profile', outputs['cli_profile']]
                with patch.object(sys, 'argv', argv), patch.object(ci_run, 'OUT', root / 'out'), \
                        patch.object(ci_run, 'cli') as build:
                    ci_run.main()
                build.assert_called_once_with('x86_64-unknown-linux-gnu', False, profile)

    def test_shards_are_disjoint_nonempty_and_complete(self):
        report = {'testsuites': [suite(str(i)) for i in range(9)]}
        shards = test_plan.partition(report, 4)
        self.assertEqual([len(s['testsuites']) for s in shards], [3, 2, 2, 2])
        self.assertEqual(len({test_plan.key(s) for part in shards for s in part['testsuites']}), 9)
        self.assertEqual(len(test_plan.partition({'testsuites': [suite('only')]})), 1)
        self.assertEqual(test_plan.partition({'testsuites': []}), [])
        with self.assertRaises(ValueError):
            test_plan.partition({'testsuites': [suite('same'), suite('same')]})
        # Architecture-specific builds must not disappear with native coverage.
        self.assertNotEqual(test_plan.key(suite('same')), test_plan.key(suite('same', 'qemu_x86/atom')))

    def test_missing_duplicate_or_zero_execution_reports_fail(self):
        expected = {'testsuites': [suite('one'), suite('two')]}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'twister.json'
            path.write_text(json.dumps({'testsuites': [suite('one', status='passed')]}))
            for reports in ([], [path], [path, path]):
                with self.assertRaises(ValueError):
                    test_plan.verify(expected, reports, True)
            path.write_text(json.dumps({'testsuites': [suite('one', status='passed'), suite('two', status='passed')]}))
            test_plan.verify(expected, [path], True)

    def test_single_layer_plans_allow_empty_other_layer_but_not_empty_selection(self):
        for runtime, compile_suites, expected_layers in (
                ([suite('runtime')], [suite('runtime')], ['runtime']),
                ([], [suite('compile')], ['compile']),
                ([], [], [])):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                snapshot = root / 'snapshot'
                snapshot.mkdir()
                selection = plan.select(['tests/subsys/clock/src/main.c'])
                (snapshot / 'plan.json').write_text(json.dumps(selection))
                def twister(command, **kwargs):
                    destination = Path(command[command.index('--save-tests') + 1])
                    suites = runtime if destination.stem == 'runtime' else compile_suites
                    destination.write_text(json.dumps({'testsuites': suites}))
                    self.assertEqual(command[command.index('-T') + 1], str(root / 'meshbus/tests/subsys/clock'))
                with patch.object(test_plan.subprocess, 'run', side_effect=twister):
                    if not expected_layers:
                        with self.assertRaisesRegex(ValueError, 'no runnable or buildable'):
                            test_plan.generate(root, snapshot)
                    else:
                        test_plan.generate(root, snapshot)
                        matrix = json.loads((snapshot / 'shards.json').read_text())['include']
                        self.assertEqual([part['layer'] for part in matrix], expected_layers)
        test_plan.verify({'testsuites': []}, [], False)

    def test_board_compile_scope_does_not_filter_component_platforms(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            snapshot = root / 'snapshot'
            snapshot.mkdir()
            selection = plan.select(['tests/subsys/clock/src/main.c'])
            selection.update(board_compile_roots=['samples/board'],
                             board_platforms=['board/soc'],
                             compile_roots=['tests/subsys/clock', 'samples/board'])
            (snapshot / 'plan.json').write_text(json.dumps(selection))
            def twister(command, **kwargs):
                target = Path(command[command.index('--save-tests') + 1])
                reports = {'runtime': [suite('clock')],
                           'compile': [suite('clock'), suite('clock', 'other/soc')],
                           'board-compile': [suite('board_sample', 'board/soc'),
                                             suite('unselected', 'other/soc')]}
                target.write_text(json.dumps({'testsuites': reports[target.stem]}))
                if target.stem == 'board-compile':
                    self.assertIn('-p', command)
                else:
                    self.assertNotIn('-p', command)
            with patch.object(test_plan.subprocess, 'run', side_effect=twister):
                test_plan.generate(root, snapshot)
            report = json.loads((snapshot / 'compile.json').read_text())
            self.assertEqual({(s['name'], s['platform']) for s in report['testsuites']},
                             {('clock', 'other/soc'), ('board_sample', 'board/soc')})

    def test_downloaded_reports_accept_single_and_multiple_artifact_layouts(self):
        for nested in (False, True):
            for layers in (('runtime',), ('compile',), ('runtime', 'compile')):
                with self.subTest(nested=nested, layers=layers), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    snapshot, reports = root / 'snapshot', root / 'reports'
                    snapshot.mkdir()
                    for layer in ('runtime', 'compile'):
                        suites = [suite(layer)] if layer in layers else []
                        (snapshot / f'{layer}.json').write_text(json.dumps({'testsuites': suites}))
                        if suites:
                            folder = reports / f'zephyr-{layer}-0' if nested else reports
                            folder = folder / f'{layer}-0'
                            folder.mkdir(parents=True)
                            (folder / 'twister.json').write_text(json.dumps({
                                'testsuites': [suite(layer, status='passed')]}))
                    result = subprocess.run([
                        sys.executable, str(Path(test_plan.__file__)), 'verify',
                        '--snapshot', str(snapshot), '--reports', str(reports)],
                        text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_selected_instance_cannot_disappear_as_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'twister.json'
            expected = {'testsuites': [suite('one'), suite('two')]}
            report.write_text(json.dumps({'testsuites': [suite('one', status='passed'), suite('two', status='skipped')]}))
            with self.assertRaises(ValueError):
                test_plan.verify(expected, [report], True)

    def test_sample_only_generation_never_invokes_default_runtime_discovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot = root / 'snapshot'
            snapshot.mkdir()
            selected = plan.select(['samples/subsys/clock/src/main.c'])
            (snapshot / 'plan.json').write_text(json.dumps(selected))
            def twister(command, **kwargs):
                self.assertIn('--build-only', command)
                self.assertEqual(command[command.index('-T') + 1],
                                 str(root / 'meshbus/samples/subsys/clock'))
                Path(command[command.index('--save-tests') + 1]).write_text(
                    json.dumps({'testsuites': [suite('sample')]}))
            with patch.object(test_plan.subprocess, 'run', side_effect=twister) as execute:
                test_plan.generate(root, snapshot)
                self.assertEqual(execute.call_count, 1)
            self.assertEqual(json.loads((snapshot / 'runtime.json').read_text()), {'testsuites': []})
            matrix = json.loads((snapshot / 'shards.json').read_text())['include']
            self.assertEqual(matrix, [{'layer': 'compile', 'shard': 0, 'name': 'Twister Build (1)'}])

    def test_light_installer_only_downloads_existing_source_tools(self):
        import importlib.util
        installer = Path(__file__).resolve().parents[3] / '.github/docker/install-tools.py'
        spec = importlib.util.spec_from_file_location('ci_install_tools', installer)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(sys, 'argv', ['install-tools.py', '--light', '--prefix', temporary]), \
                    patch.object(module, 'download') as download, patch.object(module.subprocess, 'run') as execute:
                module.main()
                self.assertEqual(download.call_count, 2)
                self.assertIn('actionlint', download.call_args_list[0].args[0])
                self.assertIn('gitleaks', download.call_args_list[1].args[0])
                self.assertTrue(all(call.args[0][:2] == ['tar', 'xf'] for call in execute.call_args_list))
                self.assertEqual({p.name for p in Path(temporary).iterdir()}, {'actionlint', 'gitleaks'})

    def test_yaml_duplicate_scenario_is_rejected(self):
        with self.assertRaises(ValueError):
            yaml.load('tests:\n  same: {}\n  same: {}\n', Loader=metadata.UniqueLoader)

    def test_docs_resolve_encoded_paths_and_ignore_urls_and_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'two words.md').touch()
            path = root / 'guide.md'
            path.write_text('[good](two%20words.md) [web](https://example.com)\n'
                            '```md\n[example](missing)\n```\n[broken](absent.md)')
            self.assertEqual(docs.broken_links(path, root), ['guide.md: absent.md'])


if __name__ == '__main__':
    unittest.main()
