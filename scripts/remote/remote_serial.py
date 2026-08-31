# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Serial subcommand for west remote."""

from __future__ import annotations

import argparse
import contextlib
import os
import shlex
import signal
import socket
import subprocess
import shutil
import sys
import termios
import threading
import time
import tty
from collections import deque
from pathlib import Path, PurePosixPath

from west import log

from terminal_format import (
    _cell_len,
    _fit_ansi,
    _fit_plain,
    _format_age,
    _visible_len,
)

try:
    import serial
    import serial.rfc2217
except ImportError:  # pragma: no cover - exercised only without pyserial
    serial = None

_PortManagerBase = serial.rfc2217.PortManager if serial is not None else object


DEFAULT_BAUDRATE = 115200
REMOTE_SERIAL_LINK_TIMEOUT = 20
DEFAULT_REMOTE_ESPTOOL_CFG = "/tmp/meshbus-esptool-usb-jtag-reset.cfg"
REMOTE_RFC2217_AUTO_PORT_MIN = 42900
DASHBOARD_RENDER_INTERVAL_SECONDS = 0.5
DASHBOARD_LOOP_SLEEP_SECONDS = 0.1
PREEMPT_SERIAL_SETTLE_SECONDS = 0.25
RFC2217_IDLE_SLEEP_SECONDS = 0.001
RFC2217_SOCKET_BUFFER_SIZE = 256 * 1024
SERIAL_RECONNECT_INTERVAL_SECONDS = 0.5
ESP_USB_JTAG_RESET_SUPPRESS_SECONDS = 2.0
ESP_USB_JTAG_RESET_SEQUENCE = "R0|D0|W0.1|D1|R0|W0.1|R1|D0|R1|W0.1|D0|R0"

C_RESET = "\033[0m"
C_DIM = "\033[2m"
C_BOLD = "\033[1m"
C_GREEN = "\033[38;5;114m"
C_YELLOW = "\033[38;5;221m"
C_RED = "\033[38;5;203m"
C_BLUE = "\033[38;5;117m"
C_CYAN = "\033[38;5;80m"
C_MAGENTA = "\033[38;5;176m"
C_GRAY = "\033[38;5;245m"
C_PANEL = "\033[38;5;67m"


HELP_EPILOG = """\
Examples:
  west remote serial build-host.example.com /dev/tty.usbmodem211201
  west remote serial build-host.example.com /dev/tty.usbmodem211201 --baudrate 921600
  west remote serial build-host.example.com /dev/tty.usbmodem211201 --rfc2217-only --remote-rfc2217-port 49221
  west remote serial build-host.example.com /dev/tty.usbmodem211201 --rfc2217-only --esp-reset-strategy usb-jtag --remote-rfc2217-port 49221

The command maps the local serial device to a remote PTY through RFC2217 over
an SSH reverse tunnel. By default, the remote serial name is the same as the
local device path. The command keeps running until interrupted, then removes
the remote symlink it created.

The remote /dev path is a PTY and is intended for serial logs and shell access.
For tools that need DTR/RTS control, such as esptool flashing, use
--rfc2217-only and pass the printed rfc2217:// URL as the device.

ESP USB Serial/JTAG ports are detected automatically. Their RFC2217 DTR/RTS
control is converted locally into the USB Serial/JTAG reset sequence unless
--esp-reset-strategy default is used.
"""


REMOTE_BRIDGE_CODE = r"""
import os
import pty
import sys
import contextlib
import threading
import time
import tty

import serial


remote_rfc2217_port = int(sys.argv[1])
remote_link = sys.argv[2]
baudrate = int(sys.argv[3])
replace_symlink = sys.argv[4] == "1"

stop = threading.Event()
counts = {"pty_to_serial": 0, "serial_to_pty": 0}


def fail(message):
    print("ERROR " + message, file=sys.stderr, flush=True)
    sys.exit(1)


def stdin_watcher():
    try:
        sys.stdin.buffer.read()
    finally:
        stop.set()


def create_remote_link(slave_name):
    parent = os.path.dirname(remote_link)
    if parent and not os.path.isdir(parent):
        fail("remote serial parent directory does not exist: " + parent)

    if os.path.lexists(remote_link):
        if not os.path.islink(remote_link):
            fail("remote serial path exists and is not a symlink: " + remote_link)
        if not replace_symlink:
            fail("remote serial symlink already exists: " + remote_link)
        os.unlink(remote_link)

    os.symlink(slave_name, remote_link)


def pty_to_serial(master, ser):
    while not stop.is_set():
        try:
            data = os.read(master, 1024)
        except Exception:
            stop.set()
            return
        if data:
            counts["pty_to_serial"] += len(data)
            try:
                ser.write(data)
            except Exception:
                stop.set()
                return


def serial_to_pty(master, ser):
    while not stop.is_set():
        try:
            data = ser.read(4096)
        except Exception:
            stop.set()
            return
        if data:
            counts["serial_to_pty"] += len(data)
            try:
                os.write(master, data)
            except Exception:
                stop.set()
                return


ser = serial.serial_for_url(
    "rfc2217://127.0.0.1:%d" % remote_rfc2217_port,
    baudrate=baudrate,
    timeout=0.05,
)
threading.Thread(target=stdin_watcher, daemon=True).start()
master, slave = pty.openpty()
tty.setraw(slave)
slave_name = os.ttyname(slave)
create_remote_link(slave_name)
print("READY %s %s" % (remote_link, slave_name), flush=True)

threading.Thread(target=pty_to_serial, args=(master, ser), daemon=True).start()
threading.Thread(target=serial_to_pty, args=(master, ser), daemon=True).start()

try:
    while not stop.is_set():
        time.sleep(0.2)
finally:
    try:
        if os.path.islink(remote_link) and os.readlink(remote_link) == slave_name:
            os.unlink(remote_link)
    except FileNotFoundError:
        pass
    with contextlib.suppress(Exception):
        ser.close()
    with contextlib.suppress(Exception):
        os.close(master)
    with contextlib.suppress(Exception):
        os.close(slave)
    print("COUNTS pty_to_serial=%d serial_to_pty=%d" % (
        counts["pty_to_serial"],
        counts["serial_to_pty"],
    ), flush=True)
"""


class SerialCommand:
    """Register and run the remote serial subcommand."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "serial",
            help="map a local serial device to the remote host",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Map a local serial device to the remote host with the same device name.",
        )
        parser.add_argument("ssh_host", help="SSH host that will receive the serial mapping")
        parser.add_argument(
            "local_serial",
            help="Local serial device path, for example /dev/tty.usbmodem211201",
        )
        parser.add_argument(
            "--remote-serial",
            default=None,
            help="Remote serial symlink path (default: same as local_serial)",
        )
        parser.add_argument(
            "--baudrate",
            type=int,
            default=DEFAULT_BAUDRATE,
            help=f"Serial baudrate used by the local RFC2217 endpoint (default: {DEFAULT_BAUDRATE})",
        )
        parser.add_argument(
            "--remote-python",
            default=None,
            help="Remote Python executable with pyserial installed",
        )
        parser.add_argument(
            "--remote-rfc2217-port",
            type=int,
            default=None,
            help=(
                "Remote loopback TCP port for the SSH reverse tunnel "
                f"(default: random free remote port >= {REMOTE_RFC2217_AUTO_PORT_MIN})"
            ),
        )
        parser.add_argument(
            "--local-rfc2217-port",
            type=int,
            default=0,
            help="Local loopback TCP port for the RFC2217 server (default: random)",
        )
        parser.add_argument(
            "--no-replace-symlink",
            action="store_true",
            help="Fail if the remote serial symlink already exists",
        )
        parser.add_argument(
            "--rfc2217-only",
            action="store_true",
            help=(
                "Only expose the remote RFC2217 endpoint; do not create a "
                "remote PTY symlink. Use this for esptool/west flash."
            ),
        )
        parser.add_argument(
            "--esp-reset-strategy",
            choices=("auto", "usb-jtag", "default", "remote-cfg"),
            default="auto",
            help=(
                "ESP reset handling for remote esptool examples: auto detects "
                "ESP USB Serial/JTAG ports and proxies reset locally, usb-jtag "
                "forces the local proxy, default disables it, remote-cfg uses "
                "the legacy remote ESPTOOL_CFGFILE profile (default: auto)"
            ),
        )
        parser.add_argument(
            "--esp-usb-jtag-reset",
            dest="esp_reset_strategy",
            action="store_const",
            const="usb-jtag",
            help="Deprecated alias for --esp-reset-strategy usb-jtag",
        )
        parser.add_argument(
            "--remote-esptool-cfg",
            default=DEFAULT_REMOTE_ESPTOOL_CFG,
            help=(
                "Remote temporary esptool config path used only with "
                "--esp-reset-strategy remote-cfg "
                f"(default: {DEFAULT_REMOTE_ESPTOOL_CFG})"
            ),
        )
        parser.add_argument(
            "--ready-timeout",
            type=float,
            default=REMOTE_SERIAL_LINK_TIMEOUT,
            help=f"Seconds to wait for the remote PTY bridge (default: {REMOTE_SERIAL_LINK_TIMEOUT})",
        )
        parser.add_argument(
            "--no-dashboard",
            action="store_true",
            help="Disable the live terminal dashboard even when stdout is a TTY",
        )
        parser.set_defaults(handler=self.run)
        return parser

    def run(self, args) -> int:
        _require_pyserial()
        local_serial = _validate_device_path(args.local_serial, "local serial")
        remote_serial = _validate_device_path(
            args.remote_serial or args.local_serial,
            "remote serial",
        )
        if not Path(local_serial.as_posix()).exists():
            log.wrn(f"local serial device is not present; waiting for reconnect: {local_serial}")
        if not args.ssh_host:
            log.die("missing SSH host")
        esp_reset_strategy, esp_reset_reason = _resolve_esp_reset_strategy(
            local_serial.as_posix(),
            args.esp_reset_strategy,
        )

        remote_python = None
        if not args.rfc2217_only:
            remote_python = args.remote_python or _discover_remote_python(args.ssh_host)
        runtime = _RuntimeState()
        metrics = _SerialMetrics()
        reset_proxy_policy = _reset_proxy_policy(esp_reset_strategy, args.rfc2217_only)
        local_server = _Rfc2217Server(
            local_serial.as_posix(),
            args.baudrate,
            args.local_rfc2217_port,
            metrics,
            reset_proxy_policy=reset_proxy_policy,
        )
        processes: list[subprocess.Popen] = []
        bridge: subprocess.Popen | None = None
        remote_esptool_cfg: PurePosixPath | None = None
        last_remote_pty = ""
        stop = threading.Event()

        def request_stop(_signum=None, _frame=None):
            stop.set()

        old_sigint = signal.signal(signal.SIGINT, request_stop)
        old_sigterm = signal.signal(signal.SIGTERM, request_stop)
        try:
            local_server.start()
            remote_rfc2217_port = args.remote_rfc2217_port or _allocate_remote_tcp_port(
                args.ssh_host,
                args.remote_python,
            )
            runtime.local_serial = local_serial.as_posix()
            runtime.remote_serial = remote_serial.as_posix()
            runtime.ssh_host = args.ssh_host
            runtime.baudrate = args.baudrate
            runtime.esp_reset_strategy = _display_esp_reset_strategy(esp_reset_strategy)
            tunnel = _start_reverse_tunnel(
                args.ssh_host,
                remote_rfc2217_port,
                local_server.host,
                local_server.port,
            )
            processes.append(tunnel)
            _ensure_process_running(tunnel, "ssh reverse tunnel")

            remote_rfc2217_url = f"rfc2217://127.0.0.1:{remote_rfc2217_port}"
            runtime.local_rfc2217 = f"{local_server.host}:{local_server.port}"
            runtime.remote_rfc2217 = f"127.0.0.1:{remote_rfc2217_port}"
            runtime.remote_rfc2217_url = remote_rfc2217_url
            log.inf(
                "local RFC2217 endpoint: "
                f"{local_server.host}:{local_server.port}; "
                "remote tunnel endpoint: "
                f"127.0.0.1:{remote_rfc2217_port}"
            )
            if esp_reset_strategy == "remote-cfg":
                remote_esptool_cfg = _validate_remote_temp_path(
                    args.remote_esptool_cfg,
                    "remote esptool config",
                )
                _write_remote_esptool_cfg(args.ssh_host, remote_esptool_cfg.as_posix())
                runtime.remote_esptool_cfg = remote_esptool_cfg.as_posix()
                log.inf("ESP reset strategy: remote-cfg (" + esp_reset_reason + ")")
                log.inf(f"remote esptool config: {remote_esptool_cfg}")
            elif esp_reset_strategy == "usb-jtag-proxy":
                log.inf("ESP reset strategy: usb-jtag proxy (" + esp_reset_reason + ")")
            else:
                log.inf("ESP reset strategy: default (" + esp_reset_reason + ")")
            if args.rfc2217_only:
                log.inf(f"remote RFC2217 device URL: {remote_rfc2217_url}")
                log.inf(
                    "ESP32 flash example: "
                    + _west_flash_example(remote_rfc2217_url, remote_esptool_cfg)
                )
                runtime.bridge_state = "rfc2217-only"
                log.inf("press Ctrl-C to stop the remote RFC2217 tunnel")
            else:
                bridge = _start_remote_bridge(
                    args.ssh_host,
                    remote_python,
                    remote_rfc2217_port,
                    remote_serial.as_posix(),
                    args.baudrate,
                    replace_symlink=not args.no_replace_symlink,
                )
                processes.append(bridge)
                ready = _wait_remote_ready(bridge, args.ready_timeout)
                runtime.remote_pty = ready.remote_pty
                last_remote_pty = ready.remote_pty
                runtime.bridge_state = "mapped"
                log.inf(f"remote serial mapped: {ready.remote_link} -> {ready.remote_pty}")
                log.inf(
                    "remote PTY is for serial logs/shell. Direct RFC2217 clients "
                    "can temporarily preempt it; the PTY will be restored after "
                    "the direct client disconnects."
                )
                if remote_esptool_cfg is not None:
                    log.inf(
                        "ESP32 flash example: "
                        + _west_flash_example(remote_rfc2217_url, remote_esptool_cfg)
                    )
                log.inf("press Ctrl-C to stop and remove the remote serial mapping")

            dashboard = None
            if sys.stdout.isatty() and not args.no_dashboard:
                dashboard = _SerialDashboard(
                    runtime,
                    metrics,
                )
                dashboard.start()

            while not stop.is_set():
                _ensure_process_running(tunnel, "ssh reverse tunnel")
                runtime.tunnel_state = "up"
                if not args.rfc2217_only:
                    assert bridge is not None
                    if bridge.poll() is not None:
                        runtime.bridge_state = "preempted"
                        runtime.remote_pty = ""
                        if local_server.has_active_connection():
                            if dashboard is not None:
                                dashboard.render()
                            time.sleep(DASHBOARD_LOOP_SLEEP_SECONDS)
                            continue
                        _drain_pipe(bridge.stdout)
                        _drain_pipe(bridge.stderr)
                        runtime.bridge_state = "restoring"
                        if dashboard is None:
                            log.inf("remote PTY bridge disconnected; restoring serial symlink")
                        bridge = _start_remote_bridge(
                            args.ssh_host,
                            remote_python,
                            remote_rfc2217_port,
                            remote_serial.as_posix(),
                            args.baudrate,
                            replace_symlink=True,
                            quiet=dashboard is not None,
                        )
                        processes.append(bridge)
                        ready = _wait_remote_ready(bridge, args.ready_timeout)
                        runtime.remote_pty = ready.remote_pty
                        last_remote_pty = ready.remote_pty
                        runtime.bridge_state = "mapped"
                        if dashboard is None:
                            log.inf(f"remote serial restored: {ready.remote_link} -> {ready.remote_pty}")
                if dashboard is not None:
                    dashboard.render()
                time.sleep(DASHBOARD_LOOP_SLEEP_SECONDS if dashboard is not None else 0.2)
        finally:
            if "dashboard" in locals() and dashboard is not None:
                dashboard.stop()
            signal.signal(signal.SIGINT, old_sigint)
            signal.signal(signal.SIGTERM, old_sigterm)
            _stop_processes(processes)
            local_server.stop()
            if not args.rfc2217_only and last_remote_pty:
                _cleanup_remote_link(
                    args.ssh_host,
                    remote_serial.as_posix(),
                    expected_target=last_remote_pty,
                )
            if remote_esptool_cfg is not None:
                _cleanup_remote_file(args.ssh_host, remote_esptool_cfg.as_posix())
        return 0


class _RemoteReady:
    def __init__(self, remote_link: str, remote_pty: str):
        self.remote_link = remote_link
        self.remote_pty = remote_pty


class _RuntimeState:
    def __init__(self):
        self.local_serial = ""
        self.remote_serial = ""
        self.remote_pty = ""
        self.ssh_host = ""
        self.baudrate = 0
        self.local_rfc2217 = ""
        self.remote_rfc2217 = ""
        self.remote_rfc2217_url = ""
        self.esp_reset_strategy = ""
        self.remote_esptool_cfg = ""
        self.tunnel_state = "starting"
        self.bridge_state = "starting"


class _SerialMetrics:
    def __init__(self):
        self._lock = threading.Lock()
        self.started_at = time.monotonic()
        self.serial_to_remote = 0
        self.remote_to_serial = 0
        self.connections = 0
        self.preemptions = 0
        self.active_connection = "idle"
        self.last_connection_at = 0.0
        self.last_disconnect_at = 0.0
        self.serial_settings = {
            "baudrate": DEFAULT_BAUDRATE,
            "bytesize": 8,
            "parity": "N",
            "stopbits": 1,
            "dtr": None,
            "rts": None,
            "break_condition": None,
        }
        self.byte_events = deque(maxlen=2000)

    def note_connection(self, *, preempted: bool) -> None:
        now = time.monotonic()
        with self._lock:
            self.connections += 1
            if preempted:
                self.preemptions += 1
            self.active_connection = "active"
            self.last_connection_at = now

    def note_disconnect(self) -> None:
        with self._lock:
            self.active_connection = "idle"
            self.last_disconnect_at = time.monotonic()

    def note_connection_state(self, state: str) -> None:
        with self._lock:
            self.active_connection = state

    def note_serial_to_remote(self, data: bytes) -> None:
        self._note_bytes("serial->remote", data)

    def note_remote_to_serial(self, data: bytes) -> None:
        self._note_bytes("remote->serial", data)

    def note_serial_settings(self, **settings) -> None:
        with self._lock:
            self.serial_settings.update(settings)

    def _note_bytes(self, direction: str, data: bytes) -> None:
        now = time.monotonic()
        with self._lock:
            if direction == "serial->remote":
                self.serial_to_remote += len(data)
            else:
                self.remote_to_serial += len(data)
            self.byte_events.append((now, direction, len(data)))

    def read(self) -> dict:
        now = time.monotonic()
        with self._lock:
            events = list(self.byte_events)
            serial_to_remote = self.serial_to_remote
            remote_to_serial = self.remote_to_serial
            connections = self.connections
            preemptions = self.preemptions
            active = self.active_connection
            started_at = self.started_at
            last_connection_at = self.last_connection_at
            last_disconnect_at = self.last_disconnect_at
            serial_settings = dict(self.serial_settings)
        window_start = now - 3.0
        recent_serial = sum(size for ts, direction, size in events if ts >= window_start and direction == "serial->remote")
        recent_remote = sum(size for ts, direction, size in events if ts >= window_start and direction == "remote->serial")
        return {
            "uptime": now - started_at,
            "serial_to_remote": serial_to_remote,
            "remote_to_serial": remote_to_serial,
            "serial_to_remote_rate": recent_serial / 3.0,
            "remote_to_serial_rate": recent_remote / 3.0,
            "connections": connections,
            "preemptions": preemptions,
            "active_connection": active,
            "last_connection_age": None if last_connection_at == 0.0 else now - last_connection_at,
            "last_disconnect_age": None if last_disconnect_at == 0.0 else now - last_disconnect_at,
            "serial_settings": serial_settings,
        }


class _SerialDashboard:
    def __init__(
        self,
        runtime: _RuntimeState,
        metrics: _SerialMetrics,
    ):
        self.runtime = runtime
        self.metrics = metrics
        self.enabled = False
        self.needs_render = True
        self._last_render_at = 0.0
        self._stdin_fd: int | None = None
        self._old_stdin_attrs = None

    def start(self) -> None:
        self.enabled = True
        self._enable_input_capture()
        sys.stdout.write(
            "\033[?1049h\033[H\033[2J\033[?25l\033[?7l"
            "\033[?1007l\033[?1000h\033[?1002h\033[?1006h"
        )
        sys.stdout.flush()

    def stop(self) -> None:
        if not self.enabled:
            return
        with contextlib.suppress(Exception):
            sys.stdout.write(
                "\033[?1006l\033[?1002l\033[?1000l\033[?1007h"
                "\033[?7h\033[?25h\033[0m\033[?1049l"
            )
            sys.stdout.flush()
        self._restore_input()
        self.enabled = False

    def render(self) -> None:
        if not self.enabled:
            return
        self._drain_input()
        size = shutil.get_terminal_size((100, 30))
        width = size.columns
        height = size.lines
        width = max(40, width)
        height = max(16, height)
        snap = self.metrics.read()
        inner = width - 4
        has_extra_info = bool(self.runtime.remote_esptool_cfg or self.runtime.esp_reset_strategy)
        base_fixed_rows = 11 if has_extra_info else 10
        optional_gap_rows = max(0, height - base_fixed_rows - 1)
        title_gap_rows = min(2, optional_gap_rows)
        section_gap_rows = min(2, max(0, optional_gap_rows - title_gap_rows))
        now = time.monotonic()
        if (
            not self.needs_render
            and self._last_render_at > 0.0
            and now - self._last_render_at < DASHBOARD_RENDER_INTERVAL_SECONDS
        ):
            return
        lines = self._build_lines(
            width,
            height,
            snap,
            section_gap_rows,
            title_gap_rows,
        )
        output = [f"\r\033[2K{line}" for line in lines]
        sys.stdout.write("\033[H\033[J" + "\n".join(output))
        sys.stdout.flush()
        self.needs_render = False
        self._last_render_at = now

    def _build_lines(
        self,
        width: int,
        height: int,
        snap: dict,
        section_gap_rows: int,
        title_gap_rows: int,
    ) -> list[str]:
        inner = width - 4
        gaps_before = set(range(1, section_gap_rows + 1))
        title_gaps = set(range(1, title_gap_rows + 1))
        settings = snap["serial_settings"]
        baudrate = settings.get("baudrate") or self.runtime.baudrate
        serial_mode = _format_serial_mode(settings)
        lines = [
            self._top(width, " 󰕓 Meshbus Remote Serial "),
            self._row(inner, [
                (C_BLUE, "󰒋 host "),
                ("", self.runtime.ssh_host + "   "),
                (C_BLUE, "󰌘 tunnel "),
                self._status_segment(self.runtime.tunnel_state, good={"up"}),
            ]),
            self._row(inner, [
                (C_MAGENTA, "󰈙 local "),
                ("", self.runtime.local_serial + " "),
                (C_DIM, f"@ {baudrate} baud {serial_mode}"),
            ]),
            self._row(inner, [
                (C_MAGENTA, "󰑓 remote "),
                ("", self.runtime.remote_serial + " "),
                (C_DIM, f"-> {self.runtime.remote_pty or '-'}"),
            ]),
            self._row(inner, [
                (C_CYAN, "󰖟 rfc2217 "),
                ("", self.runtime.remote_rfc2217_url + "   "),
                (C_DIM, f"local {self.runtime.local_rfc2217}   "),
                (C_CYAN, "󰇚 bridge "),
                self._status_segment(
                    self.runtime.bridge_state,
                    good={"mapped", "rfc2217-only"},
                    warn_prefixes=("preempted", "restoring", "starting"),
                ),
            ]),
        ]
        if self.runtime.remote_esptool_cfg:
            lines.append(self._row(inner, [
                (C_YELLOW, "󰛓 esptool cfg "),
                ("", self.runtime.remote_esptool_cfg),
            ]))
        elif self.runtime.esp_reset_strategy:
            lines.append(self._row(inner, [
                (C_YELLOW, "󰛓 esp reset "),
                ("", self.runtime.esp_reset_strategy),
            ]))
        if 1 in gaps_before:
            lines.append(self._blank_row(inner))
        lines.extend([
            self._section(inner, "traffic"),
        ])
        if 1 in title_gaps:
            lines.append(self._blank_row(inner))
        lines.extend([
            self._row(inner, [
                (C_GREEN, "󰁝 remote->serial "),
                ("", f"{_format_bytes(snap['remote_to_serial']):>10} "),
                (C_DIM, f"{_format_rate(snap['remote_to_serial_rate']):>10} "),
                (C_GREEN, _bar(snap["remote_to_serial_rate"], inner - 48, C_GREEN)),
            ]),
            self._row(inner, [
                (C_BLUE, "󰁅 serial->remote "),
                ("", f"{_format_bytes(snap['serial_to_remote']):>10} "),
                (C_DIM, f"{_format_rate(snap['serial_to_remote_rate']):>10} "),
                (C_BLUE, _bar(snap["serial_to_remote_rate"], inner - 48, C_BLUE)),
            ]),
            self._row(inner, [
                (C_CYAN, "󰩠 sessions "),
                ("", f"{snap['connections']}   "),
                (C_YELLOW, "󰜉 preemptions "),
                ("", f"{snap['preemptions']}   "),
                (C_MAGENTA, "󰔟 active "),
                self._status_segment(
                    snap["active_connection"],
                    good={"active"},
                    warn_prefixes=("waiting",),
                ),
                (C_DIM, "   restore immediate"),
            ]),
            self._row(inner, [
                (C_BLUE, "󰅐 last conn "),
                (C_DIM, _format_age(snap["last_connection_age"]) + "   "),
                (C_BLUE, "󰅒 last close "),
                (C_DIM, _format_age(snap["last_disconnect_age"]) + "   "),
                (C_MAGENTA, "󰘲 lines "),
                (C_DIM, _format_line_state(settings)),
            ]),
        ])
        if 2 in gaps_before:
            lines.append(self._blank_row(inner))
        controls_rows = 3 + (1 if 2 in title_gaps else 0)
        while len(lines) < height - controls_rows:
            lines.append(self._blank_row(inner))

        lines.extend([
            self._section(inner, "controls"),
        ])
        if 2 in title_gaps:
            lines.append(self._blank_row(inner))
        lines.extend([
            self._row(inner, [
                (C_GREEN, "mode monitor   "),
                (C_DIM, "Ctrl-C stop  traffic only"),
            ]),
            self._bottom(width),
        ])
        return lines[:height]

    def _enable_input_capture(self) -> None:
        if not sys.stdin.isatty():
            return
        fd = sys.stdin.fileno()
        with contextlib.suppress(Exception):
            self._old_stdin_attrs = termios.tcgetattr(fd)
            tty.setcbreak(fd)
            self._stdin_fd = fd

    def _restore_input(self) -> None:
        fd = self._stdin_fd
        if fd is None:
            return
        if self._old_stdin_attrs is not None:
            with contextlib.suppress(Exception):
                termios.tcsetattr(fd, termios.TCSADRAIN, self._old_stdin_attrs)
        self._stdin_fd = None
        self._old_stdin_attrs = None

    def _drain_input(self) -> None:
        fd = self._stdin_fd
        if fd is None:
            return
        import select

        while True:
            with contextlib.suppress(Exception):
                ready, _, _ = select.select([fd], [], [], 0)
                if not ready:
                    return
                os.read(fd, 4096)
                continue
            return

    def _status_segment(
        self,
        value: str,
        *,
        good: set[str] | None = None,
        warn_prefixes: tuple[str, ...] = (),
    ) -> tuple[str, str]:
        good = good or set()
        if value in good:
            return C_GREEN, f"● {value}"
        if value == "idle":
            return C_GRAY, f"● {value}"
        if value.startswith(warn_prefixes):
            return C_YELLOW, f"● {value}"
        return C_RED, f"● {value}"

    def _top(self, width: int, title: str) -> str:
        inner = width - 2
        title = _fit_plain(title, inner)
        return (
            f"{C_PANEL}╭{C_RESET}"
            f"{C_BOLD}{C_CYAN}{title}{C_RESET}"
            f"{C_PANEL}{'─' * max(0, inner - _visible_len(title))}╮{C_RESET}"
        )

    def _bottom(self, width: int) -> str:
        return f"{C_PANEL}╰{'─' * max(0, width - 2)}╯{C_RESET}"

    def _section(self, inner: int, title: str) -> str:
        label = f" {title} "
        fill = "─" * max(0, inner + 2 - _cell_len(label))
        return f"{C_PANEL}├{C_RESET}{C_BOLD}{C_GRAY}{label}{C_RESET}{C_PANEL}{fill}┤{C_RESET}"

    def _row(self, inner: int, segments: list[tuple[str, str]]) -> str:
        content = _fit_ansi("".join(f"{color}{text}{C_RESET if color else ''}" for color, text in segments), inner)
        return f"{C_PANEL}│{C_RESET} {content} {C_PANEL}│{C_RESET}"

    def _blank_row(self, inner: int) -> str:
        return f"{C_PANEL}│{C_RESET} {' ' * inner} {C_PANEL}│{C_RESET}"


def _format_bytes(value: int) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    amount = float(value)
    for unit in units:
        if amount < 1024 or unit == units[-1]:
            if unit == "B":
                return f"{int(amount)} {unit}"
            return f"{amount:.1f} {unit}"
        amount /= 1024
    return f"{value} B"


def _format_rate(value: float) -> str:
    return _format_bytes(int(value)) + "/s"


def _format_serial_mode(settings: dict) -> str:
    bytesize = settings.get("bytesize") or 8
    parity = settings.get("parity") or "N"
    stopbits = settings.get("stopbits") or 1
    if isinstance(stopbits, float) and stopbits.is_integer():
        stopbits = int(stopbits)
    return f"{bytesize}{parity}{stopbits}"


def _format_line_state(settings: dict) -> str:
    return (
        "DTR=" + _format_tristate(settings.get("dtr"))
        + " RTS=" + _format_tristate(settings.get("rts"))
        + " BRK=" + _format_tristate(settings.get("break_condition"))
    )


def _format_tristate(value) -> str:
    if value is None:
        return "-"
    return "1" if bool(value) else "0"


def _bar(rate: float, width: int, color: str) -> str:
    width = max(8, min(width, 28))
    filled = min(width, int((rate / 4096.0) * width))
    if rate > 0 and filled == 0:
        filled = 1
    empty = width - filled
    return f"{C_DIM}[{C_RESET}{color}{'█' * filled}{C_DIM}{'░' * empty}]{C_RESET}"


class _Rfc2217Server:
    def __init__(
        self,
        serial_path: str,
        baudrate: int,
        port: int,
        metrics: _SerialMetrics,
        *,
        reset_proxy_policy: str = "off",
    ):
        self.serial_path = serial_path
        self.baudrate = baudrate
        self.host = "127.0.0.1"
        self.port = port
        self.metrics = metrics
        self.reset_proxy_policy = reset_proxy_policy
        self._socket: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._error: BaseException | None = None
        self._active_lock = threading.Lock()
        self._active_conn: socket.socket | None = None
        self._active_thread: threading.Thread | None = None
        self._active_serial = None
        self._active_deadline = 0.0

    def start(self) -> None:
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()
        if not self._ready.wait(timeout=5):
            log.die("local RFC2217 server did not start")
        if self._error is not None:
            log.die(f"local RFC2217 server failed: {self._error}")

    def stop(self) -> None:
        self._stop.set()
        if self._socket is not None:
            with contextlib.suppress(OSError):
                self._socket.close()
        self._close_active_connection()
        if self._thread is not None:
            self._thread.join(timeout=2)

    def has_active_connection(self) -> bool:
        with self._active_lock:
            thread = self._active_thread
            if thread is not None and thread.is_alive():
                return True
            return time.monotonic() < self._active_deadline

    def _serve(self) -> None:
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._socket.bind((self.host, self.port))
            self.port = self._socket.getsockname()[1]
            self._socket.listen(1)
            self._socket.settimeout(0.2)
            self._ready.set()
            while not self._stop.is_set():
                try:
                    conn, _addr = self._socket.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                _configure_rfc2217_socket(conn)
                self._replace_active_connection(conn)
        except BaseException as exc:  # noqa: BLE001 - surface to west log
            self._error = exc
            self._ready.set()

    def _replace_active_connection(self, conn: socket.socket) -> None:
        with self._active_lock:
            old_thread = self._active_thread
            preempted = old_thread is not None and old_thread.is_alive()
        self._close_active_connection()
        if preempted:
            time.sleep(PREEMPT_SERIAL_SETTLE_SECONDS)
        reset_proxy = (
            self.reset_proxy_policy == "always"
            or (self.reset_proxy_policy == "preempted" and preempted)
        )
        thread = threading.Thread(
            target=self._handle_connection,
            args=(conn, reset_proxy),
            daemon=True,
        )
        with self._active_lock:
            self._active_conn = conn
            self._active_thread = thread
            self._active_deadline = time.monotonic() + 5.0
        self.metrics.note_connection(preempted=preempted)
        thread.start()

    def _close_active_connection(self) -> None:
        old_thread: threading.Thread | None
        with self._active_lock:
            old_conn = self._active_conn
            old_thread = self._active_thread
            old_serial = self._active_serial
            self._active_conn = None
            self._active_thread = None
            self._active_serial = None
            self._active_deadline = 0.0
        if old_conn is not None:
            with contextlib.suppress(OSError):
                old_conn.shutdown(socket.SHUT_RDWR)
            with contextlib.suppress(OSError):
                old_conn.close()
        if old_serial is not None:
            with contextlib.suppress(Exception):
                old_serial.close()
        if old_thread is not None and old_thread.is_alive():
            old_thread.join(timeout=2)

    def _handle_connection(self, conn: socket.socket, reset_proxy: bool) -> None:
        ser = None
        try:
            ser = _ReconnectingSerial(
                self.serial_path,
                self.baudrate,
                self.metrics,
                self._stop,
            )
            self.metrics.note_serial_settings(**_serial_settings_from_port(ser))
            with contextlib.closing(conn), contextlib.closing(ser):
                with self._active_lock:
                    if self._active_conn is conn:
                        self._active_serial = ser
                with contextlib.suppress(Exception):
                    ser.dtr = False
                    ser.rts = False
                    self.metrics.note_serial_settings(dtr=ser.dtr, rts=ser.rts)
                manager = _SafePortManager(
                    _SerialControlProxy(
                        ser,
                        self.metrics,
                        usb_jtag_reset_proxy=reset_proxy,
                    ),
                    _SocketWriter(conn),
                )
                conn.setblocking(False)
                with self._active_lock:
                    if self._active_conn is conn:
                        self._active_deadline = 0.0
                while not self._stop.is_set():
                    processed_data = False
                    data = _recv_nonblocking(conn)
                    if data == b"":
                        return
                    if data is not None:
                        processed_data = True
                        payload = bytearray()
                        for chunk in manager.filter(data):
                            chunk_bytes = bytes([chunk]) if isinstance(chunk, int) else chunk
                            payload.extend(chunk_bytes)
                        if payload:
                            ser.write(payload)
                            self.metrics.note_remote_to_serial(bytes(payload))

                    serial_data = ser.read(4096)
                    if serial_data:
                        processed_data = True
                        self.metrics.note_serial_to_remote(serial_data)
                        conn.sendall(b"".join(manager.escape(serial_data)))

                    with contextlib.suppress(Exception):
                        manager.check_modem_lines()
                    if not processed_data:
                        time.sleep(RFC2217_IDLE_SLEEP_SECONDS)
        except Exception:
            pass
        finally:
            with self._active_lock:
                if self._active_conn is conn:
                    self._active_conn = None
                    self._active_thread = None
                    if self._active_serial is ser:
                        self._active_serial = None
            self.metrics.note_disconnect()


class _SocketWriter:
    def __init__(self, sock: socket.socket):
        self.sock = sock

    def write(self, data: bytes) -> None:
        self.sock.sendall(data)


class _ReconnectingSerial:
    _SETTING_NAMES = {"baudrate", "bytesize", "parity", "stopbits", "xonxoff", "rtscts", "dsrdtr"}
    _LINE_NAMES = {"dtr", "rts", "break_condition"}
    _MODEM_NAMES = {"cts", "dsr", "ri", "cd"}

    def __init__(
        self,
        serial_path: str,
        baudrate: int,
        metrics: _SerialMetrics,
        stop_event: threading.Event,
    ):
        object.__setattr__(self, "serial_path", serial_path)
        object.__setattr__(self, "metrics", metrics)
        object.__setattr__(self, "stop_event", stop_event)
        object.__setattr__(self, "_lock", threading.RLock())
        object.__setattr__(self, "_serial", None)
        object.__setattr__(self, "_closed", False)
        object.__setattr__(self, "_last_open_attempt", 0.0)
        object.__setattr__(self, "_settings", {
            "baudrate": baudrate,
            "bytesize": 8,
            "parity": "N",
            "stopbits": 1,
            "xonxoff": False,
            "rtscts": False,
            "dsrdtr": False,
            "dtr": None,
            "rts": None,
            "break_condition": False,
        })

    def close(self) -> None:
        with self._lock:
            self._closed = True
            self._close_locked()

    def read(self, size: int) -> bytes:
        ser = self._ensure_open(block=False)
        if ser is None:
            return b""
        try:
            return ser.read(size)
        except Exception:
            self._mark_disconnected()
            return b""

    def write(self, data) -> int:
        payload = bytes(data)
        written = 0
        while written < len(payload) and not self.stop_event.is_set() and not self._closed:
            ser = self._ensure_open(block=True)
            if ser is None:
                break
            try:
                written += ser.write(payload[written:])
            except Exception:
                self._mark_disconnected()
        return written

    def reset_input_buffer(self) -> None:
        ser = self._ensure_open(block=False)
        if ser is not None:
            with contextlib.suppress(Exception):
                ser.reset_input_buffer()

    def reset_output_buffer(self) -> None:
        ser = self._ensure_open(block=False)
        if ser is not None:
            with contextlib.suppress(Exception):
                ser.reset_output_buffer()

    def __getattr__(self, name: str):
        if name in self._MODEM_NAMES:
            ser = self._ensure_open(block=False)
            if ser is None:
                return False
            try:
                return getattr(ser, name)
            except Exception:
                self._mark_disconnected()
                return False
        if name in self._SETTING_NAMES or name in self._LINE_NAMES:
            with self._lock:
                if name in self._settings:
                    return self._settings[name]
        ser = self._ensure_open(block=False)
        if ser is None:
            raise AttributeError(name)
        return getattr(ser, name)

    def __setattr__(self, name: str, value) -> None:
        if name in self._SETTING_NAMES or name in self._LINE_NAMES:
            with self._lock:
                self._settings[name] = value
                ser = self._serial
            if ser is not None:
                try:
                    setattr(ser, name, value)
                except Exception:
                    self._mark_disconnected()
                    return
            self.metrics.note_serial_settings(**{name: value})
            return
        object.__setattr__(self, name, value)

    def _ensure_open(self, *, block: bool):
        while not self.stop_event.is_set() and not self._closed:
            with self._lock:
                ser = self._serial
                if ser is not None:
                    return ser
            if not self._open_once():
                if not block:
                    return None
                time.sleep(SERIAL_RECONNECT_INTERVAL_SECONDS)
                continue
        return None

    def _open_once(self) -> bool:
        now = time.monotonic()
        with self._lock:
            if self._serial is not None:
                return True
            if now - self._last_open_attempt < SERIAL_RECONNECT_INTERVAL_SECONDS:
                return False
            self._last_open_attempt = now
            settings = dict(self._settings)
        try:
            ser = serial.Serial(
                self.serial_path,
                settings["baudrate"],
                timeout=0,
                rtscts=False,
                dsrdtr=False,
            )
            for name, value in settings.items():
                if value is not None and name not in {"baudrate"}:
                    with contextlib.suppress(Exception):
                        setattr(ser, name, value)
        except Exception:
            self.metrics.note_connection_state("waiting-device")
            return False
        with self._lock:
            self._serial = ser
        self.metrics.note_connection_state("active")
        self.metrics.note_serial_settings(**_serial_settings_from_port(self))
        return True

    def _mark_disconnected(self) -> None:
        with self._lock:
            self._close_locked()
        self.metrics.note_connection_state("waiting-device")

    def _close_locked(self) -> None:
        ser = self._serial
        self._serial = None
        if ser is not None:
            with contextlib.suppress(Exception):
                ser.close()


class _SerialControlProxy:
    def __init__(self, wrapped, metrics: _SerialMetrics, *, usb_jtag_reset_proxy: bool = False):
        object.__setattr__(self, "_wrapped", wrapped)
        object.__setattr__(self, "_metrics", metrics)
        object.__setattr__(self, "_usb_jtag_reset_proxy", usb_jtag_reset_proxy)
        object.__setattr__(self, "_reset_lock", threading.Lock())
        object.__setattr__(self, "_reset_done", False)
        object.__setattr__(self, "_suppress_control_until", 0.0)

    def __getattr__(self, name):
        return getattr(self._wrapped, name)

    def __setattr__(self, name, value):
        if name in {"dtr", "rts"} and self._usb_jtag_reset_proxy:
            self._set_usb_jtag_control_line(name, value)
            return
        if name in {"dtr", "rts", "break_condition"}:
            with contextlib.suppress(OSError):
                setattr(self._wrapped, name, value)
                self._metrics.note_serial_settings(**{name: getattr(self._wrapped, name)})
            return
        if name in {"baudrate", "bytesize", "parity", "stopbits"}:
            setattr(self._wrapped, name, value)
            self._metrics.note_serial_settings(**{name: getattr(self._wrapped, name)})
            return
        setattr(self._wrapped, name, value)

    def _set_usb_jtag_control_line(self, name: str, value) -> None:
        now = time.monotonic()
        with self._reset_lock:
            if now < self._suppress_control_until:
                return
            if not self._reset_done:
                object.__setattr__(self, "_reset_done", True)
                _apply_esp_usb_jtag_reset_sequence(self._wrapped)
                self._metrics.note_serial_settings(**_serial_settings_from_port(self._wrapped))
                object.__setattr__(
                    self,
                    "_suppress_control_until",
                    time.monotonic() + ESP_USB_JTAG_RESET_SUPPRESS_SECONDS,
                )
                return
        with contextlib.suppress(OSError):
            setattr(self._wrapped, name, value)
            self._metrics.note_serial_settings(**{name: getattr(self._wrapped, name)})


def _apply_esp_usb_jtag_reset_sequence(ser) -> None:
    for step in ESP_USB_JTAG_RESET_SEQUENCE.split("|"):
        if not step:
            continue
        op = step[0]
        value = step[1:]
        if op == "W":
            time.sleep(float(value))
        elif op == "R":
            with contextlib.suppress(OSError):
                ser.rts = bool(int(value))
        elif op == "D":
            with contextlib.suppress(OSError):
                ser.dtr = bool(int(value))


def _serial_settings_from_port(ser) -> dict:
    settings = {}
    for name in ("baudrate", "bytesize", "parity", "stopbits", "dtr", "rts", "break_condition"):
        with contextlib.suppress(Exception):
            settings[name] = getattr(ser, name)
    return settings


class _SafePortManager(_PortManagerBase):
    def check_modem_lines(self, force_notification=False):
        with contextlib.suppress(OSError):
            return super().check_modem_lines(force_notification=force_notification)
        return None


def _configure_rfc2217_socket(conn: socket.socket) -> None:
    with contextlib.suppress(OSError):
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with contextlib.suppress(OSError):
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, RFC2217_SOCKET_BUFFER_SIZE)
    with contextlib.suppress(OSError):
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, RFC2217_SOCKET_BUFFER_SIZE)


def _recv_nonblocking(conn: socket.socket) -> bytes | None:
    try:
        return conn.recv(1024)
    except BlockingIOError:
        return None
    except OSError:
        return b""


def _require_pyserial() -> None:
    if serial is None:
        log.die("pyserial is required locally for RFC2217 serial mapping")


def _resolve_esp_reset_strategy(
    local_serial: str,
    requested_strategy: str,
) -> tuple[str, str]:
    if requested_strategy == "usb-jtag":
        return "usb-jtag-proxy", "forced by --esp-reset-strategy usb-jtag"
    if requested_strategy == "default":
        return "default", "forced by --esp-reset-strategy default"
    if requested_strategy == "remote-cfg":
        return "remote-cfg", "forced by --esp-reset-strategy remote-cfg"

    port_info = _find_local_serial_port_info(local_serial)
    if port_info is None:
        return "default", "auto did not find USB metadata for " + local_serial
    if _is_esp_usb_jtag_port(port_info):
        return "usb-jtag-proxy", "auto-detected " + _serial_port_description(port_info)
    return "default", "auto detected " + _serial_port_description(port_info)


def _reset_proxy_policy(esp_reset_strategy: str, rfc2217_only: bool) -> str:
    if esp_reset_strategy != "usb-jtag-proxy":
        return "off"
    if rfc2217_only:
        return "always"
    return "preempted"


def _display_esp_reset_strategy(esp_reset_strategy: str) -> str:
    if esp_reset_strategy == "usb-jtag-proxy":
        return "usb-jtag proxy"
    if esp_reset_strategy == "remote-cfg":
        return "remote cfg"
    return "default"


def _find_local_serial_port_info(local_serial: str):
    try:
        from serial.tools import list_ports
    except ImportError:
        return None

    candidates = _serial_device_candidates(local_serial)
    real_candidates = {os.path.realpath(candidate) for candidate in candidates}
    for port_info in list_ports.comports(include_links=True):
        device = getattr(port_info, "device", "")
        if device in candidates or os.path.realpath(device) in real_candidates:
            return port_info
    return None


def _serial_device_candidates(local_serial: str) -> set[str]:
    candidates = {local_serial}
    tty_prefix = "/dev/tty."
    cu_prefix = "/dev/cu."
    if local_serial.startswith(tty_prefix):
        candidates.add(cu_prefix + local_serial[len(tty_prefix):])
    elif local_serial.startswith(cu_prefix):
        candidates.add(tty_prefix + local_serial[len(cu_prefix):])
    return candidates


def _is_esp_usb_jtag_port(port_info) -> bool:
    vid = getattr(port_info, "vid", None)
    pid = getattr(port_info, "pid", None)
    if vid != 0x303A:
        return False
    if pid == 0x1001:
        return True
    text = " ".join(
        str(value or "")
        for value in (
            getattr(port_info, "manufacturer", ""),
            getattr(port_info, "product", ""),
            getattr(port_info, "description", ""),
            getattr(port_info, "hwid", ""),
        )
    ).lower()
    return "jtag" in text and ("serial" in text or "usb" in text)


def _serial_port_description(port_info) -> str:
    vid = getattr(port_info, "vid", None)
    pid = getattr(port_info, "pid", None)
    product = getattr(port_info, "product", None) or getattr(port_info, "description", None)
    parts = [getattr(port_info, "device", "") or "serial port"]
    if vid is not None and pid is not None:
        parts.append(f"VID:PID={vid:04x}:{pid:04x}")
    if product:
        parts.append(str(product))
    return " ".join(parts)


def _validate_device_path(path: str, label: str) -> PurePosixPath:
    value = PurePosixPath(path)
    if not value.is_absolute() or value.parts[:2] != ("/", "dev"):
        log.die(f"{label} path must be an absolute /dev path: {path}")
    if ".." in value.parts:
        log.die(f"{label} path must not contain '..': {path}")
    if not value.name:
        log.die(f"{label} path is invalid: {path}")
    return value


def _validate_remote_temp_path(path: str, label: str) -> PurePosixPath:
    value = PurePosixPath(path)
    if not value.is_absolute() or value.parts[:2] != ("/", "tmp"):
        log.die(f"{label} path must be an absolute /tmp path: {path}")
    if ".." in value.parts:
        log.die(f"{label} path must not contain '..': {path}")
    if not value.name:
        log.die(f"{label} path is invalid: {path}")
    return value


def _discover_remote_python(host: str) -> str:
    candidates = ("python3", "python")
    test = (
        "import importlib.util, sys; "
        "sys.exit(0 if importlib.util.find_spec('serial') else 1)"
    )
    for candidate in candidates:
        quoted = shlex.quote(candidate)
        cmd = f"command -v {quoted} >/dev/null 2>&1 && {quoted} -c {shlex.quote(test)}"
        if subprocess.call(["ssh", host, cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
            return candidate
    log.die(
        "failed to find remote Python with pyserial; "
        "pass --remote-python /path/to/python"
    )


def _start_reverse_tunnel(
    host: str,
    remote_port: int,
    local_host: str,
    local_port: int,
) -> subprocess.Popen:
    cmd = [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "ExitOnForwardFailure=yes",
        "-N",
        "-R",
        f"127.0.0.1:{remote_port}:{local_host}:{local_port}",
        host,
    ]
    log.inf("starting SSH reverse tunnel: " + shlex.join(cmd))
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def _allocate_remote_tcp_port(host: str, remote_python: str | None) -> int:
    python = remote_python or "python3"
    script = (
        "import random, socket, sys\n"
        f"start = {REMOTE_RFC2217_AUTO_PORT_MIN}\n"
        "ports = list(range(start, 65536))\n"
        "random.shuffle(ports)\n"
        "for port in ports:\n"
        "    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)\n"
        "    try:\n"
        "        sock.bind(('127.0.0.1', port))\n"
        "        print(port)\n"
        "        sys.exit(0)\n"
        "    except OSError:\n"
        "        pass\n"
        "    finally:\n"
        "        sock.close()\n"
        "sys.exit(1)\n"
    )
    remote_cmd = shlex.join([python, "-c", script])
    result = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", host, remote_cmd],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        log.die(
            "failed to allocate remote RFC2217 port >= "
            f"{REMOTE_RFC2217_AUTO_PORT_MIN}: {result.stderr.strip()}"
        )
    try:
        port = int(result.stdout.strip())
    except ValueError:
        log.die("remote RFC2217 port allocator returned invalid output: " + result.stdout.strip())
    if port < REMOTE_RFC2217_AUTO_PORT_MIN:
        log.die(f"remote RFC2217 port allocator returned out-of-range port: {port}")
    log.inf(f"allocated remote RFC2217 port: {port}")
    return port


def _start_remote_bridge(
    host: str,
    remote_python: str,
    remote_port: int,
    remote_serial: str,
    baudrate: int,
    *,
    replace_symlink: bool,
    quiet: bool = False,
) -> subprocess.Popen:
    remote_cmd = shlex.join(
        [
            remote_python,
            "-u",
            "-c",
            REMOTE_BRIDGE_CODE,
            str(remote_port),
            remote_serial,
            str(baudrate),
            "1" if replace_symlink else "0",
        ]
    )
    if not quiet:
        log.inf("starting remote PTY bridge on " + host)
    return subprocess.Popen(
        ["ssh", "-o", "BatchMode=yes", host, remote_cmd],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def _wait_remote_ready(process: subprocess.Popen, timeout: float) -> _RemoteReady:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        _ensure_process_running(process, "remote PTY bridge")
        line = _readline_nonblocking(process.stdout)
        if line is None:
            time.sleep(0.05)
            continue
        line = line.strip()
        if not line:
            continue
        if line.startswith("READY "):
            parts = line.split(maxsplit=2)
            if len(parts) != 3:
                log.die(f"invalid remote bridge ready line: {line}")
            return _RemoteReady(parts[1], parts[2])
        log.inf("remote bridge: " + line)

    stderr = _drain_pipe(process.stderr)
    log.die(f"timed out waiting for remote serial mapping; stderr: {stderr}")


def _readline_nonblocking(pipe) -> str | None:
    if pipe is None:
        return None
    fd = pipe.fileno()
    import select

    ready, _, _ = select.select([fd], [], [], 0)
    if not ready:
        return None
    return pipe.readline()


def _ensure_process_running(process: subprocess.Popen, label: str) -> None:
    if process.poll() is None:
        return
    stderr = _drain_pipe(process.stderr)
    stdout = _drain_pipe(process.stdout)
    details = "; ".join(part for part in (stdout, stderr) if part)
    log.die(f"{label} exited with status {process.returncode}: {details}")


def _stop_processes(processes: list[subprocess.Popen]) -> None:
    for process in reversed(processes):
        if process.poll() is not None:
            continue
        if process.stdin is not None:
            with contextlib.suppress(Exception):
                process.stdin.close()
        try:
            process.wait(timeout=2)
            continue
        except subprocess.TimeoutExpired:
            pass
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def _cleanup_remote_link(
    host: str,
    remote_serial: str,
    *,
    expected_target: str,
) -> None:
    cleanup = (
        "if [ -L {path} ] && [ \"$(readlink {path})\" = {target} ]; "
        "then rm -f {path}; fi"
    ).format(
        path=shlex.quote(remote_serial),
        target=shlex.quote(expected_target),
    )
    subprocess.call(
        ["ssh", "-o", "BatchMode=yes", host, cleanup],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def _write_remote_esptool_cfg(host: str, path: str) -> None:
    content = (
        "[esptool]\n"
        f"custom_reset_sequence = {ESP_USB_JTAG_RESET_SEQUENCE}\n"
    )
    cmd = f"umask 077; cat > {shlex.quote(path)}"
    subprocess.run(
        ["ssh", "-o", "BatchMode=yes", host, cmd],
        check=True,
        input=content,
        text=True,
    )


def _cleanup_remote_file(host: str, path: str) -> None:
    subprocess.call(
        ["ssh", "-o", "BatchMode=yes", host, f"rm -f {shlex.quote(path)}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def _west_flash_example(
    remote_rfc2217_url: str,
    remote_esptool_cfg: PurePosixPath | None,
) -> str:
    command = ["west", "flash", "--esp-device", remote_rfc2217_url]
    rendered = shlex.join(command)
    if remote_esptool_cfg is not None:
        rendered = f"ESPTOOL_CFGFILE={shlex.quote(remote_esptool_cfg.as_posix())} {rendered}"
    return rendered


def _drain_pipe(pipe) -> str:
    if pipe is None:
        return ""
    fd = pipe.fileno()
    import select

    chunks: list[str] = []
    while True:
        ready, _, _ = select.select([fd], [], [], 0)
        if not ready:
            break
        chunk = pipe.readline()
        if not chunk:
            break
        chunks.append(chunk.strip())
    return " ".join(chunks)
