# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

import argparse
import io
import sys
import threading
import unittest
from unittest import mock
from pathlib import Path
from types import SimpleNamespace


REMOTE_DIR = Path(__file__).resolve().parents[1]
if str(REMOTE_DIR) not in sys.path:
    sys.path.insert(0, str(REMOTE_DIR))

from remote_device import DeviceCommand, _DeviceDashboard
from remote_gdb import _GdbMetrics
from terminal_format import _strip_ansi, _visible_len


class _RecordingAdapter:
    def __init__(self, failure: BaseException | None = None, ready=None):
        self.calls = []
        self.failure = failure
        self.ready = ready
        self.quiet = []

    def run(
        self,
        args,
        *,
        stop_event,
        manage_signals,
        quiet,
        ready_callback,
        telemetry_callback,
    ):
        self.calls.append((args, stop_event, manage_signals))
        self.quiet.append(quiet)
        if self.failure is not None:
            raise self.failure
        if self.ready is not None:
            ready_callback(self.ready)
        return 0


class _BlockingAdapter(_RecordingAdapter):
    def run(
        self,
        args,
        *,
        stop_event,
        manage_signals,
        quiet,
        ready_callback,
        telemetry_callback,
    ):
        self.calls.append((args, stop_event, manage_signals))
        self.quiet.append(quiet)
        if not stop_event.wait(timeout=2):
            raise AssertionError("peer failure did not request shutdown")
        return 0


class _Metrics:
    def __init__(self, values):
        self.values = values

    def read(self):
        return dict(self.values)


def _parse(command: DeviceCommand, *arguments: str):
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="subcommand", required=True)
    command.add_parser(subparsers)
    return parser.parse_args(("device", *arguments))


class DeviceCommandTest(unittest.TestCase):
    def test_combines_gdb_and_serial_with_one_shared_stop_event(self):
        gdb = _RecordingAdapter(
            ready={"endpoint": "127.0.0.1:57066", "flash_command": "west flash"}
        )
        serial = _RecordingAdapter(
            ready={"url": "rfc2217://127.0.0.1:49222", "pty": None}
        )
        output = []
        command = DeviceCommand(
            gdb=gdb,
            serial=serial,
            err=lambda _message: None,
            inf=output.append,
        )
        args = _parse(
            command,
            "build-host",
            "--probe",
            "probe-a",
            "--serial",
            "/dev/tty.test",
            "--gdb-port",
            "57066",
            "--serial-port",
            "49222",
        )

        self.assertEqual(command.run(args), 0)

        self.assertEqual(len(gdb.calls), 1)
        self.assertEqual(len(serial.calls), 1)
        gdb_args, gdb_stop, gdb_signals = gdb.calls[0]
        serial_args, serial_stop, serial_signals = serial.calls[0]
        self.assertIs(gdb_stop, serial_stop)
        self.assertFalse(gdb_signals)
        self.assertFalse(serial_signals)
        self.assertEqual(gdb_args.port, 57066)
        self.assertEqual(gdb_args.probe, "probe-a")
        self.assertEqual(serial_args.remote_rfc2217_port, 49222)
        self.assertEqual(serial_args.local_serial, "/dev/tty.test")
        self.assertTrue(serial_args.rfc2217_only)
        self.assertEqual(gdb.quiet, [True])
        self.assertEqual(serial.quiet, [True])
        self.assertEqual(
            output,
            [
                "Remote  host   build-host",
                "GDB     ready  127.0.0.1:57066",
                "Flash          west flash",
                "Serial  ready  rfc2217://127.0.0.1:49222",
                "Device  ready  press Ctrl-C to stop",
            ],
        )

    def test_no_serial_runs_only_gdb(self):
        gdb = _RecordingAdapter()
        serial = _RecordingAdapter()
        command = DeviceCommand(gdb=gdb, serial=serial, inf=lambda _message: None)
        args = _parse(command, "build-host", "--no-serial", "--probe", "probe-a")

        self.assertEqual(command.run(args), 0)

        self.assertEqual(len(gdb.calls), 1)
        self.assertEqual(serial.calls, [])

    def test_adapter_failure_stops_peer_and_is_reported(self):
        failure = RuntimeError("GDB failed")
        gdb = _RecordingAdapter(failure=failure)
        serial = _BlockingAdapter()
        errors = []
        command = DeviceCommand(
            gdb=gdb,
            serial=serial,
            err=errors.append,
            inf=lambda _message: None,
        )
        args = _parse(command, "build-host", "--serial", "/dev/tty.test")

        with self.assertRaisesRegex(RuntimeError, "GDB failed"):
            command.run(args)

        self.assertEqual(len(serial.calls), 1)
        self.assertTrue(serial.calls[0][1].is_set())
        self.assertEqual(errors, ["GDB forwarding failed: GDB failed"])

    def test_verbose_keeps_adapter_info_enabled(self):
        gdb = _RecordingAdapter()
        command = DeviceCommand(
            gdb=gdb,
            serial=_RecordingAdapter(),
            inf=lambda _message: None,
        )
        args = _parse(command, "build-host", "--no-serial", "--verbose")

        self.assertEqual(command.run(args), 0)
        self.assertEqual(gdb.quiet, [False])

    def test_dashboard_renders_colored_links_rates_and_totals(self):
        args = SimpleNamespace(
            ssh_host="build-host",
            gdb_port=57065,
            serial_port=49221,
            probe="probe-a",
            local_serial="/dev/tty.test",
            baudrate=115200,
        )
        telemetry = {
            "GDB": (
                SimpleNamespace(tunnel_state="up"),
                _Metrics(
                    {
                        "uptime": 12,
                        "client_state": "active",
                        "pyocd_stage": "programming",
                        "gdb_client_to_pyocd_rate": 2048,
                        "gdb_client_to_pyocd_bytes": 4096,
                        "gdb_pyocd_to_client_rate": 1024,
                        "gdb_pyocd_to_client_bytes": 8192,
                        "sessions": 2,
                        "flashes": 1,
                    }
                ),
            ),
            "serial": (
                SimpleNamespace(tunnel_state="up", bridge_state="rfc2217-only"),
                _Metrics(
                    {
                        "uptime": 12,
                        "active_connection": "active",
                        "remote_to_serial_rate": 512,
                        "remote_to_serial": 1024,
                        "serial_to_remote_rate": 256,
                        "serial_to_remote": 2048,
                        "connections": 3,
                        "preemptions": 0,
                    }
                ),
            ),
        }
        ready = {
            "GDB": {"endpoint": "127.0.0.1:57065"},
            "serial": {"url": "rfc2217://127.0.0.1:49221"},
        }

        lines = _DeviceDashboard(args, telemetry, ready, {"GDB", "serial"})._build_lines(100)
        plain = "\n".join(_strip_ansi(line) for line in lines)

        self.assertTrue(any("\033[" in line for line in lines))
        self.assertTrue(all(_visible_len(line) == 100 for line in lines))
        self.assertIn("GDB", plain)
        self.assertIn("Serial", plain)
        self.assertIn("2.0 KiB/s", plain)
        self.assertIn("rfc2217://127.0.0.1:49221", plain)
        self.assertIn("GDB sessions 2", plain)

    def test_gdb_metrics_report_three_second_rates(self):
        metrics = _GdbMetrics()
        metrics.note_gdb_io("client->pyocd", 3072)
        metrics.note_gdb_io("pyocd->client", 1536)

        snapshot = metrics.read()

        self.assertEqual(snapshot["gdb_client_to_pyocd_bytes"], 3072)
        self.assertEqual(snapshot["gdb_pyocd_to_client_bytes"], 1536)
        self.assertEqual(snapshot["gdb_client_to_pyocd_rate"], 1024)
        self.assertEqual(snapshot["gdb_pyocd_to_client_rate"], 512)

    def test_dashboard_restores_terminal_screen_and_cursor(self):
        args = SimpleNamespace(
            ssh_host="build-host",
            gdb_port=57065,
            serial_port=49221,
            probe=None,
            local_serial=None,
            baudrate=115200,
        )
        output = io.StringIO()
        dashboard = _DeviceDashboard(args, {}, {}, set())

        with mock.patch.object(sys, "stdout", output):
            dashboard.start()
            dashboard.render(force=True)
            dashboard.stop()

        rendered = output.getvalue()
        self.assertIn("\033[?1049h", rendered)
        self.assertIn("\033[?25l", rendered)
        self.assertTrue(rendered.endswith("\033[?1049l"))

    def test_requires_serial_unless_disabled(self):
        command = DeviceCommand(gdb=_RecordingAdapter(), serial=_RecordingAdapter())
        args = _parse(command, "build-host")

        with self.assertRaises(SystemExit):
            command.run(args)

    def test_rejects_colliding_remote_ports(self):
        command = DeviceCommand(gdb=_RecordingAdapter(), serial=_RecordingAdapter())
        args = _parse(
            command,
            "build-host",
            "--serial",
            "/dev/tty.test",
            "--gdb-port",
            "57065",
            "--serial-port",
            "57065",
        )

        with self.assertRaises(SystemExit):
            command.run(args)


if __name__ == "__main__":
    unittest.main()
