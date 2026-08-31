# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""GDB subcommand for west remote."""

from __future__ import annotations

import argparse
import contextlib
import os
import select
import selectors
import shlex
import signal
import shutil
import socket
import subprocess
import sys
import termios
import threading
import time
import tty
from collections import deque

from west import log

from terminal_format import (
    _cell_len,
    _fit_ansi,
    _fit_plain,
    _format_age,
    _strip_ansi,
    _visible_len,
)


DEFAULT_PYOCD_TARGET = "nrf54l"
DEFAULT_PYOCD_FREQUENCY = "4000000"
REMOTE_GDB_AUTO_PORT_MIN = 42900
LOCAL_GDB_READY_TIMEOUT = 20.0
DEFAULT_FLASH_IDLE_TIMEOUT = 180.0
SOCKET_WRITE_IDLE_TIMEOUT = 30.0
DASHBOARD_RENDER_INTERVAL_SECONDS = 0.5
DASHBOARD_LOOP_SLEEP_SECONDS = 0.2

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
  west remote gdb build-host.example.com --target nrf54l --frequency 4000000
  west remote gdb build-host.example.com --port 57065

The command exposes a remote GDB endpoint through an SSH reverse tunnel and
starts local pyOCD only while a remote GDB client is connected.
On the remote host, flash with:

  west flash -r gdb -- --gdb-port <remote-port>
"""


class GdbCommand:
    """Register and run the remote GDB subcommand."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "gdb",
            help="expose a local pyOCD GDB server to the remote host",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Expose a local pyOCD GDB server through an SSH reverse tunnel.",
            epilog=HELP_EPILOG,
        )
        parser.add_argument("ssh_host", help="SSH host that will receive the GDB endpoint")
        parser.add_argument(
            "--target",
            default=DEFAULT_PYOCD_TARGET,
            help=f"pyOCD target name (default: {DEFAULT_PYOCD_TARGET})",
        )
        parser.add_argument(
            "--frequency",
            default=DEFAULT_PYOCD_FREQUENCY,
            help=f"pyOCD SWD clock frequency in Hz (default: {DEFAULT_PYOCD_FREQUENCY})",
        )
        parser.add_argument(
            "--probe",
            "--dev-id",
            dest="probe",
            default=None,
            help="pyOCD probe unique ID or substring",
        )
        parser.add_argument(
            "--daparg",
            default=None,
            help="Additional pyOCD -da argument",
        )
        parser.add_argument(
            "--pyocd",
            default="pyocd",
            help="Local pyOCD executable (default: pyocd)",
        )
        parser.add_argument(
            "--pyocd-opt",
            action="append",
            default=[],
            help="Additional option passed to pyocd gdbserver; may be repeated",
        )
        parser.add_argument(
            "--port",
            type=int,
            default=None,
            help="Use the same TCP port for the local proxy and remote GDB endpoints",
        )
        parser.add_argument(
            "--local-gdb-port",
            type=int,
            default=0,
            help="Local pyOCD/proxy GDB port (default: random, or --port)",
        )
        parser.add_argument(
            "--remote-gdb-port",
            type=int,
            default=None,
            help=(
                "Remote loopback TCP port for the SSH reverse tunnel "
                f"(default: random free remote port >= {REMOTE_GDB_AUTO_PORT_MIN}, or --port)"
            ),
        )
        parser.add_argument(
            "--remote-python",
            default=None,
            help="Remote Python executable used to allocate a free port",
        )
        parser.add_argument(
            "--flash-idle-timeout",
            type=float,
            default=DEFAULT_FLASH_IDLE_TIMEOUT,
            help=(
                "Fail an active erase/program flash session after this many "
                "seconds without pyOCD output or GDB traffic; use 0 to disable "
                f"(default: {DEFAULT_FLASH_IDLE_TIMEOUT:g})"
            ),
        )
        parser.add_argument(
            "--persistent-pyocd",
            action="store_true",
            help=(
                "Start pyOCD immediately and keep it running. This may halt or "
                "otherwise hold the target, so use it only for diagnosis."
            ),
        )
        parser.add_argument(
            "--no-dashboard",
            action="store_true",
            help="Disable the live terminal dashboard even when stdout is a TTY",
        )
        parser.set_defaults(handler=self.run)
        return parser

    def run(self, args) -> int:
        if not args.ssh_host:
            log.die("missing SSH host")

        stop = threading.Event()
        old_sigint = signal.signal(signal.SIGINT, lambda _sig, _frame: stop.set())
        old_sigterm = signal.signal(signal.SIGTERM, lambda _sig, _frame: stop.set())
        processes: list[subprocess.Popen] = []
        runtime = _GdbRuntimeState()
        metrics = _GdbMetrics()
        dashboard = None
        proxy = None
        pyocd = None
        try:
            local_port, remote_port = _resolve_gdb_ports(args)
            runtime.ssh_host = args.ssh_host
            runtime.target = args.target
            runtime.frequency = str(args.frequency)
            runtime.probe = args.probe or "-"
            runtime.local_gdb = f"127.0.0.1:{local_port}"
            runtime.remote_gdb = f"127.0.0.1:{remote_port}"
            runtime.remote_flash_command = shlex.join([
                "west",
                "flash",
                "-r",
                "gdb",
                "--",
                "--gdb-port",
                str(remote_port),
            ])

            use_dashboard = sys.stdout.isatty() and not args.no_dashboard
            if args.persistent_pyocd:
                runtime.pyocd_state = "starting"
                pyocd = _start_pyocd_gdbserver(args, local_port, quiet=use_dashboard)
                processes.append(pyocd)
                _wait_for_process_start(
                    pyocd,
                    "pyOCD GDB server",
                    LOCAL_GDB_READY_TIMEOUT,
                    metrics,
                    echo=not use_dashboard,
                )
                runtime.pyocd_state = "up"
                metrics.note_proxy_event(
                    "pyocd",
                    f"persistent pyOCD GDB server listening on 127.0.0.1:{local_port}",
                )
                local_forward_port = local_port
            else:
                proxy = _LazyGdbProxy(args, local_port, runtime, metrics)
                proxy.start()
                runtime.pyocd_state = "idle"
                local_forward_port = local_port

            tunnel = _start_reverse_tunnel(args.ssh_host, remote_port, "127.0.0.1", local_forward_port)
            processes.append(tunnel)
            _ensure_process_running(tunnel, "ssh reverse tunnel")
            runtime.tunnel_state = "up"

            endpoint = f"127.0.0.1:{remote_port}"
            local_label = "local pyOCD GDB server" if args.persistent_pyocd else "local lazy GDB proxy"
            log.inf(f"{local_label} endpoint: 127.0.0.1:{local_port}")
            log.inf(f"remote GDB endpoint: {endpoint}")
            log.inf("remote flash command: " + runtime.remote_flash_command)
            log.inf("press Ctrl-C to stop the remote GDB tunnel")

            if use_dashboard:
                dashboard = _GdbDashboard(runtime, metrics)
                dashboard.start()
                dashboard.render()

            while not stop.is_set():
                _ensure_process_running(tunnel, "ssh reverse tunnel")
                if proxy is not None:
                    proxy.raise_if_failed()
                if pyocd is not None:
                    _ensure_process_running(pyocd, "pyOCD GDB server")
                    _consume_process_output(pyocd, "pyocd", metrics, echo=dashboard is None)
                    try:
                        _check_flash_idle_timeout(metrics, args.flash_idle_timeout)
                    except TimeoutError as exc:
                        runtime.pyocd_state = "error"
                        metrics.note_proxy_event("error", str(exc))
                        log.die(str(exc))
                runtime.tunnel_state = "up"
                _consume_process_output(tunnel, "ssh", metrics, echo=dashboard is None)
                if dashboard is not None:
                    dashboard.render()
                time.sleep(DASHBOARD_LOOP_SLEEP_SECONDS if dashboard is not None else 0.2)
        finally:
            if dashboard is not None:
                dashboard.stop()
            if proxy is not None:
                proxy.stop()
            signal.signal(signal.SIGINT, old_sigint)
            signal.signal(signal.SIGTERM, old_sigterm)
            _stop_processes(processes)
        return 0


class _GdbRuntimeState:
    def __init__(self):
        self.ssh_host = ""
        self.target = ""
        self.frequency = ""
        self.probe = ""
        self.local_gdb = ""
        self.remote_gdb = ""
        self.remote_flash_command = ""
        self.pyocd_state = "starting"
        self.tunnel_state = "starting"


class _GdbMetrics:
    def __init__(self):
        self._lock = threading.Lock()
        self.started_at = time.monotonic()
        self.sessions = 0
        self.pyocd_starts = 0
        self.flashes = 0
        self.gdb_client_to_pyocd_bytes = 0
        self.gdb_pyocd_to_client_bytes = 0
        self.client_state = "idle"
        self.pyocd_stage = "idle"
        self.last_activity_at = self.started_at
        self.last_gdb_io_at = 0.0
        self.last_client_at = 0.0
        self.last_disconnect_at = 0.0
        self.last_flash_at = 0.0
        self.last_pyocd_line = ""
        self.last_ssh_line = ""
        self.events = deque(maxlen=6)

    def note_restart(self, message: str) -> None:
        now = time.monotonic()
        with self._lock:
            self.last_activity_at = now
            self.pyocd_starts += 1
            self.pyocd_stage = "restarting"
            self.events.append((now, "restart", _clean_event(message)))

    def note_proxy_event(self, label: str, message: str) -> None:
        now = time.monotonic()
        with self._lock:
            self.last_activity_at = now
            clean = _clean_event(message)
            if label == "client":
                self.pyocd_starts += 1
                self.client_state = "active"
                self.pyocd_stage = "starting"
                self.last_client_at = now
            elif label == "closed":
                self.client_state = "idle"
                self.pyocd_stage = "idle"
                self.last_disconnect_at = now
            elif label == "error":
                self.pyocd_stage = "error"
            self.events.append((now, label, clean))

    def note_process_line(self, label: str, line: str) -> None:
        clean = _clean_event(line)
        if not clean:
            return
        now = time.monotonic()
        with self._lock:
            self.last_activity_at = now
            if label == "pyocd":
                self.last_pyocd_line = clean
                self._apply_pyocd_line(now, clean)
            elif label == "ssh":
                self.last_ssh_line = clean
            self.events.append((now, label, clean))

    def _apply_pyocd_line(self, now: float, line: str) -> None:
        lower = line.lower()
        if "gdb server listening" in lower:
            self.pyocd_stage = "listening"
        elif "client" in lower and "disconnected" in lower:
            self.client_state = "idle"
            self.pyocd_stage = "listening"
            self.last_disconnect_at = now
        elif "client" in lower and "connected" in lower:
            self.sessions += 1
            self.client_state = "active"
            self.pyocd_stage = "client"
            self.last_client_at = now
        elif "erasing" in lower:
            self.pyocd_stage = "erasing"
        elif "programming" in lower:
            self.pyocd_stage = "programming"
        elif "erased" in lower and "programmed" in lower:
            self.flashes += 1
            self.pyocd_stage = "flashed"
            self.last_flash_at = now

    def note_gdb_io(self, direction: str, byte_count: int) -> None:
        if byte_count <= 0:
            return
        now = time.monotonic()
        with self._lock:
            self.last_activity_at = now
            self.last_gdb_io_at = now
            if direction == "client->pyocd":
                self.gdb_client_to_pyocd_bytes += byte_count
            elif direction == "pyocd->client":
                self.gdb_pyocd_to_client_bytes += byte_count

    def read(self) -> dict:
        now = time.monotonic()
        with self._lock:
            return {
                "uptime": now - self.started_at,
                "sessions": self.sessions,
                "pyocd_starts": self.pyocd_starts,
                "flashes": self.flashes,
                "gdb_client_to_pyocd_bytes": self.gdb_client_to_pyocd_bytes,
                "gdb_pyocd_to_client_bytes": self.gdb_pyocd_to_client_bytes,
                "client_state": self.client_state,
                "pyocd_stage": self.pyocd_stage,
                "last_activity_age": now - self.last_activity_at,
                "last_gdb_io_age": None if self.last_gdb_io_at == 0.0 else now - self.last_gdb_io_at,
                "last_client_age": None if self.last_client_at == 0.0 else now - self.last_client_at,
                "last_disconnect_age": None if self.last_disconnect_at == 0.0 else now - self.last_disconnect_at,
                "last_flash_age": None if self.last_flash_at == 0.0 else now - self.last_flash_at,
                "last_pyocd_line": self.last_pyocd_line,
                "last_ssh_line": self.last_ssh_line,
                "events": list(self.events),
            }


class _LazyGdbProxy:
    def __init__(
        self,
        args,
        listen_port: int,
        runtime: _GdbRuntimeState,
        metrics: _GdbMetrics,
    ):
        self.args = args
        self.listen_port = listen_port
        self.runtime = runtime
        self.metrics = metrics
        self.echo = not (sys.stdout.isatty() and not args.no_dashboard)
        self._server: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._failed: BaseException | None = None
        self._active_lock = threading.Lock()
        self._active = False
        self._processes: list[subprocess.Popen] = []

    def start(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", self.listen_port))
        server.listen(1)
        server.settimeout(0.2)
        self._server = server
        self._thread = threading.Thread(target=self._accept_loop, name="remote-gdb-proxy", daemon=True)
        self._thread.start()
        self.metrics.note_proxy_event("proxy", f"lazy proxy listening on 127.0.0.1:{self.listen_port}")

    def stop(self) -> None:
        self._stop.set()
        if self._server is not None:
            with contextlib.suppress(OSError):
                self._server.close()
        self._terminate_active_processes()
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=1)

    def raise_if_failed(self) -> None:
        if self._failed is not None:
            raise self._failed

    def _accept_loop(self) -> None:
        assert self._server is not None
        while not self._stop.is_set():
            try:
                client, address = self._server.accept()
            except socket.timeout:
                continue
            except OSError as exc:
                if not self._stop.is_set():
                    self._failed = exc
                return

            if not self._try_claim_session():
                self.metrics.note_proxy_event("busy", f"rejected concurrent GDB client from {address[0]}:{address[1]}")
                with contextlib.suppress(OSError):
                    client.close()
                continue

            thread = threading.Thread(
                target=self._serve_client,
                args=(client, address),
                name="remote-gdb-session",
                daemon=True,
            )
            thread.start()

    def _try_claim_session(self) -> bool:
        with self._active_lock:
            if self._active:
                return False
            self._active = True
            return True

    def _release_session(self) -> None:
        with self._active_lock:
            self._active = False

    def _serve_client(self, client: socket.socket, address) -> None:
        pyocd: subprocess.Popen | None = None
        backend: socket.socket | None = None
        try:
            peer = f"{address[0]}:{address[1]}"
            self.runtime.pyocd_state = "starting"
            self.metrics.note_proxy_event("client", f"remote GDB client connected from {peer}; starting pyOCD")

            backend_port = _allocate_local_tcp_port()
            pyocd = _start_pyocd_gdbserver(self.args, backend_port, quiet=not self.echo)
            self._processes.append(pyocd)
            _wait_for_process_start(
                pyocd,
                "pyOCD GDB server",
                LOCAL_GDB_READY_TIMEOUT,
                self.metrics,
                echo=self.echo,
            )
            backend = _connect_with_retry("127.0.0.1", backend_port, pyocd, LOCAL_GDB_READY_TIMEOUT)
            self.runtime.pyocd_state = "up"
            self.metrics.note_proxy_event("proxy", f"bridging remote client to pyOCD on 127.0.0.1:{backend_port}")
            _bridge_gdb_session(
                client,
                backend,
                pyocd,
                self.metrics,
                self.echo,
                self._stop,
                self.args.flash_idle_timeout,
            )
            _consume_process_output(pyocd, "pyocd", self.metrics, echo=self.echo)
        except BaseException as exc:
            self.runtime.pyocd_state = "error"
            self.metrics.note_proxy_event("error", str(exc))
            self._failed = exc
        finally:
            _close_socket(client)
            if backend is not None:
                _close_socket(backend)
            if pyocd is not None:
                _terminate_process(pyocd)
                with contextlib.suppress(ValueError):
                    self._processes.remove(pyocd)
            if not self._stop.is_set():
                self.runtime.pyocd_state = "idle"
                self.metrics.note_proxy_event("closed", "remote GDB client disconnected; pyOCD stopped")
            self._release_session()

    def _terminate_active_processes(self) -> None:
        for process in list(self._processes):
            _terminate_process(process)
        self._processes.clear()


class _GdbDashboard:
    def __init__(self, runtime: _GdbRuntimeState, metrics: _GdbMetrics):
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
        width = max(50, size.columns)
        height = max(16, size.lines)
        snap = self.metrics.read()
        now = time.monotonic()
        if (
            not self.needs_render
            and self._last_render_at > 0.0
            and now - self._last_render_at < DASHBOARD_RENDER_INTERVAL_SECONDS
        ):
            return
        lines = self._build_lines(width, height, snap)
        output = [f"\r\033[2K{line}" for line in lines]
        sys.stdout.write("\033[H\033[J" + "\n".join(output))
        sys.stdout.flush()
        self.needs_render = False
        self._last_render_at = now

    def _build_lines(self, width: int, height: int, snap: dict) -> list[str]:
        inner = width - 4
        event_rows = max(1, height - 18)
        lines = [
            self._top(width, " 󰢹 Meshbus Remote GDB "),
            self._row(inner, [
                (C_BLUE, "󰒋 host "),
                ("", self.runtime.ssh_host + "   "),
                (C_BLUE, "󰌘 tunnel "),
                self._status_segment(self.runtime.tunnel_state, good={"up"}),
            ]),
            self._row(inner, [
                (C_MAGENTA, "󰔶 target "),
                ("", self.runtime.target + "   "),
                (C_DIM, f"{self.runtime.frequency} Hz   "),
                (C_MAGENTA, "󰘲 probe "),
                ("", self.runtime.probe),
            ]),
            self._row(inner, [
                (C_CYAN, "󰗧 local "),
                ("", self.runtime.local_gdb + "   "),
                (C_CYAN, "󰖟 remote "),
                ("", self.runtime.remote_gdb),
            ]),
            self._row(inner, [
                (C_YELLOW, "󰐊 flash "),
                ("", self.runtime.remote_flash_command),
            ]),
            self._blank_row(inner),
            self._section(inner, "status"),
            self._blank_row(inner),
            self._row(inner, [
                (C_CYAN, "󰢹 pyocd "),
                self._status_segment(self.runtime.pyocd_state, good={"up"}),
                (C_DIM, f"   stage {snap['pyocd_stage']}   "),
                (C_MAGENTA, "󰌘 client "),
                self._status_segment(snap["client_state"], good={"active"}),
            ]),
            self._row(inner, [
                (C_BLUE, "󰩠 sessions "),
                ("", f"{snap['sessions']}   "),
                (C_GREEN, "󰐊 flashes "),
                ("", f"{snap['flashes']}   "),
                (C_YELLOW, "󰜉 pyocd starts "),
                ("", f"{snap['pyocd_starts']}"),
            ]),
            self._row(inner, [
                (C_BLUE, "󰅐 last client "),
                (C_DIM, _format_age(snap["last_client_age"]) + "   "),
                (C_GREEN, "󰅒 last flash "),
                (C_DIM, _format_age(snap["last_flash_age"]) + "   "),
                (C_BLUE, "󰅒 last close "),
                (C_DIM, _format_age(snap["last_disconnect_age"])),
            ]),
            self._row(inner, [
                (C_MAGENTA, "󰈙 gdb I/O "),
                (C_DIM, _format_age(snap["last_gdb_io_age"]) + "   "),
                (C_BLUE, "to pyocd "),
                ("", _format_bytes(snap["gdb_client_to_pyocd_bytes"]) + "   "),
                (C_GREEN, "to client "),
                ("", _format_bytes(snap["gdb_pyocd_to_client_bytes"])),
            ]),
            self._blank_row(inner),
            self._section(inner, "events"),
            self._blank_row(inner),
        ]
        events = snap["events"][-event_rows:]
        if not events:
            lines.append(self._event_row(inner, "idle", "waiting for pyOCD activity"))
        else:
            for _ts, label, text in events:
                lines.append(self._event_row(inner, label, text))
        while len(lines) < height - 3:
            lines.append(self._blank_row(inner))
        lines.extend([
            self._section(inner, "controls"),
            self._row(inner, [
                (C_GREEN, "mode monitor   "),
                (C_DIM, "Ctrl-C stop  pyOCD starts only while a GDB client is connected"),
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
    ) -> tuple[str, str]:
        good = good or set()
        if value in good:
            return C_GREEN, f"● {value}"
        if value == "idle":
            return C_GRAY, f"● {value}"
        if value in {"starting", "restarting", "erasing", "programming"}:
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

    def _event_row(self, inner: int, label: str, text: str) -> str:
        color = C_BLUE if label == "ssh" else C_CYAN
        if label == "restart":
            color = C_YELLOW
        if label == "idle":
            color = C_DIM
        return self._row(inner, [(color, f"{label:<7} "), ("", text)])

    def _blank_row(self, inner: int) -> str:
        return f"{C_PANEL}│{C_RESET} {' ' * inner} {C_PANEL}│{C_RESET}"


def _clean_event(text: str) -> str:
    text = _strip_ansi(text)
    return " ".join(text.split())


def _format_bytes(value: int) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    size = float(value)
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            if unit == "B":
                return f"{int(size)}{unit}"
            return f"{size:.1f}{unit}"
        size /= 1024.0
    return f"{value}B"


def _start_pyocd_gdbserver(args, port: int, *, quiet: bool = False) -> subprocess.Popen:
    cmd = [
        args.pyocd,
        "gdbserver",
        "-p",
        str(port),
        "-t",
        args.target,
        "-f",
        str(args.frequency),
    ]
    if args.probe:
        cmd.extend(["-u", args.probe])
    if args.daparg:
        cmd.extend(["-da", args.daparg])
    cmd.extend(args.pyocd_opt)
    if not quiet:
        log.inf("starting local pyOCD GDB server: " + shlex.join(cmd))
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    _make_process_pipes_nonblocking(process)
    return process


def _connect_with_retry(
    host: str,
    port: int,
    process: subprocess.Popen,
    timeout: float,
) -> socket.socket:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        _ensure_process_running(process, "pyOCD GDB server")
        try:
            sock = socket.create_connection((host, port), timeout=0.5)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    details = _process_output_details(process)
    message = f"timed out connecting to pyOCD GDB server at {host}:{port}"
    if last_error is not None:
        message += f": {last_error}"
    if details:
        message += f": {details}"
    raise RuntimeError(message)


def _bridge_gdb_session(
    client: socket.socket,
    backend: socket.socket,
    process: subprocess.Popen,
    metrics: _GdbMetrics,
    echo: bool,
    stop: threading.Event,
    flash_idle_timeout: float,
) -> None:
    _set_tcp_nodelay(client)
    _set_tcp_nodelay(backend)
    client.setblocking(False)
    backend.setblocking(False)
    relay = selectors.DefaultSelector()
    relay.register(client, selectors.EVENT_READ, (backend, "client->pyocd"))
    relay.register(backend, selectors.EVENT_READ, (client, "pyocd->client"))
    try:
        while not stop.is_set() and relay.get_map():
            _consume_process_output(process, "pyocd", metrics, echo=echo)
            if process.poll() is not None:
                break
            _check_flash_idle_timeout(metrics, flash_idle_timeout)
            for key, _events in relay.select(timeout=0.02):
                src = key.fileobj
                dst, direction = key.data
                try:
                    data = src.recv(65536)
                except BlockingIOError:
                    continue
                except OSError as exc:
                    metrics.note_proxy_event("proxy", f"{direction} read closed: {exc}")
                    _unregister_socket(relay, src)
                    _shutdown_socket_write(dst)
                    continue
                if not data:
                    metrics.note_proxy_event("proxy", f"{direction} EOF")
                    _unregister_socket(relay, src)
                    _shutdown_socket_write(dst)
                    continue
                try:
                    sent = _send_all_nonblocking(dst, data, stop)
                    metrics.note_gdb_io(direction, sent)
                except TimeoutError as exc:
                    metrics.note_proxy_event("error", f"{direction} write timeout: {exc}")
                    raise
                except OSError as exc:
                    metrics.note_proxy_event("proxy", f"{direction} write closed: {exc}")
                    _unregister_socket(relay, src)
                    _shutdown_socket_write(src)
    finally:
        relay.close()
    _consume_process_output(process, "pyocd", metrics, echo=echo)
    if process.poll() is not None and process.returncode not in (0, None):
        details = _process_output_details(process)
        raise RuntimeError(f"pyOCD GDB server exited with status {process.returncode}: {details}")


def _check_flash_idle_timeout(metrics: _GdbMetrics, timeout: float) -> None:
    if timeout <= 0.0:
        return
    snap = metrics.read()
    stage = snap["pyocd_stage"]
    if stage not in {"erasing", "programming"}:
        return
    age = snap["last_activity_age"]
    if age <= timeout:
        return
    detail = f"pyOCD {stage} stage idle for {age:.1f}s"
    last_line = snap["last_pyocd_line"]
    if last_line:
        detail += f"; last pyOCD line: {last_line}"
    raise TimeoutError(detail)


def _send_all_nonblocking(sock: socket.socket, data: bytes, stop: threading.Event) -> int:
    view = memoryview(data)
    total = 0
    deadline = time.monotonic() + SOCKET_WRITE_IDLE_TIMEOUT
    while view:
        if stop.is_set():
            raise InterruptedError("GDB proxy stopped while forwarding data")
        _, writable, _ = select.select([], [sock], [], 0.5)
        if not writable:
            if time.monotonic() > deadline:
                raise TimeoutError(
                    f"socket was not writable for {SOCKET_WRITE_IDLE_TIMEOUT:g}s"
                )
            continue
        try:
            sent = sock.send(view)
        except BlockingIOError:
            if time.monotonic() > deadline:
                raise TimeoutError(
                    f"socket write made no progress for {SOCKET_WRITE_IDLE_TIMEOUT:g}s"
                )
            continue
        if sent == 0:
            raise OSError("socket closed while forwarding GDB data")
        total += sent
        view = view[sent:]
        deadline = time.monotonic() + SOCKET_WRITE_IDLE_TIMEOUT
    return total


def _unregister_socket(relay: selectors.BaseSelector, sock: socket.socket) -> None:
    with contextlib.suppress(KeyError, ValueError, OSError):
        relay.unregister(sock)


def _shutdown_socket_write(sock: socket.socket) -> None:
    with contextlib.suppress(OSError):
        sock.shutdown(socket.SHUT_WR)


def _set_tcp_nodelay(sock: socket.socket) -> None:
    with contextlib.suppress(OSError):
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)


def _close_socket(sock: socket.socket) -> None:
    with contextlib.suppress(OSError):
        sock.shutdown(socket.SHUT_RDWR)
    with contextlib.suppress(OSError):
        sock.close()


def _terminate_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


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
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    _make_process_pipes_nonblocking(process)
    return process


def _allocate_local_tcp_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _resolve_gdb_ports(args) -> tuple[int, int]:
    if args.port is not None:
        if args.local_gdb_port not in (0, args.port):
            log.die("--port conflicts with --local-gdb-port")
        if args.remote_gdb_port not in (None, args.port):
            log.die("--port conflicts with --remote-gdb-port")
        return args.port, args.port

    local_port = args.local_gdb_port or _allocate_local_tcp_port()
    remote_port = args.remote_gdb_port or _allocate_remote_tcp_port(
        args.ssh_host,
        args.remote_python,
    )
    return local_port, remote_port


def _allocate_remote_tcp_port(host: str, remote_python: str | None) -> int:
    python = remote_python or "python3"
    script = (
        "import random, socket, sys\n"
        f"start = {REMOTE_GDB_AUTO_PORT_MIN}\n"
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
            "failed to allocate remote GDB port >= "
            f"{REMOTE_GDB_AUTO_PORT_MIN}: {result.stderr.strip()}"
        )
    try:
        port = int(result.stdout.strip())
    except ValueError:
        log.die("remote GDB port allocator returned invalid output: " + result.stdout.strip())
    if port < REMOTE_GDB_AUTO_PORT_MIN:
        log.die(f"remote GDB port allocator returned out-of-range port: {port}")
    log.inf(f"allocated remote GDB port: {port}")
    return port


def _wait_for_process_start(
    process: subprocess.Popen,
    label: str,
    timeout: float,
    metrics: "_GdbMetrics | None" = None,
    *,
    echo: bool = True,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        _ensure_process_running(process, label)
        _consume_process_output(process, "pyocd", metrics, echo=echo)
        if metrics is None or metrics.read()["pyocd_stage"] in {"listening", "client"}:
            return
        time.sleep(0.1)
        _ensure_process_running(process, label)
    details = _drain_pipe(process.stderr) or _drain_pipe(process.stdout)
    log.die(f"timed out waiting for {label}: {details}")


def _ensure_process_running(process: subprocess.Popen, label: str) -> None:
    if process.poll() is None:
        return
    details = _process_output_details(process)
    log.die(f"{label} exited with status {process.returncode}: {details}")


def _restart_pyocd_gdbserver_if_needed(
    process: subprocess.Popen,
    args,
    local_port: int,
    processes: list[subprocess.Popen],
    metrics: "_GdbMetrics",
    echo: bool,
) -> subprocess.Popen:
    if process.poll() is None:
        return process

    details = _process_output_details(process)
    if process.returncode != 0:
        log.die(f"pyOCD GDB server exited with status {process.returncode}: {details}")

    _note_details_lines(metrics, details)
    message = "pyOCD GDB server exited after client disconnect"
    if details:
        message += ": " + details
    if echo:
        log.inf(message)
        log.inf("restarting local pyOCD GDB server on the same port")
    metrics.note_restart(message)
    restarted = _start_pyocd_gdbserver(args, local_port, quiet=not echo)
    processes.append(restarted)
    _wait_for_process_start(
        restarted,
        "pyOCD GDB server",
        LOCAL_GDB_READY_TIMEOUT,
        metrics,
        echo=echo,
    )
    return restarted


def _consume_process_output(
    process: subprocess.Popen,
    label: str,
    metrics: "_GdbMetrics | None",
    *,
    echo: bool,
) -> None:
    for line in _drain_pipe(process.stdout).replace("\r", "\n").splitlines():
        if metrics is not None:
            metrics.note_process_line(label, line)
        if echo:
            log.inf(f"{label}: {line}")
    for line in _drain_pipe(process.stderr).replace("\r", "\n").splitlines():
        if metrics is not None:
            metrics.note_process_line(label, line)
        if echo:
            log.wrn(f"{label}: {line}")


def _process_output_details(process: subprocess.Popen) -> str:
    stdout = _drain_pipe(process.stdout)
    stderr = _drain_pipe(process.stderr)
    return "; ".join(part for part in (stdout, stderr) if part)


def _note_details_lines(metrics: _GdbMetrics, details: str) -> None:
    for part in details.split("; "):
        for line in part.splitlines():
            metrics.note_process_line("pyocd", line)


def _drain_pipe(pipe) -> str:
    if pipe is None:
        return ""
    fd = pipe.fileno()

    chunks: list[str] = []
    while True:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            break
        except OSError:
            break
        if not chunk:
            break
        chunks.append(chunk.decode(errors="replace"))
    return "".join(chunks)


def _make_process_pipes_nonblocking(process: subprocess.Popen) -> None:
    for pipe in (process.stdout, process.stderr):
        if pipe is None:
            continue
        with contextlib.suppress(OSError, AttributeError):
            os.set_blocking(pipe.fileno(), False)


def _stop_processes(processes: list[subprocess.Popen]) -> None:
    for process in reversed(processes):
        _terminate_process(process)
