# Develop an MBA with local release files

The standalone CLI uses a board-specific EDK and local compiler tools. It does
not need the firmware source tree or a GitHub release. The current host workflow
supports macOS arm64 and UART MCUmgr on identity-capable C2 development firmware.
See [distribution](../DISTRIBUTION.md#edk-and-extension-packages) for the EDK
boundary and the local source/index, target, sync and lock commands.

```sh
meshbus app new hello --template native
# Use --template arduboy for an SDK-backed sketch project.
meshbus app source --output /path/to/release.json \
  --edk /path/to/c2-edk.tar.xz --toolchain /path/to/arm-zephyr-eabi \
  --sdk /path/to/sdk-arduboy
meshbus app --project hello --device '<UART port or USB serial>' \
  target --source /path/to/release.json
meshbus app --project hello sync
meshbus app --project hello doctor
meshbus app --project hello run
```

`source` describes existing local files; `sync` verifies and caches their exact
content. `run` first builds, then checks the bound device's firmware identity,
installs the complete file collection, confirms the managed Session and follows
logs. It never flashes firmware. A build failure leaves the existing app alone.
Use `run --follow-seconds 5` for bounded observation. Ctrl-C ends observation and
retains the app. A device mismatch requires an explicit new binding/target.

The independent operations are:

```sh
meshbus app --project hello build --locked --offline
meshbus app --project hello install                 # build/<id>.install/package.json
meshbus app --project hello install --replace       # allow replacing this registered app
meshbus app --project hello installed               # list registered packages
meshbus app --project hello installed hello         # persistent transaction state
meshbus app --project hello start hello
meshbus app --project hello status
meshbus app --project hello logs --session 123
meshbus app --project hello stop hello --session 123
meshbus app --project hello uninstall hello         # keep saves
meshbus app --project hello uninstall hello --remove-saves
```

Replacement stops only the same managed app and requires confirmed resource
reclamation. Another app, an uncooperative app, or an unknown stop result blocks
file mutation. Unregistered files are not overwritten or adopted. Uninstall
removes registered current/previous files; explicit `--remove-saves` also removes
that app ID's `.dat`, `.sav` and corresponding `.tmp` files under `/extra/saves`.
It does not remove another app's files or saves.

## Space and recovery

The ordinary install mode can reclaim the registered previous payload before
uploading a replacement. It preserves saves but does not promise rollback of
the removed payload. `install --replace --atomic` and `run --atomic` use version
directories and retain a complete previous version when there is enough room.
Arduboy resource builds require the EDK's resource-location resolver for this
mode. Capacity checks include files, transaction records and new directories;
a conservative rejection reports required/free bytes without deleting other apps.

The installer verifies local files, uploads and verifies the control record,
reserves a transaction, uploads and checks every device SHA256, then commits.
The device blocks MBA launch while a transaction is pending. A single registry
rename selects a complete group. Failed cleanup keeps the journal for retry.

```sh
meshbus app --project hello installed hello
meshbus app --project hello recover hello           # retry commit/cleanup
meshbus app --project hello recover hello --abort   # discard an uncommitted install
meshbus app --project hello rollback hello          # retained complete previous group
```

An incomplete upload must be discarded before installing again; `recover` does
not manufacture missing bytes. Abort is refused after commit and during a
rollback or uninstall; resume those operations with `recover`. An interrupted
uninstall retains its original save-removal policy. A complete matching install
is idempotent within the same layout. To change an existing identical bundle
between direct and atomic layouts, uninstall it (retaining saves) and reinstall
with the desired mode. The recovery contract covers controlled failures, disconnects and
restarts, not physical power-cut durability.

## Watch, logs and diagnostics

`meshbus app --project hello watch` coalesces source changes for 500 ms and runs
one build/deploy at a time. It ignores build outputs and its own state/logs.
Failures consume the current revision instead of retrying indefinitely. On
connection loss it reports unknown device state; reconnect the same bound device
and save a source file to retry. Ctrl-C releases the UART and reports the last
query result without resetting, flashing or stopping the app.

Each `run` records a separate directory under `.meshbus-runs/`: device identity,
package/MBA digests, Session, raw device log, MBA and matching symbol files. The
latest confirmed start is referenced by `.meshbus-last-run.json`. Firmware logs
are a shared stream: deferred lines can predate the Session. State is determined
by protocol replies, never inferred from log text. `logs` follows an exact
Session and clearly labels this stream boundary.

```sh
meshbus app --project hello capture frame.png
meshbus app --project hello diagnose --section .text --offset 0
```

Capture requires the recorded build to be running before and after the frozen
frame is read. The PNG and companion JSON record the Session and snapshot;
this is software framebuffer evidence, not physical OLED readback.

The builder retains `.symbols.elf` and its digest separately while stripping
only debug sections from the transferred MBA. Run archives survive a later
build or failed deployment. Diagnosis verifies both files against the recorded
MBA identity before invoking local `addr2line`. The offset above is an explicit
section-relative address from matching symbols, not a raw runtime PC. Without
a trusted runtime address map, matching source symbols, or valid digests, the
CLI reports an unknown mapping. Sketch-generated sources retain `#line` origins.
