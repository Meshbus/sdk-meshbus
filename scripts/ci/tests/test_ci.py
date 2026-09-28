# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import checks
import artifact
import plan
import quality
import vulnerabilities
import workspace
import run as runner
import west_vulnerabilities


class Gates(unittest.TestCase):
    def quality_checkout(self, root):
        checkout = root / 'meshbus'
        checkout.mkdir()
        subprocess.run(['git', 'init', '-q', checkout], check=True)
        (checkout / 'initial.py').write_text('value = 1\n')
        self.commit_quality_fixture(checkout)
        return checkout

    def commit_quality_fixture(self, checkout):
        subprocess.run(['git', 'add', '.'], cwd=checkout, check=True)
        subprocess.run(['git', '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                        '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture'],
                       cwd=checkout, check=True)

    def test_root_commit_quality_checks_the_complete_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = self.quality_checkout(root)
            for baseline in ('', '0' * 40, 'f' * 40):
                with self.subTest(baseline=baseline), \
                        patch.object(quality, 'ROOT', checkout), \
                        patch.object(quality, 'OUT', root / 'reports'), \
                        patch.object(quality, 'run') as invoked, \
                        patch.dict(os.environ, {'DIFF_BASE': baseline}):
                    quality.quality('style', None)
                    invoked.assert_any_call('ruff', 'check', '--select', 'E4,E7,E9,F', 'initial.py')
                    quality.quality('vulnerabilities', None)
                    self.assertEqual(json.loads((root / 'reports/vulnerabilities-base.json').read_text()), {})
                    self.assertTrue(any(call.args[:2] == ('trivy', 'fs') for call in invoked.call_args_list))
                    self.assertFalse(any(call.args[:2] == ('git', 'worktree') for call in invoked.call_args_list))
                    quality.quality('west-vulnerabilities', None)
                    self.assertEqual(invoked.call_args.args[-2:], ('--base', '0' * 40))

    def test_quality_uses_available_parent_and_explicit_baseline(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = self.quality_checkout(root)
            parent = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=checkout, text=True).strip()
            (checkout / 'changed.py').write_text('value = 2\n')
            self.commit_quality_fixture(checkout)
            for baseline in ('', parent):
                with self.subTest(baseline=baseline), \
                        patch.object(quality, 'ROOT', checkout), \
                        patch.object(quality, 'OUT', root / 'reports'), \
                        patch.object(quality, 'run') as invoked, \
                        patch.dict(os.environ, {'DIFF_BASE': baseline}):
                    self.assertEqual(quality.comparison_base(None), parent)
                    quality.quality('style', None)
                    invoked.assert_any_call('ruff', 'check', '--select', 'E4,E7,E9,F', 'changed.py')
                    self.assertFalse(any('initial.py' in call.args for call in invoked.call_args_list))
                    quality.quality('vulnerabilities', None)
                    invoked.assert_any_call('git', 'worktree', 'add', '--detach',
                                            root / 'vulnerability-base', parent)
                    quality.quality('west-vulnerabilities', None)
                    self.assertEqual(invoked.call_args.args[-2:], ('--base', parent))

    def test_python_tool_checks_do_not_consume_release_workspace(self):
        with patch.object(runner, 'run') as invoked:
            runner.host(python_only=True)
            suites = [call.args[5] for call in invoked.call_args_list]
            self.assertEqual(suites, ['scripts/ci/tests', 'scripts/tests', 'scripts/remote/tests'])
            invoked.reset_mock()
            runner.host()
            self.assertTrue(any('scripts/release/tests' in call.args for call in invoked.call_args_list))
            self.assertTrue(any(call.args[0] == 'cargo' for call in invoked.call_args_list))

    def test_current_audit_does_not_compare_against_parent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(quality, 'OUT', root), patch.object(quality, 'run') as invoked, \
                    patch.object(quality, 'comparison_base') as comparison:
                quality.quality('vulnerabilities', 'a' * 40, current=True)
                comparison.assert_not_called()
                self.assertEqual(json.loads((root / 'vulnerabilities-base.json').read_text()), {})
                self.assertFalse(any(call.args[:2] == ('git', 'worktree') for call in invoked.call_args_list))
                self.assertFalse(any(call.args[:2] == ('trivy', 'rootfs') for call in invoked.call_args_list))
                quality.quality('west-vulnerabilities', 'a' * 40, current=True)
                self.assertEqual(invoked.call_args.args[-2:], ('--base', '0' * 40))

    def test_repository_license_check_does_not_need_font_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(quality, 'OUT', Path(temporary)), patch.object(quality, 'run') as invoked:
                quality.quality('licenses', None)
                self.assertEqual(invoked.call_count, 1)
                self.assertEqual(invoked.call_args.args[1], 'scripts/ci/license_policy.py')

    def test_emulation_cannot_qualify_wrong_binary_architecture(self):
        elf = bytearray(20)
        elf[:6] = b'\x7fELF\x02\x01'
        elf[18:20] = (62).to_bytes(2, 'little')
        pe = bytearray(134)
        pe[:2] = b'MZ'
        pe[60:64] = (128).to_bytes(4, 'little')
        pe[128:134] = b'PE\x00\x00' + (0x8664).to_bytes(2, 'little')
        macho = b'\xcf\xfa\xed\xfe' + (0x1000007).to_bytes(4, 'little')
        for data, suffix in ((elf, 'unknown-linux-gnu'), (pe, 'pc-windows-msvc'), (macho, 'apple-darwin')):
            artifact.verify_architecture(data, 'x86_64-' + suffix)
            with self.assertRaisesRegex(ValueError, 'architecture'):
                artifact.verify_architecture(data, 'aarch64-' + suffix)
        with self.assertRaises(ValueError):
            artifact.verify_architecture(b'MZ', 'x86_64-pc-windows-msvc')

    def test_initial_push_quality_runs_outside_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            checkout = Path(temporary) / 'meshbus'
            checkout.mkdir()
            subprocess.run(['git', 'init', '-q', checkout], check=True)
            previous = Path.cwd()
            try:
                os.chdir(temporary)
                with patch.object(quality, 'ROOT', checkout), \
                        patch.object(quality, 'OUT', Path(temporary) / 'reports'), \
                        patch.object(quality, 'run'), \
                        patch.dict(os.environ, {'DIFF_BASE': '0' * 40}):
                    quality.quality('licenses', None)
            finally:
                os.chdir(previous)

    def test_empty_or_skipped_required_jobs_fail(self):
        for needs in ({}, {'prepare': {'result': 'skipped'}},
                      {'prepare': {'result': 'success'}, 'test': {'result': 'skipped'}}):
            with self.assertRaises(ValueError):
                checks.aggregate(needs, ['prepare', 'test'])

    def test_unselected_skip_allowed_but_failure_never_hidden(self):
        checks.aggregate({'plan': {'result': 'success'}, 'unused': {'result': 'skipped'}}, ['plan'])
        for state in ('failure', 'cancelled'):
            with self.assertRaises(ValueError):
                checks.aggregate({'plan': {'result': 'success'}, 'unused': {'result': state}}, ['plan'])

    def test_twister_build_only_not_runtime_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'twister.json'
            report.write_text(json.dumps({'testsuites': [{'name': 'compile', 'status': 'not run'}]}))
            checks.report_twister(report)
            with self.assertRaises(ValueError):
                checks.report_twister(report, True)
            report.write_text(json.dumps({'testsuites': [{'status': 'passed', 'execution_time': '0.00'}]}))
            with self.assertRaises(ValueError):
                checks.report_twister(report, True)
            report.write_text(json.dumps({'testsuites': [{'status': 'passed', 'execution_time': '0.10'}]}))
            checks.report_twister(report, True)

    def test_empty_filtered_twister_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'twister.json'
            report.write_text(json.dumps({'testsuites': [{'status': 'filtered'}]}))
            with self.assertRaises(ValueError):
                checks.report_twister(report)

    def test_fast_native_runtime_requires_passed_harness_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'twister.json'
            suite = {'name': 'clock.timestamps_only', 'status': 'passed',
                     'runnable': True, 'execution_time': '0.00',
                     'testcases': [{'identifier': 'clock.units', 'status': 'passed'}]}
            report.write_text(json.dumps({'testsuites': [suite]}))
            checks.report_twister(report, True)
            suite['testcases'][0]['status'] = 'not run'
            report.write_text(json.dumps({'testsuites': [suite]}))
            with self.assertRaises(ValueError):
                checks.report_twister(report, True)

    def test_selection_shared_full_and_docs(self):
        self.assertFalse(plan.select(['README.md'])['sdk'])
        self.assertFalse(plan.select(['scripts/meshbus/src/main.rs'])['sdk'])
        self.assertTrue(plan.select(['include/meshbus.h'])['sdk'])
        self.assertEqual(len(plan.select(['README.md'], full=True)['native_targets']), 6)
        self.assertEqual(len(plan.select(['west.yml'])['native_targets']), 6)
        for path in ('LICENSING.md', 'LICENSES/Apache-2.0.txt',
                     'west.yml', 'docs/licensing/fonts.md'):
            with self.subTest(path=path):
                self.assertTrue(plan.select([path])['sdk'])
                self.assertTrue(plan.select([path])['cli'])

    def test_west_advisory_delta_records_new_high_matches(self):
        report = {'matches': [{'artifact': {'name': 'mbedtls', 'version': '4.1.1'},
                               'vulnerability': {'id': 'CVE-1', 'severity': 'High'}}]}
        self.assertTrue(west_vulnerabilities.compare(report, {})['introduced_high_or_critical'])
        self.assertFalse(west_vulnerabilities.compare(report, report)['introduced_high_or_critical'])

    def test_west_high_findings_are_reported_without_failing(self):
        report = {'matches': [{'artifact': {'name': 'fatfs', 'version': 'r0.16'},
                               'vulnerability': {'id': 'CVE-2026-6687', 'severity': 'High'}}]}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'reports'
            args = ['west_vulnerabilities.py', '--workspace', temporary,
                    '--output', str(output), '--base', '0' * 40]
            with patch.object(sys, 'argv', args), \
                    patch.object(west_vulnerabilities.subprocess, 'run'), \
                    patch.object(west_vulnerabilities, 'scan', return_value=report), \
                    patch.dict(os.environ):
                west_vulnerabilities.main()
            saved = json.loads((output / 'west-vulnerabilities-delta.json').read_text())
            self.assertEqual(saved['policy'], 'report-only')
            self.assertEqual(saved['introduced_high_or_critical'],
                             [['fatfs', 'r0.16', 'CVE-2026-6687']])

    def test_west_report_only_policy_does_not_hide_scanner_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'reports'
            args = ['west_vulnerabilities.py', '--workspace', temporary,
                    '--output', str(output), '--base', '0' * 40]
            with patch.object(sys, 'argv', args), \
                    patch.object(west_vulnerabilities.subprocess, 'run'), \
                    patch.object(west_vulnerabilities, 'scan',
                                 side_effect=subprocess.CalledProcessError(2, 'grype')), \
                    patch.dict(os.environ):
                with self.assertRaises(subprocess.CalledProcessError):
                    west_vulnerabilities.main()
            self.assertFalse((output / 'west-vulnerabilities-delta.json').exists())

    def test_ci_requires_every_revision_pinned(self):
        workspace.require_fixed([{'name': 'good', 'revision': 'a' * 40}])
        for revision in ('main', 'v1.0.0', 'abc123'):
            with self.assertRaises(ValueError):
                workspace.require_fixed([{'name': 'bad', 'revision': revision}])

    def test_workspace_path_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for relative in ('../outside', '/tmp/outside', '.', ''):
                with self.assertRaises(ValueError):
                    workspace.destination(root, relative)
            self.assertEqual(workspace.destination(root, 'modules/lib/example'), root / 'modules/lib/example')

    def test_vulnerability_delta_retains_preexisting_and_unknown(self):
        def report(severity='HIGH', version='1'):
            return {'Results': [{'Target': 'Cargo.lock', 'Vulnerabilities': [
                {'PkgName': 'crate', 'InstalledVersion': version, 'VulnerabilityID': 'CVE-1', 'Severity': severity}]}]}
        self.assertFalse(vulnerabilities.compare(report(), report())['introduced_high_or_critical'])
        self.assertTrue(vulnerabilities.compare(report(version='2'), report())['introduced_high_or_critical'])
        self.assertTrue(vulnerabilities.compare(report('UNKNOWN'), {})['severity_requires_review'])


if __name__ == '__main__':
    unittest.main()
