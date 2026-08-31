# West Remote Hardware Transports

`west remote` provides only `gdb` and `serial`. It forwards debug probes and
serial ports attached to the machine running the command to an SSH development
host. It does not synchronize sources, build, run Twister, manage workspaces,
or download artifacts.

## Host Roles

- **Device host:** the machine with the USB probes and serial devices. Run
  `west remote gdb` and `west remote serial` here.
- **Development host:** the SSH destination passed to those commands. Run
  your editor/Codex, ordinary `west build`, debugger, flash runner, and serial
  client here, using the forwarded loopback endpoints.

These roles do not depend on where Codex runs. For example, Codex and the build
workspace can live on a remote Linux computer while several devices are plugged
into your local Mac. Run the forwarding commands on the Mac, targeting Linux.
If the USB devices are on the remote computer and development is local, run
the forwarding commands there, targeting the local computer's SSH server.
The device host must be able to SSH into the development host in either case.

```text
Development host                    Device host
GDB / flash / serial client          USB debug probes / UARTs
        |                                   ^
        +--- loopback endpoint --- SSH -----+
                                   reverse tunnel initiated by device host
```

This is GDB/RFC2217 forwarding, not arbitrary USB passthrough.

## Requirements And Safety

- On the device host: a west workspace exposing this SDK, SSH, and pyOCD for
  GDB or pyserial for serial forwarding, with permission to access the devices.
- On the development host: an SSH server accepting reverse forwarding. Python
  is needed for automatic remote port allocation; the PTY bridge also requires
  pyserial. Fixed ports with `--rfc2217-only` avoid the remote Python bridge.
- Debug, flash, reset, and serial forwarding require explicit authorization.
  Serial clients can change DTR/RTS and cause resets; forwarding is not a
  passive observation guarantee.
- Endpoints request loopback binding. Treat both hosts and other users on the
  development host as trusted; forwarded endpoints have no per-client login.
- Never commit credentials, keys, or live host/device maps.

Read `west remote gdb --help` or `west remote serial --help` before use.

## GDB And Flashing

Run one process per probe on the **device host**, selecting the probe explicitly
when several are connected. Example placeholders below must be replaced:

```sh
west remote gdb dev-host.example.com --target nrf54l --probe <probe-a> --port 57065 --pyocd-opt=--telnet-port=0
west remote gdb dev-host.example.com --target nrf54l --probe <probe-b> --port 57066 --pyocd-opt=--telnet-port=0
```

Run each command in its own terminal. The extra pyOCD option chooses a free
semihosting telnet port so concurrent servers do not share its default port.
Omit `--port` to allocate GDB ports automatically and use the printed endpoints.
For different local and remote port numbers, use `--local-gdb-port` and
`--remote-gdb-port` instead.

On the **development host**, connect GDB with
`target extended-remote 127.0.0.1:57065`, or flash a selected build:

```sh
west flash --no-rebuild -d <build-dir> -r gdb -- --gdb-port 57065
```

The SDK's GDB runner remains available through `west flash`; there is no
separate `remote flash` subcommand. The runner loads the selected ELF. It does
not replace product-specific signed-image, partition, or multi-image flashing
requirements. Keep the build/artifact-to-probe mapping explicit.

By default, pyOCD starts only when a GDB client connects and stops on disconnect.
`--persistent-pyocd` keeps it running and may hold or halt the target. Ctrl-C
stops the forwarding process.

## Serial Monitoring And Serial Flashing

For RFC2217 clients, run one process per UART on the **device host**:

```sh
west remote serial dev-host.example.com /dev/tty.usbmodemA --rfc2217-only --remote-rfc2217-port 49221
west remote serial dev-host.example.com /dev/tty.usbmodemB --rfc2217-only --remote-rfc2217-port 49222
```

The **development host** can open `rfc2217://127.0.0.1:49221` or
`rfc2217://127.0.0.1:49222` with a compatible monitor or serial flash tool.
For example, use a pyserial monitor or a compatible ESP build's flash runner:

```sh
python -m serial.tools.miniterm rfc2217://127.0.0.1:49221 115200
west flash --no-rebuild -d <esp-build-dir> --esp-device rfc2217://127.0.0.1:49221
```

Use one client per UART at a time; a new direct connection preempts the old one.
ESP USB Serial/JTAG reset handling is selected by `--esp-reset-strategy`.

Without `--rfc2217-only`, the tool creates a PTY on the development host with a
symlink matching the device path (or `--remote-serial`). This requires permission
to create that `/dev` link. Existing symlinks are replaced by default; use
`--no-replace-symlink` to refuse replacement. The PTY supports logs and shell
access, but not transparent DTR/RTS control. Use RFC2217 directly for flashing.
After direct-client preemption the bridge recreates the PTY; a monitor that
held the old PTY may need to reopen it.

The device host retries opening the same serial path after a disconnect. A
changed device path requires restarting with the new path. Ctrl-C stops the
forwarding and removes the PTY link created by that process.

Transport startup alone is not runtime or hardware validation. Save separate
serial logs and record the device and artifact for each authorized operation.
