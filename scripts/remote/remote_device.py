# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Combined GDB and serial forwarding for one attached device."""

from __future__ import annotations

import argparse
import contextlib
import queue
import shutil
import signal
import sys
import threading
import time
from types import SimpleNamespace

from remote_gdb import (
    DEFAULT_FLASH_IDLE_TIMEOUT,
    DEFAULT_PYOCD_FREQUENCY,
    DEFAULT_PYOCD_TARGET,
    GdbAdapter,
)
from remote_serial import (
    DEFAULT_BAUDRATE,
    DEFAULT_REMOTE_ESPTOOL_CFG,
    REMOTE_SERIAL_LINK_TIMEOUT,
    SerialAdapter,
)
from terminal_format import _fit_ansi, _format_age


DEFAULT_GDB_PORT = 57065
DEFAULT_SERIAL_PORT = 49221
DASHBOARD_RENDER_INTERVAL_SECONDS = 0.25

C_RESET = "\033[0m"
C_DIM = "\033[38;5;245m"
C_BOLD = "\033[1m"
C_GREEN = "\033[38;5;114m"
C_YELLOW = "\033[38;5;221m"
C_RED = "\033[38;5;203m"
C_BLUE = "\033[38;5;117m"
C_CYAN = "\033[38;5;80m"
C_MAGENTA = "\033[38;5;176m"


def _default_die(message: str) -> None:
    raise SystemExit(message)


def _default_err(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)


def _format_bytes(value: float) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    amount = float(value)
    for unit in units:
        if abs(amount) < 1024 or unit == units[-1]:
            if unit == "B":
                return f"{amount:.0f} {unit}"
            return f"{amount:.1f} {unit}"
        amount /= 1024
    return f"{amount:.1f} GiB"


def _format_rate(value: float) -> str:
    return _format_bytes(value) + "/s"


def _status(value: str, *, active: tuple[str, ...] = ("up", "active")) -> str:
    normalized = value or "starting"
    if normalized in active:
        color = C_GREEN
    elif normalized == "error" or normalized.startswith("failed"):
        color = C_RED
    elif normalized in {"disabled", "idle"}:
        color = C_DIM
    else:
        color = C_YELLOW
    return f"{color}● {normalized}{C_RESET}"


class _DeviceDashboard:
    """Render combined transport telemetry without exposing adapter internals."""

    def __init__(self, args, telemetry: dict, ready: dict, expected: set[str]):
        self.args = args
        self.telemetry = telemetry
        self.ready = ready
        self.expected = expected
        self.enabled = False
        self.started_at = time.monotonic()
        self._last_render_at = 0.0

    def start(self) -> None:
        self.enabled = True
        sys.stdout.write("\033[?1049h\033[H\033[2J\033[?25l\033[?7l")
        sys.stdout.flush()

    def stop(self) -> None:
        if not self.enabled:
            return
        with contextlib.suppress(Exception):
            sys.stdout.write("\033[?7h\033[?25h\033[0m\033[?1049l")
            sys.stdout.flush()
        self.enabled = False

    def render(self, *, force: bool = False) -> None:
        if not self.enabled:
            return
        now = time.monotonic()
        if not force and now - self._last_render_at < DASHBOARD_RENDER_INTERVAL_SECONDS:
            return
        width = max(48, shutil.get_terminal_size((100, 24)).columns)
        lines = self._build_lines(width)
        output = [f"\r\033[2K{line}" for line in lines]
        sys.stdout.write("\033[H\033[J" + "\n".join(output))
        sys.stdout.flush()
        self._last_render_at = now

    def _build_lines(self, width: int) -> list[str]:
        gdb_runtime, gdb_metrics = self.telemetry.get("GDB", (None, None))
        serial_runtime, serial_metrics = self.telemetry.get("serial", (None, None))
        gdb = gdb_metrics.read() if gdb_metrics is not None else {}
        serial = serial_metrics.read() if serial_metrics is not None else {}

        gdb_tunnel = "disabled" if "GDB" not in self.expected else getattr(
            gdb_runtime, "tunnel_state", "starting"
        )
        gdb_client = gdb.get("client_state", "disabled" if "GDB" not in self.expected else "idle")
        gdb_stage = gdb.get("pyocd_stage", "-")
        serial_tunnel = "disabled" if "serial" not in self.expected else getattr(
            serial_runtime, "tunnel_state", "starting"
        )
        serial_client = serial.get(
            "active_connection", "disabled" if "serial" not in self.expected else "idle"
        )
        serial_bridge = getattr(
            serial_runtime,
            "bridge_state",
            "disabled" if "serial" not in self.expected else "starting",
        )

        gdb_endpoint = self.ready.get("GDB", {}).get(
            "endpoint", f"127.0.0.1:{self.args.gdb_port}"
        )
        serial_endpoint = self.ready.get("serial", {}).get(
            "url", f"rfc2217://127.0.0.1:{self.args.serial_port}"
        )
        uptime = max(
            gdb.get("uptime", 0),
            serial.get("uptime", 0),
            time.monotonic() - self.started_at,
        )

        return [
            self._top(width, " Meshbus Remote Device "),
            self._row(
                width,
                f"{C_BLUE}Host{C_RESET}  {self.args.ssh_host}    "
                f"{C_DIM}uptime {_format_age(uptime)}{C_RESET}",
            ),
            self._separator(width, " links "),
            self._row(
                width,
                f"{C_CYAN}GDB{C_RESET}     {_status(gdb_tunnel)}    "
                f"client {_status(gdb_client)}    stage {C_MAGENTA}{gdb_stage}{C_RESET}",
            ),
            self._row(
                width,
                f"        {gdb_endpoint}    {C_DIM}probe {self.args.probe or 'auto'}{C_RESET}",
            ),
            self._row(
                width,
                f"        {C_BLUE}dev→probe{C_RESET} "
                f"{_format_rate(gdb.get('gdb_client_to_pyocd_rate', 0))} / "
                f"{_format_bytes(gdb.get('gdb_client_to_pyocd_bytes', 0))}    "
                f"{C_GREEN}probe→dev{C_RESET} "
                f"{_format_rate(gdb.get('gdb_pyocd_to_client_rate', 0))} / "
                f"{_format_bytes(gdb.get('gdb_pyocd_to_client_bytes', 0))}",
            ),
            self._row(
                width,
                f"{C_MAGENTA}Serial{C_RESET}  {_status(serial_tunnel)}    "
                f"client {_status(serial_client)}    bridge "
                f"{_status(serial_bridge, active=('mapped', 'rfc2217-only'))}",
            ),
            self._row(
                width,
                f"        {serial_endpoint}    "
                f"{C_DIM}{self.args.local_serial or 'disabled'} @ {self.args.baudrate}{C_RESET}",
            ),
            self._row(
                width,
                f"        {C_BLUE}dev→uart{C_RESET} "
                f"{_format_rate(serial.get('remote_to_serial_rate', 0))} / "
                f"{_format_bytes(serial.get('remote_to_serial', 0))}    "
                f"{C_GREEN}uart→dev{C_RESET} "
                f"{_format_rate(serial.get('serial_to_remote_rate', 0))} / "
                f"{_format_bytes(serial.get('serial_to_remote', 0))}",
            ),
            self._separator(width, " activity "),
            self._row(
                width,
                f"GDB sessions {gdb.get('sessions', 0)}  flashes {gdb.get('flashes', 0)}    "
                f"Serial sessions {serial.get('connections', 0)}  "
                f"preemptions {serial.get('preemptions', 0)}",
            ),
            self._row(width, f"{C_DIM}Ctrl-C stop    --verbose raw transport logs{C_RESET}"),
            self._bottom(width),
        ]

    @staticmethod
    def _top(width: int, title: str) -> str:
        title = f"{C_BOLD}{C_CYAN}{title}{C_RESET}"
        plain_title_width = len(" Meshbus Remote Device ")
        fill = "─" * max(0, width - plain_title_width - 3)
        return f"{C_DIM}╭─{C_RESET}{title}{C_DIM}{fill}╮{C_RESET}"

    @staticmethod
    def _separator(width: int, title: str) -> str:
        fill = "─" * max(0, width - len(title) - 3)
        return f"{C_DIM}├─{C_RESET}{title}{C_DIM}{fill}┤{C_RESET}"

    @staticmethod
    def _row(width: int, content: str) -> str:
        return f"{C_DIM}│{C_RESET} {_fit_ansi(content, width - 4)} {C_DIM}│{C_RESET}"

    @staticmethod
    def _bottom(width: int) -> str:
        return f"{C_DIM}╰{'─' * max(0, width - 2)}╯{C_RESET}"


HELP_EPILOG = """\
Examples:
  west remote device build-host.example.com --probe '<probe-id>' --serial '<serial-port>'
  west remote device build-host.example.com --probe '<probe-id>' --serial '<serial-port>' \\
    --gdb-port 57066 --serial-port 49222
  west remote device build-host.example.com --no-serial --probe '<probe-id>'
  west remote device build-host.example.com --no-gdb --serial '<serial-port>'

Run this command on the machine with the USB probe and serial device. It keeps
both reverse tunnels alive until interrupted. On the development host, use the
printed loopback GDB endpoint and RFC2217 URL.
"""


class DeviceCommand:
    """Expose one device's debug probe and serial port as a single interface."""

    def __init__(
        self,
        gdb: GdbAdapter | None = None,
        serial: SerialAdapter | None = None,
        die=None,
        err=None,
        inf=None,
    ):
        self._gdb = gdb or GdbAdapter()
        self._serial = serial or SerialAdapter()
        self._die = die or _default_die
        self._err = err or _default_err
        self._inf = inf or print

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "device",
            help="forward one device's GDB and serial endpoints",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=(
                "Expose one locally attached device's GDB and serial endpoints "
                "to an SSH development host."
            ),
            epilog=HELP_EPILOG,
        )
        parser.add_argument("ssh_host", help="SSH development host that will receive the endpoints")
        parser.add_argument(
            "--probe",
            "--dev-id",
            dest="probe",
            help="pyOCD probe unique ID or substring",
        )
        parser.add_argument("--serial", dest="local_serial", help="local serial device path")
        parser.add_argument(
            "--no-gdb",
            action="store_true",
            help="forward only the serial endpoint",
        )
        parser.add_argument(
            "--no-serial",
            action="store_true",
            help="forward only the GDB endpoint",
        )

        gdb_group = parser.add_argument_group("GDB forwarding")
        gdb_group.add_argument(
            "--target",
            default=DEFAULT_PYOCD_TARGET,
            help=f"pyOCD target name (default: {DEFAULT_PYOCD_TARGET})",
        )
        gdb_group.add_argument(
            "--frequency",
            default=DEFAULT_PYOCD_FREQUENCY,
            help=f"pyOCD SWD clock frequency in Hz (default: {DEFAULT_PYOCD_FREQUENCY})",
        )
        gdb_group.add_argument("--daparg", help="additional pyOCD -da argument")
        gdb_group.add_argument("--pyocd", default="pyocd", help="local pyOCD executable")
        gdb_group.add_argument(
            "--pyocd-opt",
            action="append",
            default=[],
            help="additional pyOCD gdbserver option; may be repeated",
        )
        gdb_group.add_argument(
            "--gdb-port",
            type=int,
            default=DEFAULT_GDB_PORT,
            help=f"local and remote GDB port (default: {DEFAULT_GDB_PORT})",
        )
        gdb_group.add_argument(
            "--flash-idle-timeout",
            type=float,
            default=DEFAULT_FLASH_IDLE_TIMEOUT,
            help=(
                "fail an active flash after this many idle seconds; "
                f"use 0 to disable (default: {DEFAULT_FLASH_IDLE_TIMEOUT:g})"
            ),
        )
        gdb_group.add_argument(
            "--persistent-pyocd",
            action="store_true",
            help="keep pyOCD running instead of starting it for each GDB client",
        )

        serial_group = parser.add_argument_group("serial forwarding")
        serial_group.add_argument(
            "--baudrate",
            type=int,
            default=DEFAULT_BAUDRATE,
            help=f"serial baudrate (default: {DEFAULT_BAUDRATE})",
        )
        serial_group.add_argument(
            "--serial-port",
            type=int,
            default=DEFAULT_SERIAL_PORT,
            help=f"remote RFC2217 port (default: {DEFAULT_SERIAL_PORT})",
        )
        serial_group.add_argument(
            "--serial-pty",
            action="store_true",
            help="also create a remote PTY symlink; direct RFC2217 is the default",
        )
        serial_group.add_argument(
            "--remote-serial",
            help="remote PTY symlink path (default: same as --serial)",
        )
        serial_group.add_argument(
            "--no-replace-symlink",
            action="store_true",
            help="with --serial-pty, fail if the remote symlink already exists",
        )
        serial_group.add_argument(
            "--esp-reset-strategy",
            choices=("auto", "usb-jtag", "default", "remote-cfg"),
            default="auto",
            help="ESP reset handling strategy (default: auto)",
        )
        serial_group.add_argument(
            "--remote-esptool-cfg",
            default=DEFAULT_REMOTE_ESPTOOL_CFG,
            help="temporary remote esptool configuration path",
        )
        serial_group.add_argument(
            "--ready-timeout",
            type=float,
            default=REMOTE_SERIAL_LINK_TIMEOUT,
            help=f"seconds to wait for a remote PTY (default: {REMOTE_SERIAL_LINK_TIMEOUT})",
        )

        parser.add_argument(
            "--remote-python",
            help="remote Python executable used for port allocation and PTY bridging",
        )
        parser.add_argument(
            "--verbose",
            action="store_true",
            help="show internal pyOCD, SSH, and PTY startup logs",
        )
        parser.add_argument(
            "--no-dashboard",
            action="store_true",
            help="disable the live dashboard and print a concise ready summary",
        )
        parser.set_defaults(handler=self.run)
        return parser

    def run(self, args) -> int:
        self._validate(args)
        stop = threading.Event()
        failures: queue.Queue[tuple[str, BaseException]] = queue.Queue()
        ready_events: queue.Queue[tuple[str, dict]] = queue.Queue()
        telemetry_events: queue.Queue[tuple[str, object, object]] = queue.Queue()
        workers: list[threading.Thread] = []
        expected = set()
        ready = {}
        telemetry = {}
        ready_announced = False
        use_dashboard = sys.stdout.isatty() and not args.no_dashboard and not args.verbose
        dashboard = None

        def run_adapter(name, adapter, adapter_args):
            try:
                adapter.run(
                    adapter_args,
                    stop_event=stop,
                    manage_signals=False,
                    quiet=not args.verbose,
                    ready_callback=lambda details: ready_events.put((name, details)),
                    telemetry_callback=lambda runtime, metrics: telemetry_events.put(
                        (name, runtime, metrics)
                    ),
                )
            except BaseException as exc:
                failures.put((name, exc))
                stop.set()

        def report_ready_events():
            nonlocal ready_announced
            while True:
                try:
                    name, details = ready_events.get_nowait()
                except queue.Empty:
                    break
                ready[name] = details
            while True:
                try:
                    name, runtime, metrics = telemetry_events.get_nowait()
                except queue.Empty:
                    break
                telemetry[name] = (runtime, metrics)
            if set(ready) == expected and not ready_announced:
                if not use_dashboard and "GDB" in ready:
                    self._inf(f"GDB     ready  {ready['GDB']['endpoint']}")
                    self._inf(f"Flash          {ready['GDB']['flash_command']}")
                if not use_dashboard and "serial" in ready:
                    self._inf(f"Serial  ready  {ready['serial']['url']}")
                    if ready["serial"].get("pty"):
                        self._inf(f"PTY            {ready['serial']['pty']}")
                if not use_dashboard:
                    self._inf("Device  ready  press Ctrl-C to stop")
                ready_announced = True

        if not args.no_gdb:
            expected.add("GDB")
            workers.append(
                threading.Thread(
                    target=run_adapter,
                    args=("GDB", self._gdb, self._gdb_args(args)),
                    name="remote-device-gdb",
                )
            )
        if not args.no_serial:
            expected.add("serial")
            workers.append(
                threading.Thread(
                    target=run_adapter,
                    args=("serial", self._serial, self._serial_args(args)),
                    name="remote-device-serial",
                )
            )

        old_sigint = signal.signal(signal.SIGINT, lambda _sig, _frame: stop.set())
        old_sigterm = signal.signal(signal.SIGTERM, lambda _sig, _frame: stop.set())
        try:
            if use_dashboard:
                dashboard = _DeviceDashboard(args, telemetry, ready, expected)
                dashboard.start()
                dashboard.render(force=True)
            else:
                self._inf(f"Remote  host   {args.ssh_host}")
            for worker in workers:
                worker.start()
            while any(worker.is_alive() for worker in workers):
                report_ready_events()
                if dashboard is not None:
                    dashboard.render()
                if stop.is_set():
                    break
                time.sleep(0.1)
        finally:
            stop.set()
            for worker in workers:
                worker.join()
            report_ready_events()
            if dashboard is not None:
                with contextlib.suppress(Exception):
                    dashboard.render(force=True)
                dashboard.stop()
            signal.signal(signal.SIGINT, old_sigint)
            signal.signal(signal.SIGTERM, old_sigterm)

        try:
            name, failure = failures.get_nowait()
        except queue.Empty:
            return 0
        if isinstance(failure, SystemExit):
            if use_dashboard:
                self._err(f"{name} forwarding failed; rerun with --verbose for details")
        else:
            self._err(f"{name} forwarding failed: {failure}")
        raise failure

    def _validate(self, args) -> None:
        if args.no_gdb and args.no_serial:
            self._die("--no-gdb and --no-serial cannot be used together")
        if not args.no_serial and not args.local_serial:
            self._die("--serial is required unless --no-serial is used")
        if not args.no_gdb and not args.no_serial and args.gdb_port == args.serial_port:
            self._die("--gdb-port and --serial-port must be different")

    @staticmethod
    def _gdb_args(args):
        return SimpleNamespace(
            ssh_host=args.ssh_host,
            target=args.target,
            frequency=args.frequency,
            probe=args.probe,
            daparg=args.daparg,
            pyocd=args.pyocd,
            pyocd_opt=args.pyocd_opt,
            port=args.gdb_port,
            local_gdb_port=0,
            remote_gdb_port=None,
            remote_python=args.remote_python,
            flash_idle_timeout=args.flash_idle_timeout,
            persistent_pyocd=args.persistent_pyocd,
            no_dashboard=True,
        )

    @staticmethod
    def _serial_args(args):
        return SimpleNamespace(
            ssh_host=args.ssh_host,
            local_serial=args.local_serial,
            remote_serial=args.remote_serial,
            baudrate=args.baudrate,
            remote_python=args.remote_python,
            remote_rfc2217_port=args.serial_port,
            local_rfc2217_port=0,
            no_replace_symlink=args.no_replace_symlink,
            rfc2217_only=not args.serial_pty,
            esp_reset_strategy=args.esp_reset_strategy,
            remote_esptool_cfg=args.remote_esptool_cfg,
            ready_timeout=args.ready_timeout,
            no_dashboard=True,
        )
