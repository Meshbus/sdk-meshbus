#!/usr/bin/env python3
"""Serial console helper for Zephyr firmware workflows."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - exercised on hosts without pyserial
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise SystemExit(127) from exc


DEFAULT_BAUDRATE = 115200
DEFAULT_WAIT_PATTERNS = [
    r"uart:~\$",
    r"shell>",
    r"Booting Zephyr",
    r"\*\*\* Booting Zephyr OS",
]
DEFAULT_BAD_PATTERNS = [
    r"\bassert(?:ion)?\b",
    r"\bkernel (?:oops|panic)\b",
    r"\boops\b",
    r"\bpanic\b",
    r"\bhard ?fault\b",
    r"\bbus fault\b",
    r"\busage fault\b",
    r"\bmemmanage fault\b",
    r"\bfatal\b",
    r"\bfail(?:ed|ure)?\b",
    r"(?:^|\s)<err>",
    r"\berror\b",
]


def compile_patterns(patterns: Iterable[str], ignore_case: bool = True) -> list[re.Pattern[str]]:
    flags = re.IGNORECASE if ignore_case else 0
    return [re.compile(pattern, flags) for pattern in patterns]


def open_transcript(path: str | None):
    if not path:
        return None
    transcript = Path(path).expanduser()
    transcript.parent.mkdir(parents=True, exist_ok=True)
    return transcript.open("a", encoding="utf-8")


def write_transcript_header(args: argparse.Namespace, transcript, wait_patterns: list[str]) -> None:
    if not transcript or args.no_transcript_header:
        return

    command = getattr(args, "command", None)
    if isinstance(command, list):
        command_text = json.dumps(command, ensure_ascii=False)
    else:
        command_text = "" if command is None else str(command)
    if not command_text and getattr(args, "script", None):
        command_text = f"session script: {args.script}"

    header = {
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
        "subcommand": args.command_name,
        "port": args.port,
        "baudrate": args.baudrate,
        "reset": getattr(args, "reset", "none"),
        "reset_command": getattr(args, "reset_command", None),
        "wait_patterns": wait_patterns,
        "timeout": args.timeout,
        "command": command_text,
    }
    transcript.write("# serial-use transcript metadata begin\n")
    for key, value in header.items():
        transcript.write(f"# serial-use {key}: {json.dumps(value, ensure_ascii=False)}\n")
    transcript.write("# serial-use transcript metadata end\n")
    transcript.flush()


def write_text(text: str, transcript) -> None:
    print(text, end="", flush=True)
    if transcript:
        transcript.write(text)
        transcript.flush()


def list_command(args: argparse.Namespace) -> int:
    ports = []
    for port in list_ports.comports():
        ports.append(
            {
                "device": port.device,
                "description": port.description,
                "hwid": port.hwid,
                "manufacturer": port.manufacturer,
                "product": port.product,
                "serial_number": port.serial_number,
            }
        )

    if args.json:
        print(json.dumps(ports, indent=2, sort_keys=True))
        return 0

    if not ports:
        print("No serial ports found.")
        return 1

    width = max(len(item["device"]) for item in ports)
    for item in ports:
        detail = item["description"] or item["hwid"] or ""
        print(f"{item['device']:<{width}}  {detail}")
    return 0


def find_port_holders(port: str) -> str:
    try:
        result = subprocess.run(
            ["lsof", port],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except FileNotFoundError:
        return ""
    lines = result.stdout.splitlines()
    if len(lines) <= 1:
        return ""
    current_pid = str(os.getpid())
    header, *rows = lines
    other_rows = [row for row in rows if len(row.split()) < 2 or row.split()[1] != current_pid]
    if not other_rows:
        return ""
    return "\n".join([header, *other_rows])


def check_shared_port(args: argparse.Namespace) -> int:
    if args.allow_shared_port:
        return 0
    holders = find_port_holders(args.port)
    if not holders:
        return 0
    print(
        f"{args.port} is already open by another process. Close it first or rerun with --allow-shared-port.",
        file=sys.stderr,
    )
    print(holders, file=sys.stderr)
    return 4


def apply_reset(ser: serial.Serial, args: argparse.Namespace) -> None:
    if args.reset == "none" and not args.reset_command:
        return

    if args.reset_command:
        subprocess.run(args.reset_command, shell=True, check=True)

    if args.reset in {"dtr", "both"}:
        ser.dtr = False
    if args.reset in {"rts", "both"}:
        ser.rts = True
    if args.reset != "none":
        time.sleep(args.reset_pulse)
    if args.reset in {"dtr", "both"}:
        ser.dtr = True
    if args.reset in {"rts", "both"}:
        ser.rts = False


def line_ending(name: str) -> bytes:
    return {
        "lf": b"\n",
        "crlf": b"\r\n",
        "cr": b"\r",
        "none": b"",
    }[name]


def send_lines(ser: serial.Serial, commands: list[str], ending: bytes) -> None:
    for command in commands:
        ser.write(command.encode("utf-8") + ending)
        ser.flush()


def wait_satisfied(
    matched_wait_indexes: set[int],
    wait_patterns: list[re.Pattern[str]],
    wait_all: bool,
) -> bool:
    if not wait_patterns:
        return False
    if wait_all:
        return len(matched_wait_indexes) == len(wait_patterns)
    return bool(matched_wait_indexes)


def read_until(
    ser: serial.Serial,
    *,
    timeout: float,
    wait_patterns: list[re.Pattern[str]],
    wait_all: bool,
    post_wait_seconds: float,
    bad_patterns: list[re.Pattern[str]],
    ignore_patterns: list[re.Pattern[str]],
    transcript,
) -> tuple[bool, list[str]]:
    deadline = time.monotonic() + timeout if timeout >= 0 else None
    post_wait_deadline: float | None = None
    buffer = ""
    findings: list[str] = []
    matched_wait_indexes: set[int] = set()

    while True:
        now = time.monotonic()
        if post_wait_deadline is not None:
            if now >= post_wait_deadline:
                break
        elif deadline is not None and now >= deadline:
            break

        chunk = ser.read(4096)
        if not chunk:
            continue

        text = chunk.decode("utf-8", errors="replace")
        write_text(text, transcript)
        buffer = (buffer + text)[-65536:]

        for line in text.splitlines():
            if any(pattern.search(line) for pattern in ignore_patterns):
                continue
            for pattern in bad_patterns:
                if pattern.search(line):
                    findings.append(line)
                    break

        for index, pattern in enumerate(wait_patterns):
            if index not in matched_wait_indexes and pattern.search(buffer):
                matched_wait_indexes.add(index)

        if post_wait_deadline is None and wait_satisfied(
            matched_wait_indexes,
            wait_patterns,
            wait_all,
        ):
            if post_wait_seconds > 0:
                post_wait_deadline = time.monotonic() + post_wait_seconds
            else:
                break

    return wait_satisfied(matched_wait_indexes, wait_patterns, wait_all), findings


def serial_line_state(value: str) -> bool:
    return {
        "assert": True,
        "deassert": False,
    }[value]


def recover_lines_command(args: argparse.Namespace) -> int:
    shared_status = check_shared_port(args)
    if shared_status != 0:
        return shared_status

    with serial.Serial(args.port, args.baudrate, timeout=0.1) as ser:
        time.sleep(args.settle)
        try:
            ser.dtr = serial_line_state(args.dtr)
            ser.rts = serial_line_state(args.rts)
        except OSError as exc:
            print(
                f"{args.port} does not support DTR/RTS line control: {exc}",
                file=sys.stderr,
            )
            return 6
        time.sleep(args.hold)

    print(f"Recovered serial lines on {args.port}: DTR={args.dtr}, RTS={args.rts}")
    return 0


def report_bad_findings(findings: list[str]) -> None:
    print("\nBad log patterns detected:", file=sys.stderr)
    for line in findings:
        print(line, file=sys.stderr)


def load_session_steps(path: str) -> list[dict[str, Any]]:
    text = Path(path).expanduser().read_text(encoding="utf-8")
    data = json.loads(text)
    if not isinstance(data, list):
        raise ValueError("session script must be a JSON array")

    steps: list[dict[str, Any]] = []
    for index, item in enumerate(data, start=1):
        if isinstance(item, str):
            steps.append({"command": item})
            continue
        if not isinstance(item, dict):
            raise ValueError(f"session step {index} must be a string or object")
        steps.append(item)
    return steps


def list_field(value: Any, *, field_name: str) -> list[str]:
    if value is None:
        return []
    if isinstance(value, str):
        return [value]
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return list(value)
    raise ValueError(f"{field_name} must be a string or list of strings")


def session_command(args: argparse.Namespace) -> int:
    shared_status = check_shared_port(args)
    if shared_status != 0:
        return shared_status

    try:
        steps = load_session_steps(args.script)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Invalid session script: {exc}", file=sys.stderr)
        return 5

    wait_patterns = list(args.wait or [])
    if args.wait_defaults:
        wait_patterns.extend(DEFAULT_WAIT_PATTERNS)

    bad_patterns = [] if args.no_default_bad_patterns else list(DEFAULT_BAD_PATTERNS)
    bad_patterns.extend(args.fail_pattern or [])

    transcript = open_transcript(args.transcript)
    try:
        write_transcript_header(args, transcript, wait_patterns)
        with serial.Serial(args.port, args.baudrate, timeout=0.1) as ser:
            time.sleep(args.settle)
            apply_reset(ser, args)

            all_findings: list[str] = []
            for index, step in enumerate(steps, start=1):
                step_commands = list_field(
                    step.get("command") or step.get("commands"),
                    field_name=f"step {index} command",
                )
                if step_commands:
                    send_lines(
                        ser,
                        step_commands,
                        line_ending(step.get("line_ending", args.line_ending)),
                    )

                step_wait_patterns = list_field(
                    step.get("wait"),
                    field_name=f"step {index} wait",
                )
                if step.get("wait_defaults", False):
                    step_wait_patterns.extend(DEFAULT_WAIT_PATTERNS)

                step_timeout = float(step.get("timeout", args.step_timeout))
                step_wait_all = bool(step.get("wait_all", args.wait_all))
                step_post_wait_seconds = float(
                    step.get("post_wait_seconds", args.post_wait_seconds),
                )
                matched_wait, findings = read_until(
                    ser,
                    timeout=step_timeout,
                    wait_patterns=compile_patterns(step_wait_patterns),
                    wait_all=step_wait_all,
                    post_wait_seconds=step_post_wait_seconds,
                    bad_patterns=compile_patterns(bad_patterns),
                    ignore_patterns=compile_patterns(args.ignore or []),
                    transcript=transcript,
                )
                all_findings.extend(findings)

                if findings:
                    report_bad_findings(all_findings)
                    return 2

                if step_wait_patterns and not matched_wait:
                    print(
                        f"\nTimed out waiting for pattern in session step {index}.",
                        file=sys.stderr,
                    )
                    return 3

                pause_after = float(step.get("pause_after", 0.0))
                if pause_after > 0:
                    time.sleep(pause_after)

            if args.timeout != 0:
                matched_wait, findings = read_until(
                    ser,
                    timeout=args.timeout,
                    wait_patterns=compile_patterns(wait_patterns),
                    wait_all=args.wait_all,
                    post_wait_seconds=args.post_wait_seconds,
                    bad_patterns=compile_patterns(bad_patterns),
                    ignore_patterns=compile_patterns(args.ignore or []),
                    transcript=transcript,
                )
                all_findings.extend(findings)

                if findings:
                    report_bad_findings(all_findings)
                    return 2

                if wait_patterns and not matched_wait:
                    print("\nTimed out waiting for pattern.", file=sys.stderr)
                    return 3
    finally:
        if transcript:
            transcript.close()

    if all_findings:
        report_bad_findings(all_findings)
        return 2

    return 0


def serial_command(args: argparse.Namespace) -> int:
    shared_status = check_shared_port(args)
    if shared_status != 0:
        return shared_status

    wait_patterns = list(args.wait or [])
    if args.wait_defaults:
        wait_patterns.extend(DEFAULT_WAIT_PATTERNS)

    bad_patterns = [] if args.no_default_bad_patterns else list(DEFAULT_BAD_PATTERNS)
    bad_patterns.extend(args.fail_pattern or [])

    transcript = open_transcript(args.transcript)
    try:
        write_transcript_header(args, transcript, wait_patterns)
        with serial.Serial(args.port, args.baudrate, timeout=0.1) as ser:
            time.sleep(args.settle)
            apply_reset(ser, args)
            if args.command:
                send_lines(ser, args.command, line_ending(args.line_ending))

            matched_wait, findings = read_until(
                ser,
                timeout=args.timeout,
                wait_patterns=compile_patterns(wait_patterns),
                wait_all=args.wait_all,
                post_wait_seconds=args.post_wait_seconds,
                bad_patterns=compile_patterns(bad_patterns),
                ignore_patterns=compile_patterns(args.ignore or []),
                transcript=transcript,
            )
    finally:
        if transcript:
            transcript.close()

    if findings:
        report_bad_findings(findings)
        return 2

    if wait_patterns and not matched_wait:
        print("\nTimed out waiting for pattern.", file=sys.stderr)
        return 3

    return 0


def check_log_command(args: argparse.Namespace) -> int:
    if args.file:
        text = Path(args.file).expanduser().read_text(encoding="utf-8", errors="replace")
    else:
        text = sys.stdin.read()

    bad_patterns = [] if args.no_default_bad_patterns else list(DEFAULT_BAD_PATTERNS)
    bad_patterns.extend(args.fail_pattern or [])
    compiled_bad = compile_patterns(bad_patterns)
    compiled_ignore = compile_patterns(args.ignore or [])

    findings = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        if line.startswith("# serial-use "):
            continue
        if any(pattern.search(line) for pattern in compiled_ignore):
            continue
        if any(pattern.search(line) for pattern in compiled_bad):
            findings.append((line_number, line))

    if not findings:
        print("No bad log patterns detected.")
        return 0

    for line_number, line in findings:
        print(f"{line_number}: {line}")
    return 2


def add_serial_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("port", help="Serial device path, for example /dev/tty.usbmodem...")
    parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Seconds to read; use -1 for no timeout",
    )
    parser.add_argument("--settle", type=float, default=0.2, help="Delay after opening the port")
    parser.add_argument("--transcript", help="Append serial output to this file")
    parser.add_argument("--wait", action="append", help="Regex to wait for; repeatable")
    parser.add_argument(
        "--wait-defaults",
        action="store_true",
        help="Wait for common Zephyr prompts or boot banners",
    )
    parser.add_argument(
        "--wait-all",
        action="store_true",
        help="Require every --wait pattern to match before exiting",
    )
    parser.add_argument(
        "--post-wait-seconds",
        type=float,
        default=0.0,
        help="Continue reading for N seconds after the wait condition matches",
    )
    parser.add_argument("--fail-pattern", action="append", help="Extra bad-log regex; repeatable")
    parser.add_argument("--ignore", action="append", help="Ignore matching log lines; repeatable")
    parser.add_argument("--no-default-bad-patterns", action="store_true")
    parser.add_argument(
        "--no-transcript-header",
        action="store_true",
        help="Do not add serial-use metadata to transcripts",
    )
    parser.add_argument("--reset", choices=["none", "dtr", "rts", "both"], default="none")
    parser.add_argument("--reset-pulse", type=float, default=0.2)
    parser.add_argument(
        "--reset-command",
        help="Shell command to run before reading, for explicit reset workflows",
    )
    parser.add_argument(
        "--allow-shared-port",
        action="store_true",
        help="Open even when lsof shows another process on the port",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command_name", required=True)

    list_parser = subparsers.add_parser("list", help="List local serial ports")
    list_parser.add_argument("--json", action="store_true")
    list_parser.set_defaults(func=list_command)

    monitor_parser = subparsers.add_parser("monitor", help="Open and monitor a serial console")
    add_serial_options(monitor_parser)
    monitor_parser.set_defaults(command=[], line_ending="lf", func=serial_command)

    send_parser = subparsers.add_parser(
        "send",
        help="Send one or more commands, then monitor output",
    )
    add_serial_options(send_parser)
    send_parser.add_argument(
        "command",
        nargs="+",
        help="Command line to send; repeat as separate argv values",
    )
    send_parser.add_argument("--line-ending", choices=["lf", "crlf", "cr", "none"], default="lf")
    send_parser.set_defaults(func=serial_command)

    check_parser = subparsers.add_parser(
        "check-log",
        help="Scan a transcript for bad Zephyr log patterns",
    )
    check_parser.add_argument("--file", help="Transcript path; stdin is used when omitted")
    check_parser.add_argument(
        "--fail-pattern",
        action="append",
        help="Extra bad-log regex; repeatable",
    )
    check_parser.add_argument(
        "--ignore",
        action="append",
        help="Ignore matching log lines; repeatable",
    )
    check_parser.add_argument("--no-default-bad-patterns", action="store_true")
    check_parser.set_defaults(func=check_log_command)

    recover_parser = subparsers.add_parser(
        "recover-lines",
        help="Release DTR/RTS to recover USB-serial adapter state",
    )
    recover_parser.add_argument("port", help="Serial device path, for example /dev/tty.usbmodem...")
    recover_parser.add_argument("--baudrate", type=int, default=DEFAULT_BAUDRATE)
    recover_parser.add_argument(
        "--settle",
        type=float,
        default=0.2,
        help="Delay after opening the port",
    )
    recover_parser.add_argument(
        "--hold",
        type=float,
        default=0.2,
        help="Delay after setting line states",
    )
    recover_parser.add_argument("--dtr", choices=["assert", "deassert"], default="deassert")
    recover_parser.add_argument("--rts", choices=["assert", "deassert"], default="deassert")
    recover_parser.add_argument(
        "--allow-shared-port",
        action="store_true",
        help="Open even when lsof shows another process on the port",
    )
    recover_parser.set_defaults(func=recover_lines_command)

    session_parser = subparsers.add_parser(
        "session",
        help="Run a JSON command session over one serial connection",
    )
    add_serial_options(session_parser)
    session_parser.add_argument("script", help="JSON array of command steps")
    session_parser.add_argument("--line-ending", choices=["lf", "crlf", "cr", "none"], default="lf")
    session_parser.add_argument(
        "--step-timeout",
        type=float,
        default=10.0,
        help="Default per-step wait timeout",
    )
    session_parser.set_defaults(func=session_command)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
