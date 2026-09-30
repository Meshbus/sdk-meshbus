# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Board changes select actual board/profile consumers, including shared includes."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import product_impact
import plan


class ProductImpact(unittest.TestCase):
    def test_product_version_does_not_select_unrelated_sdk_tests(self):
        result = plan.select(['apps/meshbus/VERSION'])
        self.assertTrue(result['products'])
        self.assertFalse(result['sdk'])
        self.assertFalse(result['cli'])

    def test_product_profile_selects_its_product(self):
        result = plan.select(['apps/meshbus/boards/fobe/mesh_probe_r2/mesh_probe_r2_nrf54l15_cpuapp.conf'])
        self.assertEqual(result['product_names'], ['mesh_probe_r2'])
        self.assertFalse(result['sdk'])

    def test_board_dts_selects_board_builds_and_product(self):
        result = plan.select(['boards/fobe/mesh_probe_r2/mesh_probe_r2_common.dtsi'])
        self.assertEqual(result['product_names'], ['mesh_probe_r2'])
        self.assertIn('mesh_probe_r2/nrf54l15/cpuapp', result['board_platforms'])
        self.assertEqual(result['test_roots'], [])
        self.assertTrue(result['board_compile_roots'])

    def test_mixed_service_preserves_unrestricted_test_scope(self):
        result = plan.select(['boards/fobe/mesh_probe_r2/mesh_probe_r2_common.dtsi',
                              'subsys/gnss/gnss.c'])
        self.assertIsNone(result['product_names'])
        self.assertIn('tests/subsys/gnss', result['test_roots'])
        self.assertIn('tests/subsys/gnss', result['unrestricted_compile_roots'])

    def test_unknown_or_deleted_board_owner_retains_full_sdk(self):
        result = plan.select(['boards/fobe/removed/removed.dtsi'])
        self.assertEqual(result['test_roots'], ['tests'])
        self.assertIsNone(result['product_names'])

    def test_product_matrix_filter_never_silently_loses_an_owner(self):
        matrix = {'include': [{'id': 'one', 'board': 'one/soc'}, {'id': 'two', 'board': 'two/soc'}]}
        self.assertEqual(product_impact.filter_products(matrix, ['one']), {'include': [matrix['include'][0]]})
        self.assertEqual(product_impact.filter_products(matrix, None), matrix)
        with self.assertRaisesRegex(ValueError, 'missing'):
            product_impact.filter_products(matrix, ['removed'])

    def test_shared_and_deleted_include_find_all_consumers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('one', 'two'):
                board = root / 'boards/vendor' / name
                board.mkdir(parents=True)
                (board / 'board.yml').write_text('board:\n  name: ' + name + '\n')
                (board / f'{name}.dts').write_text('#include "../../common/shared.dtsi"\n')
                profile = root / 'apps/meshbus/boards/vendor' / name
                profile.mkdir(parents=True)
                (profile / f'{name}_soc.conf').write_text('CONFIG_TEST=y\n')
            # The include may have been deleted; its consumers still reference it.
            selected = product_impact.resolve('boards/common/shared.dtsi', root)
            self.assertEqual(selected['products'], ['one', 'two'])
            self.assertEqual(selected['boards'], ['one', 'two'])


if __name__ == '__main__':
    unittest.main()
