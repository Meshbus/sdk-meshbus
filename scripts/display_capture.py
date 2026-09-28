# SPDX-FileCopyrightText: FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Capture one Display MCUmgr snapshot over UART using the repository CLI."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

from meshbus_cli import cli_command


META = ("snapshot_id", "total_size", "width", "height", "format", "orientation", "inverted")
ORIENTATIONS = tuple("display_dump_orientation_" + name for name in (
    "horizontal", "horizontal_flip", "vertical", "vertical_flip"))
# Bound both transfer work and PNG allocation; this tool targets small OLEDs.
MAX_FRAME_SIZE = 8192


def validate_chunk(result, snapshot_id, offset):
    """Reject malformed or mismatched responses before consuming pixel bytes."""
    if not isinstance(result, dict) or any(key not in result for key in (*META, "offset", "data")):
        raise ValueError("incomplete Display DUMP response")
    for key in ("snapshot_id", "total_size", "width", "height", "offset"):
        if type(result[key]) is not int or result[key] < 0:
            raise ValueError(f"invalid {key}")
    if not 0 < result["snapshot_id"] <= 0xFFFFFFFF:
        raise ValueError("invalid snapshot_id")
    if snapshot_id and result["snapshot_id"] != snapshot_id:
        raise ValueError("snapshot changed during capture; start a new capture")
    if result["offset"] != offset:
        raise ValueError("unexpected chunk offset")
    width, height, size = result["width"], result["height"], result["total_size"]
    if not (0 < width <= 1024 and 0 < height <= 1024 and height % 8 == 0
            and 0 < size <= MAX_FRAME_SIZE and size == width * height // 8):
        raise ValueError("invalid or unsupported framebuffer dimensions/size")
    if result["format"] != "display_dump_format_ssd1306_page":
        raise ValueError("unsupported framebuffer format")
    if result["orientation"] not in ORIENTATIONS or type(result["inverted"]) is not bool:
        raise ValueError("invalid orientation or inversion metadata")
    encoded = result["data"]
    if not isinstance(encoded, str) or len(encoded) % 2 or any(
            char not in "0123456789abcdefABCDEF" for char in encoded):
        raise ValueError("pixel data must be hexadecimal CLI JSON bytes")
    pixels = bytes.fromhex(encoded)
    if offset >= size or len(pixels) != min(256, size - offset):
        raise ValueError("invalid chunk length")
    return {key: result[key] for key in META}, pixels


def capture(read):
    """Assemble one frozen snapshot; never silently combine or restart IDs."""
    meta = None
    pixels = bytearray()
    while meta is None or len(pixels) < meta["total_size"]:
        snapshot = meta["snapshot_id"] if meta else 0
        chunk_meta, chunk = validate_chunk(read(snapshot, len(pixels)), snapshot, len(pixels))
        if meta is not None and chunk_meta != meta:
            raise ValueError("framebuffer metadata changed during capture")
        meta = chunk_meta
        pixels.extend(chunk)
    return meta, bytes(pixels)


def png_bytes(meta, pixels, scale=1):
    """Render SSD1306 page bytes without rotating physical panel coordinates."""
    width, height = meta["width"], meta["height"]
    rows = bytearray()
    for y in range(height):
        row = bytearray([0])  # PNG filter: None.
        for x in range(width):
            lit = bool(pixels[(y // 8) * width + x] & (1 << (y % 8)))
            row.extend(bytes([255 if lit != meta["inverted"] else 0]) * scale)
        rows.extend(row * scale)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, height * scale, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class UART:
    def __init__(self, command, port, baudrate, timeout):
        self.command, self.port = command, port
        self.baudrate, self.timeout = baudrate, timeout
        self.exchanges = []

    def read(self, snapshot_id, offset):
        command = [*self.command, "connect", "-p", self.port,
                   "--baudrate", str(self.baudrate), "--timeout", str(self.timeout),
                   "--quiet-logs", "--json", "-c", f"display dump {snapshot_id} {offset} 256"]
        start = time.monotonic()
        # subprocess.run kills and reaps its child on timeout; Ctrl-C also waits
        # for the foreground child. Never leave a background serial owner.
        response = subprocess.run(command, capture_output=True, text=True, timeout=self.timeout + 5)
        try:
            envelope = json.loads(response.stdout)
        except json.JSONDecodeError as error:
            raise ValueError(f"CLI returned invalid JSON (exit {response.returncode})") from error
        if not isinstance(envelope, dict) or response.returncode or envelope.get("ok") is not True:
            detail = envelope.get("error", {}) if isinstance(envelope, dict) else {}
            raise ValueError(f"Display DUMP failed (exit {response.returncode}): {detail}")
        self.exchanges.append({"command": command, "seconds": time.monotonic() - start})
        return envelope.get("result")


def positive_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return seconds


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit UART serial endpoint")
    parser.add_argument("--output-dir", required=True, type=Path, help="new directory for evidence")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--timeout", type=positive_seconds, default=5, help="seconds per request")
    parser.add_argument("--scale", type=int, choices=range(1, 9), default=4)
    args = parser.parse_args(argv)
    if args.baudrate <= 0:
        parser.error("baudrate must be positive")
    out = args.output_dir.resolve()
    created = False
    try:
        # Refuse existing output before building or accessing a serial port.
        out.mkdir(parents=True, exist_ok=False)
        created = True
        command = cli_command(auto_build=True)
        uart = UART(command, args.port, args.baudrate, args.timeout)
        meta, pixels = capture(uart.read)
        (out / "frame.bin").write_bytes(pixels)
        (out / "frame.png").write_bytes(png_bytes(meta, pixels))
        (out / "preview.png").write_bytes(png_bytes(meta, pixels, args.scale))
        report = {"status": "captured", "transport": "uart", "port": args.port,
                  "baudrate": args.baudrate, "captured_at": datetime.now(timezone.utc).isoformat(),
                  "frame": meta, "sha256": hashlib.sha256(pixels).hexdigest(),
                  "cli": command, "cli_sha256": hashlib.sha256(Path(command[0]).read_bytes()).hexdigest(),
                  "exchanges": uart.exchanges, "physical_observation": "not assessed"}
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(out / "preview.png")
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        message = "capture cancelled" if isinstance(error, KeyboardInterrupt) else str(error)
        if created:
            try:
                (out / "failure.json").write_text(json.dumps({"status": "failed", "error": message}) + "\n")
            except OSError as record_error:
                print(f"could not write failure record: {record_error}", file=sys.stderr)
        print(message, file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1


if __name__ == "__main__":
    sys.exit(main())
