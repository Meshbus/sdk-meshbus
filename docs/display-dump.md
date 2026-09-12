# Display framebuffer snapshots

Display MCUmgr group **74**, READ command **4** (`DUMP`), returns a frozen copy
of the last complete u8g2 full-buffer refresh. It does not require Desktop or
the Shell. Requests and responses use `DisplayDumpRequest` and
`DisplayDumpResponse` from `meshbus/display.proto`, carried in the existing
MCUmgr CBOR `data` byte string containing Protobuf.

Enable `CONFIG_MBS_DISPLAY`, `CONFIG_MCUMGR`, `CONFIG_U8G2` and full-buffer
mode. `CONFIG_U8G2_SNAPSHOT` defaults on for this combination. The adapter
allocates one extra frame; `CONFIG_MBS_DISPLAY_DUMP_MAX_SIZE` reserves the
shared frozen snapshot (1024 bytes by default, enough for 128x64 monochrome).
Increase this setting for a larger display. Each response carries at most
256 pixel bytes; transport buffers must also accommodate metadata, CBOR and
the SMP header. The product's 612-byte buffer accommodates this response.

## Reading a frame

1. Send `{snapshot_id: 0, offset: 0, length: 0}` (an empty request also works).
   This captures a new snapshot and returns its first chunk. Zero length means
   256 bytes; explicit lengths must be 1 through 256.
2. Retain the returned `snapshot_id`, dimensions, format and `total_size`.
3. Read further chunks using that ID and the next byte offset, until
   `offset + len(data) == total_size`. Offsets must be below `total_size`.
   Chunks may be retried or read out of order.

With the repository's host CLI, whose descriptor is regenerated at build time:

```sh
west meshbus connect -p <serial-port> -c 'display dump' --json
west meshbus connect -p <serial-port> -c 'display dump <snapshot-id> 256 256' --json
```

Replace placeholders with the actual port and the ID returned by the first
request. This CLI encodes `data` as a hexadecimal string in JSON. It returns one chunk per
command; the client assembles and exports the image.

### UART capture tool

From the repository root, with the development environment activated:

```sh
python scripts/display_capture.py --port '<serial-port>' \
  --output-dir '.scratch/<task>/display-capture'
```

The output directory must be new. The tool captures one frozen snapshot,
checks every chunk's ID, offset, dimensions and metadata, and writes:

- `frame.bin`: original framebuffer bytes.
- `frame.png`: native panel dimensions, with output inversion applied.
- `preview.png`: nearest-neighbor enlargement (default 4x; `--scale 1..8`).
- `result.json`: frame metadata, SHA256, CLI identity, UART endpoint and timed
  commands. Physical observation is explicitly left unassessed.

It uses Python's standard library and the existing Rust CLI. The CLI resolver
runs the locked incremental build once before capture; `MESHBUS_CLI` selects
an explicit executable and bypasses that build. `--baudrate` defaults to
115200 and `--timeout` to 5 seconds per request, with a further 5-second process
deadline allowance. Use `--help` for current options. The tool supports
SSD1306 page frames up to 8192 bytes, dimensions up to 1024 each and heights
divisible by eight. Larger displays require a separately reviewed tool limit.

A stale snapshot or malformed response fails the whole capture; retry into a
new directory after resolving competing captures. On failure, consult
`failure.json`; only `result.json` indicates a completed capture. Do not treat
partial files as evidence of success. The tool does not reset, flash, change
settings or select a device automatically. It supports UART only.

Display the PNG for review and record physical OLED confirmation separately
when required. A framebuffer match alone cannot establish panel operation.
Keep live endpoints and frame artifacts in ignored task storage. Device-free
tool checks, from the repository root:

```sh
python -m unittest discover -s scripts/tests -p test_display_capture.py
```

For automated navigation before capture, use the existing
[Input MCUmgr injection commands](input-injection.md). Wait for the intended UI
state within a bounded deadline; command acceptance alone does not mean the
next display refresh has completed.

One snapshot slot is shared across clients. A successful new capture replaces
the prior ID. Reading an old ID returns MCUmgr `ENOENT`; restart capture in that
case. A failed capture leaves the previous snapshot intact. Rendering, Shell
dumps, and renderer teardown do not change an already frozen snapshot. IDs
are boot-local. A lost first response can be recovered by starting a new capture.

## Pixel interpretation and errors

`SSD1306_PAGE` stores pixel `(x, y)` at byte `(y / 8) * width + x`, bit `y % 8`.
One means lit before output inversion. Apply `inverted` to get the configured
output polarity. Pixels already use physical panel coordinates; `orientation`
records the drawing orientation and must not be applied as another rotation.

The source is published only after a complete full-buffer refresh without a
reported display-write error. Drawing the next frame does not alter it.
`zui dump` copies this same source while retaining its existing text format
and raw pixel polarity. Partial-area updates do not publish a new full frame.

The dump handler returns standard MCUmgr errors: `EINVAL` for invalid arguments
or malformed requests, `ENOENT` for stale IDs, `EBADSTATE` before a frame exists,
`ENOTSUP` without snapshot support (including paged mode), and `EMSGSIZE` when
the frame exceeds frozen storage. Snapshot reads do not wake the panel.
Blanking, brightness, panel readback and physical display faults are outside
this software-frame evidence.
