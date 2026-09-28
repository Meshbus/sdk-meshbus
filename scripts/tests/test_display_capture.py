# SPDX-License-Identifier: Apache-2.0
"""Device-free tests of snapshot integrity, rendering and CLI failure handling."""

import contextlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import display_capture as capture


def response(offset=0, **changes):
    result = dict(snapshot_id=7, total_size=520, width=65, height=64,
                  format="display_dump_format_ssd1306_page",
                  orientation="display_dump_orientation_horizontal", inverted=False,
                  offset=offset, data=bytes((i % 256 for i in range(offset, min(offset + 256, 520)))).hex())
    result.update(changes)
    return result


class CaptureTests(unittest.TestCase):
    def test_multiple_chunks_and_short_tail(self):
        calls = []

        def read(snapshot, offset):
            calls.append((snapshot, offset))
            return response(offset)

        meta, pixels = capture.capture(read)
        self.assertEqual(calls, [(0, 0), (7, 256), (7, 512)])
        self.assertEqual(meta["total_size"], 520)
        self.assertEqual(pixels, bytes(i % 256 for i in range(520)))

    def test_reject_corrupt_metadata_and_data(self):
        for changes in ({"data": ""}, {"data": "00"}, {"data": "00" * 257}, {"data": "AA=="},
                        {"data": "0"}, {"data": "00 00"}, {"snapshot_id": 0},
                        {"snapshot_id": True}, {"offset": 1}, {"total_size": 1},
                        {"width": 99999999}, {"height": 63}, {"inverted": 0},
                        {"orientation": "unknown"}, {"format": "unknown"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                capture.validate_chunk(response(**changes), 0, 0)

    def test_reject_missing_response(self):
        for value in (None, [], {}, {"data": "00"}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                capture.validate_chunk(value, 0, 0)

    def test_reject_snapshot_or_metadata_change(self):
        for changes in ({"snapshot_id": 8}, {"inverted": True}, {"width": 130, "height": 32}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                capture.capture(lambda snapshot, offset: response(offset, **(changes if offset else {})))

    def test_reject_overrun(self):
        with self.assertRaisesRegex(ValueError, "chunk length"):
            capture.validate_chunk(response(512, data="00" * 9), 7, 512)

    def test_png_polarity_scaling_and_no_second_rotation(self):
        for inverted in (False, True):
            for orientation in capture.ORIENTATIONS:
                for scale in (1, 4):
                    meta = dict(width=2, height=8, inverted=inverted, orientation=orientation)
                    png = capture.png_bytes(meta, b"\x01\x80", scale)
                    self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
                    pos, chunks = 8, {}
                    while pos < len(png):
                        size = struct.unpack(">I", png[pos:pos + 4])[0]
                        kind, data = png[pos + 4:pos + 8], png[pos + 8:pos + 8 + size]
                        crc = struct.unpack(">I", png[pos + 8 + size:pos + 12 + size])[0]
                        self.assertEqual(crc, zlib.crc32(kind + data))
                        chunks[kind] = data
                        pos += 12 + size
                    self.assertEqual(struct.unpack(">II", chunks[b"IHDR"][:8]), (2 * scale, 8 * scale))
                    expected = bytearray()
                    for y in range(8):
                        row = b"\0" + b"".join(bytes([255 if lit != inverted else 0]) * scale
                                                for lit in (y == 0, y == 7))
                        expected.extend(row * scale)
                    self.assertEqual(zlib.decompress(chunks[b"IDAT"]), expected)

    def test_uart_errors_and_timeout_are_not_success(self):
        uart = capture.UART(["meshbus"], "fixture", 115200, 1)
        for result in (subprocess.CompletedProcess([], 3, '{"ok":false,"error":{"code":"unsupported"}}'),
                       subprocess.CompletedProcess([], 0, 'not json'),
                       subprocess.CompletedProcess([], 0, '[]')):
            with self.subTest(result=result), patch.object(capture.subprocess, "run", return_value=result):
                with self.assertRaises(ValueError):
                    uart.read(0, 0)
        with patch.object(capture.subprocess, "run", side_effect=subprocess.TimeoutExpired([], 6)) as run:
            with self.assertRaises(subprocess.TimeoutExpired):
                uart.read(7, 256)
            self.assertEqual(run.call_args.kwargs["timeout"], 6)
        self.assertEqual(uart.exchanges, [])

    def test_existing_directory_prevents_cli_or_device_access(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(capture, "cli_command") as cli:
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(capture.main(["--port", "fixture", "--output-dir", directory]), 1)
            cli.assert_not_called()
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_failure_and_cancellation_leave_no_success_record(self):
        for error, status in ((ValueError("stale snapshot"), 1), (KeyboardInterrupt(), 130)):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "failed"
                with patch.object(capture, "cli_command", return_value=[sys.executable]), \
                     patch.object(capture.UART, "read", side_effect=error), \
                     contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(capture.main(["--port", "fixture", "--output-dir", str(output)]), status)
                self.assertEqual(json.loads((output / "failure.json").read_text())["status"], "failed")
                self.assertFalse((output / "result.json").exists())

    def test_end_to_end_with_cli_fixture_process(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "fake_cli.py"
            # Exercise argv, JSON, hex and file output across a real subprocess.
            fixture.write_text(
                "import json, sys\n"
                "assert sys.argv[1] == 'connect'\n"
                "assert sys.argv[sys.argv.index('-p') + 1] == 'fixture port'\n"
                "_, _, snapshot, offset, length = sys.argv[-1].split()\n"
                "offset = int(offset)\n"
                "assert int(snapshot) == (0 if offset == 0 else 7)\n"
                "assert length == '256'\n"
                f"result = {response()!r}\n"
                "result.update(offset=offset, data=bytes(i % 256 for i in range(offset, min(offset+256,520))).hex())\n"
                "print(json.dumps({'ok': True, 'result': result}))\n")
            output = Path(directory) / "capture"
            with patch.object(capture, "cli_command", return_value=[sys.executable, str(fixture)]), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(capture.main(["--port", "fixture port", "--output-dir", str(output)]), 0)
            report = json.loads((output / "result.json").read_text())
            self.assertEqual(report["status"], "captured")
            self.assertEqual(report["physical_observation"], "not assessed")
            self.assertEqual(len(report["exchanges"]), 3)
            self.assertEqual((output / "frame.bin").read_bytes(), bytes(i % 256 for i in range(520)))
            self.assertTrue((output / "frame.png").is_file())
            self.assertTrue((output / "preview.png").is_file())


if __name__ == "__main__":
    unittest.main()
