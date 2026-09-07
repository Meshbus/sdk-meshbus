# West Remote Device Transport

`west remote device` forwards the GDB and serial endpoints for one device
attached to the machine running the command to an SSH development host. It does
not synchronize sources, build, run Twister, manage workspaces, or download
artifacts.

## Host Roles

- **Device host:** the machine with the USB probe and serial device. Run
  `west remote device` here.
- **Development host:** the SSH destination passed to the command. Run the
  editor/Codex, ordinary `west build`, debugger, flash runner, and serial client
  here, using the forwarded loopback endpoints.

These roles do not depend on where Codex runs. For example, Codex and the build
workspace can live on a remote Linux computer while devices are plugged into a
local Mac. Run the forwarding command on the Mac, targeting Linux. The device
host must be able to SSH into the development host.

```text
Development host                    Device host
GDB / flash / serial client          USB debug probe / UART
        |                                   ^
        +--- loopback endpoints -- SSH -----+
                                   reverse tunnels initiated by device host
```

This is GDB/RFC2217 forwarding, not arbitrary USB passthrough.

## Requirements And Safety

- On the device host: a west workspace exposing this SDK, SSH, pyOCD, pyserial,
  and permission to access the devices.
- On the development host: an SSH server accepting reverse forwarding. Python
  is needed when a remote PTY is requested with `--serial-pty`.
- SSH authentication must work non-interactively because the transport uses
  `BatchMode=yes`.
- Debug, flash, reset, and serial forwarding require explicit authorization.
  Serial clients can change DTR/RTS and cause resets; forwarding is not a
  passive observation guarantee.
- Endpoints request loopback binding. Treat both hosts and other users on the
  development host as trusted; forwarded endpoints have no per-client login.
- Never commit credentials, keys, or live host/device maps.

Read `west remote device --help` before use.

## Forward One Device

Run one process per attached device on the **device host**. Select the probe and
serial device explicitly when several devices are connected:

```sh
west remote device dev-host.example.com \
  --target nrf54l \
  --probe <probe-a> \
  --serial /dev/tty.usbmodemA \
  --gdb-port 57065 \
  --serial-port 49221
```

The defaults are GDB port `57065`, serial port `49221`, lazy pyOCD startup, and
direct RFC2217 serial forwarding. Ctrl-C stops both tunnels and their local
servers.

An interactive terminal uses a colored live dashboard. It refreshes four times
per second and shows:

- SSH tunnel, GDB client, pyOCD stage, serial client, and PTY/RFC2217 bridge
  states.
- Three-second moving transfer rates and cumulative bytes in both directions
  for GDB and serial.
- GDB sessions, flashes, serial sessions, preemptions, endpoints, probe, baud
  rate, and uptime.

When stdout is redirected, or when `--no-dashboard` is passed, output falls
back to the concise ready summary:

```text
Remote  host   dev-host.example.com
GDB     ready  127.0.0.1:57065
Flash          west flash -r gdb -- --gdb-port 57065
Serial  ready  rfc2217://127.0.0.1:49221
Device  ready  press Ctrl-C to stop
```

Failures are reported after the dashboard closes. Add `--verbose` when
diagnosing startup to disable the dashboard and show raw pyOCD, SSH, RFC2217,
and PTY bridge warnings and logs.

For a second device, use another process with distinct ports:

```sh
west remote device dev-host.example.com \
  --target nrf54l \
  --probe <probe-b> \
  --serial /dev/tty.usbmodemB \
  --gdb-port 57066 \
  --serial-port 49222 \
  --pyocd-opt=--telnet-port=0
```

On the **development host**, flash a selected build and open its serial port:

```sh
west flash --no-rebuild -d <build-dir> -r gdb -- --gdb-port 57065
python -m serial.tools.miniterm rfc2217://127.0.0.1:49221 115200
```

The SDK's GDB runner loads the selected ELF. It does not replace
product-specific signed-image, partition, or multi-image flashing requirements.
Keep the build/artifact-to-device mapping explicit.

By default, pyOCD starts only when a GDB client connects and stops on
disconnect. `--persistent-pyocd` keeps it running and may hold or halt the
target.

## Forward Only One Endpoint

The public command remains device-oriented, but either endpoint can be disabled
when a task needs only the other one:

```sh
west remote device dev-host.example.com --no-serial --probe <probe-a>
west remote device dev-host.example.com --no-gdb --serial /dev/tty.usbmodemA
```

## Optional Remote PTY

Direct RFC2217 is the default and is required by clients that need transparent
DTR/RTS control. Add `--serial-pty` to also create a PTY symlink on the
development host:

```sh
west remote device dev-host.example.com \
  --no-gdb \
  --serial /dev/tty.usbmodemA \
  --serial-pty \
  --remote-serial /dev/meshbus-c2-uart
```

Creating a link under `/dev` requires sufficient permission on the development
host.

The PTY suits logs and shell access but does not transparently carry DTR/RTS.
A direct RFC2217 client temporarily preempts the PTY bridge; reopen a monitor
that held the old PTY after the bridge is restored.

The device host retries the same serial path after a disconnect. A changed
device path requires restarting the command. Transport startup alone is not
runtime or hardware validation. Save separate serial logs and record the device
and artifact for each authorized operation.
