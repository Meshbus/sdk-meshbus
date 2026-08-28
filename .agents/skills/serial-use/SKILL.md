---
name: serial-use
description: Use for Zephyr firmware serial-console work when Codex needs to list serial ports, open or monitor a device console, send shell commands, wait for Zephyr prompts or boot banners, reset through serial control lines when explicitly requested, save transcripts, or inspect logs for assert, fault, panic, or error output.
---

# Serial Use

## Workflow

Use this skill when a task needs live serial evidence from a local Zephyr device.
Default to baudrate `115200`.

Serial access and MCU reset are separate concerns. Some boards expose reset
through USB-serial DTR/RTS lines, but SWD/DAP-Link boards such as
`idea_mesh_tracker_c2/nrf54l15/cpuapp` do not reliably reset the MCU through
the serial device. For those boards, keep the serial port open for capture and
trigger reset through the debug probe with `pyocd` or an equivalent SWD tool.
When the device is attached to another machine and exposed through a remote
PTY/RFC2217 bridge, treat the bridge process as part of the device connection,
not as a competing serial monitor.

First list likely ports:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py list
```

If system `python3` is missing `pyserial`, use the shared Zephyr environment
Python instead:

```sh
~/.zephyr/env/bin/python .agents/skills/serial-use/scripts/serial_use.py list
```

For interactive manual work, prefer `tio` when available:

```sh
tio --baudrate 115200 /dev/tty.usbmodem...
```

For repeatable agent workflows, use the bundled script so transcripts, waits,
commands, and log classification are consistent:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty.usbmodem... --timeout 20 --transcript /tmp/zephyr-serial.log
python3 .agents/skills/serial-use/scripts/serial_use.py send /dev/tty.usbmodem... "kernel stacks" --wait "uart:~\\$" --timeout 10
python3 .agents/skills/serial-use/scripts/serial_use.py check-log --file /tmp/zephyr-serial.log
```

## Rules

- Do not run `west flash`, `west debug`, hardware tests, or destructive device actions unless the user explicitly asks.
- Hardware reset through serial control lines is allowed during serial validation only for boards where DTR/RTS are known to reset the target MCU.
- For SWD/DAP-Link boards, especially nRF54L15 targets, do not assume DTR/RTS reset the MCU. Prefer `--reset-command "pyocd commander -t <target> -c reset -c exit"` so the serial helper opens the port first, then resets through SWD, then captures boot output.
- Use `--reset-command <cmd>` only when the command is already known to be safe for the connected hardware or the user provides it. Record the exact command in the final validation note.
- Save a transcript when the serial evidence will support a bug report, validation result, or final claim.
- Report the exact port, baudrate, reset method, wait pattern, timeout, commands sent, and transcript path in the final answer.
- If a port is held by another monitor, close it first or ask before killing it. Use `--allow-shared-port` only for diagnosis because another reader can consume the serial output.
- Exception: if `lsof <port>` shows the remote serial bridge itself holding the
  PTY, do not kill it. Use `--allow-shared-port` and state that the process is
  the required bridge, for example a Python process mapping RFC2217 to
  `/dev/pts/N`.

## Termination Design

Choose the wait condition from the evidence you need, not from habit.

- Boot smoke: wait for a boot banner or shell prompt, then classify the log.
  Use `--wait-defaults` or explicit boot/prompt regexes.
- Startup/connect flows: wait for the service's ready or connected log. Avoid
  exiting at the first prompt when background startup continues after shell init.
- Async API flows: do not wait for `uart:~\$` as the endpoint. Send the command,
  then wait for the later business success or business failure event.

By default, multiple `--wait` patterns are ORed and the first match exits.
Use `--wait-all` when every checkpoint must appear. Use
`--post-wait-seconds N` when a synchronous result is followed by trailing async
events that should remain in the transcript.

## Reset Ladder

When a known port opens but produces no boot or shell output:

1. If the board uses SWD/DAP-Link for target reset, reset through the debug
   probe while the serial helper is already listening. For nRF54L15:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... \
  --reset-command "pyocd commander -t nrf54l -c reset -c exit" \
  --wait-defaults \
  --timeout 20 \
  --transcript /tmp/zephyr-serial.log
```

Use this pattern for `idea_mesh_tracker_c2/nrf54l15/cpuapp` and similar
CMSIS-DAP/DAP-Link workflows. Plain serial monitoring can observe logs, but
DTR/RTS line toggling is not a reliable MCU reset for these targets.

2. If the device is remote and flashing already works through a forwarded GDB
   endpoint, prefer GDB remote reset when local probe access is unavailable.
   Open the serial monitor first, then reset through the same GDB endpoint so
   the transcript captures the post-reset startup logs.
   If `pyocd` reports missing local USB support or waits forever for a debug
   probe, stop that attempt and use the forwarded GDB endpoint instead.

```sh
~/.zephyr/env/bin/python .agents/skills/serial-use/scripts/serial_use.py monitor /dev/cu.usbmodem... \
  --allow-shared-port \
  --wait "meshbus_combine: Build timestamp" \
  --post-wait-seconds 3 \
  --timeout 45 \
  --transcript /tmp/zephyr-gdb-reset.log
```

In another command, use the GDB binary recorded in
`<build-dir>/zephyr/runners.yaml` and the ELF in the same build directory:

```sh
<arm-zephyr-eabi-gdb> -q <build-dir>/zephyr/zephyr.elf \
  -ex "target extended-remote 127.0.0.1:<gdb-port>" \
  -ex "monitor reset" \
  -ex "detach" \
  -ex "quit"
```

For the current Meshbus remote tracker flow, the validated shape is:

```sh
west flash -d build.idea_mesh_tracker_c2 -r gdb -- --gdb-port 62001
```

and reset validation uses the same `127.0.0.1:62001` GDB remote endpoint. The
validated shell/UART port was `/dev/cu.usbmodemC20C8F062AB82`, exposed as a
symlink to `/dev/pts/N` by the remote bridge. Run flash and GDB reset commands
from `west topdir`. If the build directory is at the workspace root, invoke the
command from that root instead of `meshbus/`.

For development logs from the beginning of startup, start the serial monitor
before flash or reset. Prefer waiting for a business endpoint such as
`meshbus_combine: Build timestamp` over the first shell prompt when background
startup continues after shell init. Record the `Build timestamp` when present
so hardware feedback can be matched to the flashed image.

3. If the board is known to wire serial control lines to reset or boot mode,
   try DTR/RTS reset modes:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... --reset dtr --wait-defaults --timeout 20 --transcript /tmp/zephyr-serial.log
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... --reset rts --wait-defaults --timeout 20 --transcript /tmp/zephyr-serial.log
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... --reset both --wait-defaults --timeout 20 --transcript /tmp/zephyr-serial.log
```

Prefer the first board-appropriate reset mode that reliably produces the boot
banner or shell. If all reset attempts produce empty transcripts, run
`lsof <port>` and check for an existing monitor such as `tio`, `screen`, or
Espressif `idf_monitor.py`.

For full post-reset startup evidence, prefer a reset plus a business endpoint
and enough timeout for subsystem initialization:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... \
  --reset-command "pyocd commander -t nrf54l -c reset -c exit" \
  --wait "thingsboard: connected|app: ready|uart:~\\$" \
  --post-wait-seconds 2 \
  --timeout 45 \
  --transcript /tmp/zephyr-startup.log
python3 .agents/skills/serial-use/scripts/serial_use.py check-log --file /tmp/zephyr-startup.log
```

Before flashing ESP32-S3 boards that expose USB-Serial/JTAG, release DTR/RTS
if an earlier monitor left the adapter in a bad line state:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py recover-lines /dev/tty.usbmodem...
```

## Wait Patterns

Use explicit `--wait` patterns for task-specific checkpoints. Common Zephyr
patterns are:

- `uart:~\$`
- `shell>`
- `Booting Zephyr`
- `*** Booting Zephyr OS`

The script treats `--wait` values as regular expressions and exits successfully
once any requested pattern is observed.

Use `--wait-all` for ordered boot evidence where every pattern must be seen:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... \
  --reset dtr \
  --wait "Booting Zephyr" \
  --wait "uart:~\\$" \
  --wait-all \
  --timeout 30 \
  --transcript /tmp/zephyr-boot.log
```

## Network Retry Logs

Network bring-up, DNS, and MQTT flows commonly print transient retry errors
such as `-EAGAIN` before the business endpoint succeeds. For those workflows,
disable default bad patterns and add explicit business failure patterns:

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... \
  --no-default-bad-patterns \
  --fail-pattern "thingsboard: publish failed permanently|mqtt: auth failed" \
  --wait "thingsboard: connected|telemetry publish ok" \
  --timeout 90 \
  --transcript /tmp/zephyr-network.log
```

Then describe the disabled defaults and explicit failure patterns in the final
validation note.

## Async Commands

For commands that return immediately and complete later, use a two-stage flow:
`send` to trigger the command, then `monitor` to wait for the async event.

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py send /dev/tty... \
  "thingsboard telemetry publish" \
  --post-wait-seconds 1 \
  --timeout 2 \
  --transcript /tmp/zephyr-async.log
python3 .agents/skills/serial-use/scripts/serial_use.py monitor /dev/tty... \
  --wait "telemetry publish ok|telemetry publish failed" \
  --timeout 30 \
  --transcript /tmp/zephyr-async.log
```

For longer repeatable flows, use `session` with a JSON array. Each string is a
command; each object may define `command`, `commands`, `wait`, `wait_defaults`,
`wait_all`, `timeout`, `post_wait_seconds`, `line_ending`, and `pause_after`.
All steps run on one open serial connection and append to one transcript.

```json
[
  {
    "command": "thingsboard telemetry publish",
    "wait": "publish queued",
    "timeout": 5,
    "post_wait_seconds": 1
  },
  {
    "wait": "telemetry publish ok|telemetry publish failed",
    "timeout": 30
  }
]
```

```sh
python3 .agents/skills/serial-use/scripts/serial_use.py session /dev/tty... /tmp/zephyr-session.json \
  --timeout 0 \
  --transcript /tmp/zephyr-session.log
```

## Log Classification

Run `check-log` on captured output before claiming a boot or command result is
clean. Default bad-log patterns flag common Zephyr failure signals such as
assertions, kernel oops/panic, hard faults, failure lines, and error-level logs.

If a known benign line matches `error`, use `--ignore <regex>` and state that
filter in the result.

Transcripts include a metadata header by default: timestamp, subcommand, port,
baudrate, reset mode, reset command, wait patterns, timeout, and command. Use
`--no-transcript-header` only when a pure serial byte-for-byte text file is more
important than provenance.
