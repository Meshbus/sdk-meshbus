# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import serial_use  # noqa: E402


class SerialUseTest(unittest.TestCase):
    def test_wait_satisfied_supports_any_and_all(self):
        patterns = serial_use.compile_patterns(["ready", "done"])

        self.assertTrue(serial_use.wait_satisfied({0}, patterns, False))
        self.assertFalse(serial_use.wait_satisfied({0}, patterns, True))
        self.assertTrue(serial_use.wait_satisfied({0, 1}, patterns, True))

    def test_list_field_accepts_string_and_string_list(self):
        self.assertEqual(serial_use.list_field("status", field_name="command"), ["status"])
        self.assertEqual(
            serial_use.list_field(["status", "kernel stacks"], field_name="command"),
            ["status", "kernel stacks"],
        )
        with self.assertRaises(ValueError):
            serial_use.list_field(["status", 1], field_name="command")

    def test_default_bad_patterns_ignore_generic_retry_words(self):
        patterns = serial_use.compile_patterns(serial_use.DEFAULT_BAD_PATTERNS)

        self.assertFalse(any(pattern.search("network error, retrying") for pattern in patterns))
        self.assertFalse(any(pattern.search("first attempt failed") for pattern in patterns))
        self.assertTrue(any(pattern.search("<err> kernel: fatal fault") for pattern in patterns))

    def test_check_log_honors_explicit_business_failure_pattern(self):
        with tempfile.TemporaryDirectory() as directory:
            transcript = Path(directory) / "serial.log"
            transcript.write_text("business operation failed\n", encoding="utf-8")
            args = argparse.Namespace(
                file=str(transcript),
                no_default_bad_patterns=False,
                fail_pattern=[r"business operation failed"],
                ignore=None,
            )
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = serial_use.check_log_command(args)

        self.assertEqual(result, 2)
        self.assertIn("1: business operation failed", output.getvalue())

    def test_reset_command_is_executed_without_a_shell(self):
        args = argparse.Namespace(
            reset="none",
            reset_command="pyocd commander -t nrf54l -c reset -c exit",
            reset_pulse=0.0,
        )

        with mock.patch.object(serial_use.subprocess, "run") as run:
            serial_use.apply_reset(mock.Mock(), args)

        run.assert_called_once_with(
            ["pyocd", "commander", "-t", "nrf54l", "-c", "reset", "-c", "exit"],
            check=True,
        )

    def test_check_log_skips_metadata_and_flags_faults(self):
        with tempfile.TemporaryDirectory() as directory:
            transcript = Path(directory) / "serial.log"
            transcript.write_text(
                "# serial-use command: error\nBooting Zephyr\n<err> kernel: fatal fault\n",
                encoding="utf-8",
            )
            args = argparse.Namespace(
                file=str(transcript),
                no_default_bad_patterns=False,
                fail_pattern=None,
                ignore=None,
            )
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = serial_use.check_log_command(args)

        self.assertEqual(result, 2)
        self.assertIn("3: <err> kernel: fatal fault", output.getvalue())
        self.assertNotIn("serial-use command", output.getvalue())


if __name__ == "__main__":
    unittest.main()
