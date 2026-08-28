# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

import argparse
from pathlib import Path
import os
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import meshbus  # noqa: E402


class WestMeshbusRustAdapterTests(unittest.TestCase):
    def _parser(self):
        parser = argparse.ArgumentParser()
        commands = parser.add_subparsers(dest="west_command", required=True)
        meshbus.Meshbus().do_add_parser(commands)
        return parser

    def test_parser_forwards_firmware_arguments_to_rust(self):
        args, unknown = self._parser().parse_known_args(
            [
                "meshbus",
                "firmware",
                "inspect",
                "--device",
                "idea_mesh_tracker_c2",
                "--app",
                "app.hex",
            ]
        )

        self.assertEqual(args.meshbus_command, "firmware")
        self.assertEqual(
            unknown,
            [
                "--device",
                "idea_mesh_tracker_c2",
                "--app",
                "app.hex",
            ],
        )
        with patch.object(meshbus, "_rust_command", return_value=7) as run:
            result = args.meshbus_handler(args, unknown)

        self.assertEqual(result, 7)
        run.assert_called_once_with("firmware", ["inspect", *unknown])

    def test_parser_forwards_connect_console_arguments_to_rust(self):
        args, unknown = self._parser().parse_known_args(
            [
                "meshbus",
                "connect",
                "--port",
                "COM7",
                "--color",
                "never",
                "--no-history",
            ]
        )

        self.assertEqual(args.meshbus_command, "connect")
        self.assertEqual(
            unknown,
            ["--port", "COM7", "--color", "never", "--no-history"],
        )
        with patch.object(meshbus, "_rust_command", return_value=0) as run:
            result = args.meshbus_handler(args, unknown)

        self.assertEqual(result, 0)
        run.assert_called_once_with("connect", unknown)

    def test_adapter_propagates_explicit_rust_executable_failure(self):
        completed = SimpleNamespace(returncode=9)
        with (
            patch.dict(os.environ, {"MESHBUS_CLI": "/tmp/meshbus-rust"}),
            patch.object(Path, "is_file", return_value=True),
            patch.object(meshbus.subprocess, "run", return_value=completed) as run,
        ):
            with self.assertRaises(SystemExit) as raised:
                meshbus._rust_command("firmware", ["probes"])

        self.assertEqual(raised.exception.code, 9)
        run.assert_called_once_with(
            ["/tmp/meshbus-rust", "firmware", "probes"], check=False
        )

    def test_adapter_returns_success_for_explicit_rust_executable(self):
        completed = SimpleNamespace(returncode=0)
        with (
            patch.dict(os.environ, {"MESHBUS_CLI": "/tmp/meshbus-rust"}),
            patch.object(Path, "is_file", return_value=True),
            patch.object(meshbus.subprocess, "run", return_value=completed),
        ):
            result = meshbus._rust_command("firmware", ["probes"])

        self.assertEqual(result, 0)


if __name__ == "__main__":
    unittest.main()
