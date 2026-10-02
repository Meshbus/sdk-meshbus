# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Configuration selection preserves exact product pairs and conservative scope."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import checks
import plan
import product_impact


MATRIX = {'include': [{'id': name, 'board': name + '/soc'} for name in ('mesh_probe_r1', 'mesh_probe_r2')]}


class Selection(unittest.TestCase):
    def builds(self, paths, full=False):
        selected = plan.select(paths, full)
        return selected, {(row['id'], row['profile']) for row in
                          product_impact.build_matrix(MATRIX, selected['product_requests'])['include']}

    def test_profile_only_selects_actual_fragment_without_sdk_or_default(self):
        for profile in ('dev', 'prod'):
            selected, rows = self.builds([f'apps/meshbus/prj.{profile}.conf'])
            self.assertFalse(selected['sdk'])
            self.assertFalse(selected['cli'])
            self.assertEqual(rows, {(name, profile) for name in ('mesh_probe_r1', 'mesh_probe_r2')})
            self.assertIn('products', checks.expected_jobs(selected))

    def test_mixed_board_and_profile_does_not_cross_multiply(self):
        paths = ['apps/meshbus/prj.prod.conf',
                 'apps/meshbus/boards/fobe/mesh_probe_r2/mesh_probe_r2_nrf54l15_cpuapp.conf']
        selected, rows = self.builds(paths + paths)
        self.assertFalse(selected['sdk'])
        self.assertEqual(rows, {('mesh_probe_r2', 'default'), ('mesh_probe_r1', 'prod'), ('mesh_probe_r2', 'prod')})

    def test_full_preserves_default_and_explicit_profile(self):
        selected, rows = self.builds(['apps/meshbus/prj.dev.conf'], full=True)
        self.assertTrue(selected['sdk'])
        self.assertEqual(rows, {(name, profile) for name in ('mesh_probe_r1', 'mesh_probe_r2')
                               for profile in ('default', 'dev')})

    def test_known_application_inputs_stay_in_product_domain(self):
        for suffix in ('src/main.c', 'prj.conf', 'CMakeLists.txt', 'Kconfig', 'sysbuild.cmake',
                       'Kconfig.sysbuild', 'sysbuild/CMakeLists.txt', 'VERSION'):
            with self.subTest(suffix=suffix):
                selected, rows = self.builds(['apps/meshbus/' + suffix])
                self.assertFalse(selected['sdk'])
                self.assertEqual(rows, {(name, 'default') for name in ('mesh_probe_r1', 'mesh_probe_r2')})
        self.assertEqual(plan.select(['apps/meshbus/unknown.conf'])['test_roots'], ['tests'])

    def test_management_keeps_subsystem_consumers_and_mixed_driver(self):
        for path in ('subsys/mgmt/mgmt.c', 'subsys/shell/time.c'):
            selected = plan.select([path])
            self.assertEqual(selected['test_roots'], ['tests/subsys'])
            self.assertEqual(selected['compile_roots'], ['samples/subsys', 'tests/subsys'])
            self.assertTrue(selected['products'])
            mixed = plan.select([path, 'drivers/gnss/gnss_quectel_l76k.c'])
            self.assertTrue(any(root.startswith('tests/drivers/') for root in mixed['test_roots']))

    def test_only_reviewed_local_configuration_narrows(self):
        for component in ('clock', 'display', 'input', 'telemetry'):
            selected = plan.select([f'subsys/{component}/Kconfig'])
            self.assertEqual(selected['sdk_selection']['components'], [component])
            self.assertNotIn('tests', selected['test_roots'])
        for path in ('Kconfig', 'subsys/clock/CMakeLists.txt', 'subsys/desktop/Kconfig',
                     'subsys/mgmt/Kconfig', 'subsys/settings/settings.c', 'subsys/clock/time.c'):
            self.assertEqual(plan.select([path])['test_roots'], ['tests'], path)

    def test_tooling_scope_and_mixed_changes(self):
        for path in ('.gitignore', 'apps/meshbus/.gitignore', '.editorconfig', '.clang-format', 'README.md'):
            selected = plan.select([path])
            self.assertEqual(checks.expected_jobs(selected), ['plan', 'lightweight'])
            self.assertFalse(selected['openspec'])
        for path in ('openspec/config.yaml', 'package-lock.json', '.agents/skills/openspec-explore/SKILL.md'):
            self.assertTrue(plan.select([path])['openspec'])
        mixed = plan.select(['openspec/config.yaml', 'apps/meshbus/prj.prod.conf'])
        self.assertTrue(mixed['openspec'])
        self.assertTrue(mixed['product_builds'])
        self.assertTrue(plan.select(['unknown.file'])['openspec'])

    def test_reuse_keeps_inventory_but_requires_no_new_default_build(self):
        selected = plan.select([], full=True)
        selected['product_builds'] = False
        self.assertTrue(selected['products'])
        self.assertNotIn('products', checks.expected_jobs(selected))


if __name__ == '__main__':
    unittest.main()
