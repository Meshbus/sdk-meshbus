# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import license_policy


class LicensePolicy(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.config = self.root / 'prj.conf'
        self.config.write_text('CONFIG_TEST=y\n')
        self.policy = {'version': 1, 'exemptions': [{
            'category': 'configuration', 'reason': 'reviewed option assignments',
            'files': {'prj.conf': hashlib.sha256(self.config.read_bytes()).hexdigest()},
        }]}

    def report(self, paths=()):
        issues = {field: [] for field in license_policy.REQUIRED_FIELDS}
        for field in license_policy.METADATA:
            issues[field] = list(paths)
        return {'lint_version': '1.0', 'non_compliant': issues,
                'summary': {'compliant': not paths}}

    def test_only_reviewed_configuration_is_exempted(self):
        raw = self.report(['prj.conf', 'new.conf', 'main.c', 'CMakeLists.txt', 'image.png'])
        original = copy.deepcopy(raw)
        result = license_policy.evaluate(raw, self.root, self.policy)
        self.assertEqual(result['remaining_files'], ['CMakeLists.txt', 'image.png', 'main.c', 'new.conf'])
        self.assertEqual(list(result['exempted_files']), ['prj.conf'])
        self.assertFalse(result['policy_compliant'])
        self.assertEqual(raw, original)

    def test_all_exemptions_can_pass_policy_without_claiming_full_reuse(self):
        result = license_policy.evaluate(self.report(['prj.conf']), self.root, self.policy)
        self.assertTrue(result['policy_compliant'])
        self.assertFalse(result['raw_summary']['compliant'])
        license_policy.write_reports(result, self.root / 'reports')
        saved = json.loads((self.root / 'reports/license-policy.json').read_text())
        self.assertEqual(saved, result)
        self.assertEqual((self.root / 'reports/license-exempted-files.txt').read_text(), 'prj.conf\n')
        self.assertEqual((self.root / 'reports/license-remaining-licensing.txt').read_text(), '')

    def test_changed_config_must_be_reviewed_again(self):
        self.config.write_text('CONFIG_NEW=y\n')
        result = license_policy.evaluate(self.report(['prj.conf']), self.root, self.policy)
        self.assertFalse(result['policy_compliant'])
        self.assertEqual(result['remaining_files'], ['prj.conf'])
        self.assertIn('prj.conf', result['inactive_exemptions'])

    def test_board_images_are_exempt_but_runtime_assets_and_text_are_not(self):
        paths = ['boards/vendor/board/doc/photo.webp',
                 'boards/vendor/board/doc/img/photo.PNG',
                 'boards/vendor/board/doc/index.rst',
                 'boards/vendor/board/doc/diagram.svg',
                 'boards/vendor/board/assets/photo.webp',
                 'subsys/desktop/assets/photo.webp', 'third_party/photo.png']
        for name in paths:
            source = self.root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(b'image or text fixture')
        raw = self.report(paths)
        original = copy.deepcopy(raw)
        result = license_policy.evaluate(raw, self.root, self.policy)
        self.assertEqual(result['remaining_files'], sorted(paths[2:]))
        self.assertEqual(set(result['exempted_files']), set(paths[:2]))
        self.assertEqual(raw, original)
        raw = self.report(paths[:2])
        self.assertTrue(license_policy.evaluate(raw, self.root, self.policy)['policy_compliant'])
        raw['non_compliant']['bad_licenses'] = ['Invalid-License']
        self.assertFalse(license_policy.evaluate(raw, self.root, self.policy)['policy_compliant'])
        self.assertEqual(license_policy.evaluate(raw, self.root, self.policy)
                         ['non_compliant']['bad_licenses'], ['Invalid-License'])

    def test_new_and_updated_plain_versions_need_no_hash_entries(self):
        paths = ['apps/example/VERSION', 'samples/example/VERSION', 'tests/subsys/llext/VERSION',
                 'subsys/llext/METADATA_VERSION']
        for value in ('1', '42'):
            for name in paths:
                source = self.root / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text(value + '\n' if source.name != 'VERSION' else
                                  f'# Version data\nVERSION_MAJOR = {value}\nVERSION_MINOR = 0\n'
                                  'PATCHLEVEL = 0\nVERSION_TWEAK = 0\nEXTRAVERSION = rc1\n')
            result = license_policy.evaluate(self.report(paths), self.root, self.policy)
            self.assertTrue(result['policy_compliant'])
            self.assertEqual(set(result['exempted_files']), set(paths))
            self.assertFalse(result['raw_summary']['compliant'])

    def test_version_name_does_not_exempt_logic_or_malformed_data(self):
        name = 'tests/example/VERSION'
        source = self.root / name
        source.parent.mkdir(parents=True)
        valid = ('VERSION_MAJOR = 1\nVERSION_MINOR = 0\nPATCHLEVEL = 0\n'
                 'VERSION_TWEAK = 0\nEXTRAVERSION =\n')
        for content in (valid + 'print("hello")\n', valid + 'VERSION_MAJOR = 2\n',
                        valid.replace('PATCHLEVEL = 0', 'PATCHLEVEL = rc1'),
                        valid.replace('VERSION_MINOR = 0\n', ''), '', '\xff'):
            with self.subTest(content=content):
                source.write_bytes(content.encode('latin-1'))
                result = license_policy.evaluate(self.report([name]), self.root, self.policy)
                self.assertEqual(result['remaining_files'], [name])
        other = self.root / 'third_party/VERSION'
        other.parent.mkdir()
        other.write_text(valid)
        self.assertFalse(license_policy.evaluate(self.report(['third_party/VERSION']),
                                                self.root, self.policy)['policy_compliant'])

    def test_scoped_exemptions_do_not_follow_symlinks_or_escape_root(self):
        name = 'boards/vendor/board/doc/photo.png'
        source = self.root / name
        source.parent.mkdir(parents=True)
        source.symlink_to(self.config)
        for path in (name, '../tests/VERSION', '/tests/VERSION', './tests/VERSION'):
            with self.subTest(path=path):
                self.assertIsNone(license_policy.scoped_exemption(self.root, path))

    def test_deleted_file_does_not_keep_an_active_exemption(self):
        self.config.unlink()
        result = license_policy.evaluate(self.report(), self.root, self.policy)
        self.assertTrue(result['policy_compliant'])
        self.assertIn('prj.conf', result['inactive_exemptions'])

    def test_global_and_unknown_findings_cannot_be_exempted(self):
        for field in (license_policy.REQUIRED_FIELDS - set(license_policy.METADATA)) | {'future_issue'}:
            with self.subTest(field=field):
                report = self.report(['prj.conf'])
                report['non_compliant'][field] = ['failure']
                result = license_policy.evaluate(report, self.root, self.policy)
                self.assertFalse(result['policy_compliant'])
                self.assertEqual(result['non_compliant'][field], ['failure'])

    def test_distribution_text_exception_only_filters_named_unused_license(self):
        self.policy['distribution_licenses'] = {'BSL-1.0': 'External package fallback'}
        directory = self.root / 'LICENSES'
        directory.mkdir()
        (directory / 'BSL-1.0.txt').write_text('Boost license text')
        report = self.report(['prj.conf'])
        report['non_compliant']['unused_licenses'] = ['BSL-1.0']
        original = copy.deepcopy(report)
        result = license_policy.evaluate(report, self.root, self.policy)
        self.assertTrue(result['policy_compliant'])
        self.assertEqual(report, original)
        self.assertEqual(result['distribution_license_texts'], self.policy['distribution_licenses'])
        report['non_compliant']['unused_licenses'].append('MIT')
        self.assertFalse(license_policy.evaluate(report, self.root, self.policy)['policy_compliant'])
        report['non_compliant']['unused_licenses'] = ['BSL-1.0']
        report['non_compliant']['missing_licenses'] = ['BSL-1.0']
        self.assertFalse(license_policy.evaluate(report, self.root, self.policy)['policy_compliant'])
        (directory / 'BSL-1.0.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'missing distribution license'):
            license_policy.evaluate(report, self.root, self.policy)

    def test_missing_or_inconsistent_report_cannot_pass(self):
        report = self.report(['prj.conf'])
        del report['non_compliant']['missing_licenses']
        with self.assertRaises(ValueError):
            license_policy.evaluate(report, self.root, self.policy)
        report = self.report(['prj.conf'])
        report['non_compliant']['missing_licenses'] = None
        with self.assertRaises(ValueError):
            license_policy.evaluate(report, self.root, self.policy)
        report = self.report(['prj.conf'])
        report['summary']['compliant'] = True
        with self.assertRaises(ValueError):
            license_policy.evaluate(report, self.root, self.policy)

    def test_wildcards_escapes_duplicates_and_symlinks_are_rejected(self):
        for name in ('*.conf', '../prj.conf', '/prj.conf', './prj.conf'):
            with self.subTest(path=name):
                policy = copy.deepcopy(self.policy)
                policy['exemptions'][0]['files'] = {name: 'a' * 64}
                with self.assertRaises(ValueError):
                    license_policy.exemptions(self.root, policy)
        self.policy['exemptions'] *= 2
        with self.assertRaises(ValueError):
            license_policy.exemptions(self.root, self.policy)
        self.policy['exemptions'] = self.policy['exemptions'][:1]
        other = self.root / 'other.conf'
        self.config.rename(other)
        self.config.symlink_to(other)
        with self.assertRaises(ValueError):
            license_policy.exemptions(self.root, self.policy)

    def test_scanner_failure_or_invalid_output_cannot_pass(self):
        for status, data, error in [(2, '', subprocess.CalledProcessError),
                                    (1, '', json.JSONDecodeError),
                                    (1, json.dumps(self.report()), ValueError)]:
            with self.subTest(status=status, data=data):
                def scanner(args, **kwargs):
                    kwargs['stdout'].write(data)
                    return subprocess.CompletedProcess(args, status)
                with patch.object(license_policy.subprocess, 'run', side_effect=scanner):
                    with self.assertRaises(error):
                        license_policy.scan(self.root, self.root / 'reports')


if __name__ == '__main__':
    unittest.main()
