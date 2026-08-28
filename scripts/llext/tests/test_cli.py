# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

from pathlib import Path
import sys
import unittest
from unittest.mock import patch


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from meshbus_cli.cli import build_parser, main  # noqa: E402


class MeshbusCliTests(unittest.TestCase):
    def test_llext_is_a_required_meshbus_subcommand(self):
        parser = build_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args([])

    def test_llext_dispatches_to_shared_builder(self):
        with patch("meshbus_cli.cli._run_build", return_value=0) as run:
            result = main(
                [
                    "llext",
                    "--llext-sdk",
                    "/tmp/edk",
                    "--output-dir",
                    "/tmp/output",
                    "/tmp/package",
                ]
            )

        self.assertEqual(result, 0)
        run.assert_called_once()
