---
name: serial-use
description: Monitor and operate a Zephyr device serial console, capture transcripts, wait for boot or business events, send non-destructive shell commands, and classify fault logs. Use only when the task needs live serial evidence.
---

# Serial Use

Use the bundled helper for repeatable serial capture. Default to 115200 baud and
passive monitoring before sending commands.

## Setup

From anywhere inside this Git repository:

```sh
repo="$(git rev-parse --show-toplevel)"
serial_tool="$repo/.agents/skills/serial-use/scripts/serial_use.py"
python="$HOME/.zephyr/env/bin/python"
```

Use the shared Zephyr Python when system Python lacks pyserial. Do not install
packages globally as part of serial validation.

## Basic Workflow

List ports without assuming a device path:

```sh
"$python" "$serial_tool" list
```

Capture passively and retain evidence:

```sh
"$python" "$serial_tool" monitor <port> \
  --baudrate 115200 \
  --wait "<business-ready-regex>" \
  --fail-pattern "<business-failure-regex>" \
  --post-wait-seconds 2 \
  --timeout 30 \
  --transcript /tmp/meshbus-serial.log
```

Classify the transcript before claiming a clean result:

```sh
"$python" "$serial_tool" check-log \
  --file /tmp/meshbus-serial.log \
  --fail-pattern "<business-failure-regex>"
```

Send only a known non-destructive command required by the task, then wait for
its observable result:

```sh
"$python" "$serial_tool" send <port> "<command>" \
  --wait "<success-regex>" \
  --fail-pattern "<failure-regex>" \
  --timeout 20 \
  --transcript /tmp/meshbus-command.log
```

Keep success and terminal failure on separate exit paths: `--wait` contains
only success conditions, while every task-specific terminal failure uses
`--fail-pattern`. Repeat the same failure patterns when classifying a saved
transcript. Generic `error` and `failed` words remain ignored by default so a
transient retry does not fail the run. Use `--wait-all` when every success
checkpoint is required and `--post-wait-seconds` for trailing asynchronous
output.

For supported advanced options and JSON sessions, read the tool's `--help`
rather than expanding this skill into a device-specific runbook.

## Safety

- Serial access does not authorize flash, debug, reset, erase, power control,
  destructive shell commands, or killing another process.
- Ask for explicit authorization before any reset method or destructive command.
- Do not guess DTR/RTS behavior or execute a reset command merely because a port
  is silent.
- If another process owns the port, report it. Use `--allow-shared-port` only
  when the owner is a required, understood bridge and record that fact.
- Keep credentials, pairing secrets, keys, and machine-specific device maps out
  of committed files and final summaries.

## Reporting

Report the port descriptively, baud rate, wait/failure patterns, timeout,
commands sent, transcript path, observed result, and anything left unverified.
Do not claim hardware behavior beyond what the serial output actually proves.
