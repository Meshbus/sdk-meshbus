# SPDX-License-Identifier: Apache-2.0
"""Exercise local build selection and prevent stale CLI execution on failure."""

import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
# Standalone test discovery needs the scripts directory on sys.path first.
import meshbus_cli  # noqa: E402


class CliBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="meshbus cli tests ")
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name).resolve()
        self.enterContext(patch.object(
            meshbus_cli, "__file__", str(self.workspace / "meshbus/scripts/meshbus_cli.py")))
        self.enterContext(patch.dict(os.environ, {}, clear=True))
        self.which = self.enterContext(patch.object(meshbus_cli.shutil, "which", return_value=None))
        self.build = self.enterContext(patch.object(meshbus_cli.subprocess, "run"))
        self.binary = self.workspace / "build-meshbus-cli/release" / (
            "meshbus.exe" if os.name == "nt" else "meshbus")

    def write_binary(self):
        self.binary.parent.mkdir(parents=True, exist_ok=True)
        self.binary.write_text("fixture executable")
        self.binary.chmod(0o755)

    def test_missing_binary_is_built_and_installed_path_is_not_used(self):
        self.which.return_value = "/installed/old-meshbus"
        self.build.side_effect = lambda *args, **kwargs: self.write_binary()
        self.assertEqual(meshbus_cli.cli_command(auto_build=True), [str(self.binary)])
        args, kwargs = self.build.call_args
        command = args[0]
        self.assertEqual(command[:4], ["cargo", "build", "--locked", "--release"])
        self.assertEqual(command[command.index("--target-dir") + 1],
                         str(self.workspace / "build-meshbus-cli"))
        self.assertEqual(command[command.index("--manifest-path") + 1],
                         str(self.workspace / "meshbus/scripts/meshbus/Cargo.toml"))
        self.assertEqual(kwargs["cwd"], self.workspace)
        self.assertIs(kwargs["stdout"], sys.stderr)
        self.assertTrue(kwargs["check"])

    def test_existing_binary_still_gets_a_cargo_freshness_check(self):
        self.write_binary()
        for _ in range(2):
            self.assertEqual(meshbus_cli.cli_command(auto_build=True), [str(self.binary)])
        self.assertEqual(self.build.call_count, 2)

    def test_build_failure_rejects_existing_binary(self):
        self.write_binary()
        self.build.side_effect = subprocess.CalledProcessError(101, ["cargo"])
        with self.assertRaisesRegex(RuntimeError, "build failed.*101.*not started"):
            meshbus_cli.cli_command(auto_build=True)

    def test_missing_cargo_rejects_existing_binary_with_setup_hint(self):
        self.write_binary()
        self.build.side_effect = FileNotFoundError("cargo")
        with self.assertRaisesRegex(RuntimeError, "Cargo is required.*MESHBUS_CLI"):
            meshbus_cli.cli_command(auto_build=True)

    def test_success_without_expected_executable_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "Cargo completed.*missing"):
            meshbus_cli.cli_command(auto_build=True)

    def test_explicit_cli_skips_build(self):
        with patch.dict(os.environ, {"MESHBUS_CLI": "/chosen/meshbus"}):
            self.which.return_value = "/chosen/meshbus"
            self.assertEqual(meshbus_cli.cli_command(auto_build=True), ["/chosen/meshbus"])
        self.build.assert_not_called()

    def test_invalid_explicit_cli_does_not_fall_back_to_build(self):
        with patch.dict(os.environ, {"MESHBUS_CLI": "/missing/meshbus"}):
            with self.assertRaisesRegex(RuntimeError, "does not name an executable"):
                meshbus_cli.cli_command(auto_build=True)
        self.build.assert_not_called()

    def test_production_requirement_does_not_build_implicitly(self):
        with self.assertRaisesRegex(RuntimeError, "require MESHBUS_CLI"):
            meshbus_cli.cli_command(require_explicit=True, auto_build=True)
        self.build.assert_not_called()

    def test_relative_target_is_anchored_to_workspace(self):
        self.binary = self.workspace / "custom target/release" / self.binary.name
        self.build.side_effect = lambda *args, **kwargs: self.write_binary()
        with patch.dict(os.environ, {"CARGO_TARGET_DIR": "custom target", "CARGO": "custom cargo"}):
            self.assertEqual(meshbus_cli.cli_command(auto_build=True), [str(self.binary)])
        self.assertEqual(self.build.call_args.args[0][0], "custom cargo")

    def test_plain_resolver_retains_path_priority_without_building(self):
        self.which.return_value = "/installed/meshbus"
        self.assertEqual(meshbus_cli.cli_command(), ["/installed/meshbus"])
        self.build.assert_not_called()

    def test_plain_resolver_finds_new_default_without_building(self):
        self.write_binary()
        self.assertEqual(meshbus_cli.cli_command(), [str(self.binary)])
        self.build.assert_not_called()

    def test_bridge_never_executes_cli_after_build_failure(self):
        spec = importlib.util.spec_from_file_location("west_meshbus_adapter", SCRIPTS / "meshbus/meshbus.py")
        bridge = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bridge)
        command = bridge.Meshbus()
        with patch.object(bridge, "cli_command", side_effect=RuntimeError("build failed")) as resolve, \
                patch.object(command, "die", side_effect=SystemExit(1)):
            with self.assertRaises(SystemExit):
                command.do_run(argparse.Namespace(arguments=["--version"]), [])
        resolve.assert_called_once_with(auto_build=True)
        self.build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
