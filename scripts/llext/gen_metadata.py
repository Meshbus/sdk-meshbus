#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Generate Meshbus LLEXT metadata binary from llext.yaml."""

from __future__ import annotations

import argparse
import re
import struct
import sys
import sysconfig
import zlib
from pathlib import Path
from typing import Any

import yaml


ID_MAX_LEN = 31
NAME_MAX_LEN = 63
DESCRIPTION_MAX_LEN = 63
VERSION_MAX_LEN = 15
SYMBOL_MAX_LEN = 63
TARGET_MAX_LEN = 63
COMPATIBILITY_RESERVED_LEN = 48
SERVICE_METADATA_MAGIC = 0x4D424C53
APP_METADATA_MAGIC = 0x4D424C41
APP_ICON_WIDTH = 10
APP_ICON_HEIGHT = 10
APP_ICON_RAW_DATA_LEN = ((APP_ICON_WIDTH + 7) // 8) * APP_ICON_HEIGHT
APP_ICON_DATA_MAX_LEN = APP_ICON_RAW_DATA_LEN
SOURCE_APP_ICON_PATH = Path(__file__).resolve().with_name("app_icon.png")
INSTALLED_APP_ICON_PATH = (
    Path(sysconfig.get_path("data")) / "share" / "meshbus-cli" / "app_icon.png"
)
SERVICE_METADATA_STRUCT_SIZE = (
    (5 * 4)
    + (ID_MAX_LEN + 1)
    + (NAME_MAX_LEN + 1)
    + (DESCRIPTION_MAX_LEN + 1)
    + (VERSION_MAX_LEN + 1)
    + (SYMBOL_MAX_LEN + 1)
    + (VERSION_MAX_LEN + 1)
    + COMPATIBILITY_RESERVED_LEN
    + (TARGET_MAX_LEN + 1)
    + 12
)
APP_METADATA_STRUCT_SIZE = (
    (6 * 4)
    + (ID_MAX_LEN + 1)
    + (NAME_MAX_LEN + 1)
    + (VERSION_MAX_LEN + 1)
    + (SYMBOL_MAX_LEN + 1)
    + (VERSION_MAX_LEN + 1)
    + COMPATIBILITY_RESERVED_LEN
    + (TARGET_MAX_LEN + 1)
    + APP_ICON_DATA_MAX_LEN
    + 20
)
SEMVER_RE = re.compile(
    r"^(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?"
    r"(?:\+[0-9A-Za-z][0-9A-Za-z.-]*)?$"
)


def fail(message: str) -> None:
    raise ValueError(message)


def read_yaml(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        data = yaml.safe_load(infile)

    if not isinstance(data, dict):
        fail("llext.yaml must contain a mapping object")
    return data


def get_required_str(data: dict[str, Any], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or value == "":
        fail(f"missing or invalid string field: {key}")
    return value


def get_optional_str(data: dict[str, Any], key: str) -> str:
    value = data.get(key)
    if value is None:
        return ""
    if not isinstance(value, str):
        fail(f"invalid string field: {key}")
    return value


def get_required_int(data: dict[str, Any], key: str) -> int:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"missing or invalid integer field: {key}")
    if value < 0 or value > 0xFFFFFFFF:
        fail(f"integer out of uint32 range: {key}")
    return value


def valid_id(identifier: str) -> bool:
    if len(identifier) == 0 or len(identifier) > ID_MAX_LEN:
        return False
    if not ("a" <= identifier[0] <= "z"):
        return False
    for ch in identifier[1:]:
        if not (("a" <= ch <= "z") or ("0" <= ch <= "9") or ch in "_.-"):
            return False
    return True


def valid_symbol(symbol: str) -> bool:
    if len(symbol) == 0 or len(symbol) > SYMBOL_MAX_LEN:
        return False
    first = symbol[0]
    if not (("a" <= first <= "z") or ("A" <= first <= "Z") or first == "_"):
        return False
    for ch in symbol[1:]:
        if not (("a" <= ch <= "z") or ("A" <= ch <= "Z") or ("0" <= ch <= "9") or ch == "_"):
            return False
    return True


def valid_generated_id(value: str, max_len: int) -> bool:
    raw = value.encode("utf-8")
    if len(raw) == 0 or len(raw) > max_len:
        return False
    for ch in value:
        if not (
            ("a" <= ch <= "z")
            or ("A" <= ch <= "Z")
            or ("0" <= ch <= "9")
            or ch in "._:/+-"
        ):
            return False
    return True


def fixed_c_string(
    value: str, max_len: int, field_name: str, *, allow_empty: bool = False
) -> bytes:
    raw = value.encode("utf-8")
    if not allow_empty and len(raw) == 0:
        fail(f"field must not be empty: {field_name}")
    if len(raw) > max_len:
        fail(f"field exceeds max length ({max_len}): {field_name}")
    return raw + (b"\0" * ((max_len + 1) - len(raw)))


def build_metadata_blob(
    data: dict[str, Any],
    expected_id: str | None,
    *,
    metadata_version: int,
    edk_version: str,
    target: str,
    heap_size: int,
    source_dir: Path | None = None,
) -> bytes:
    llext_type = get_required_str(data, "type")
    if llext_type == "service":
        return build_service_metadata_blob(
            data,
            expected_id,
            metadata_version=metadata_version,
            edk_version=edk_version,
            target=target,
            heap_size=heap_size,
        )
    if llext_type == "app":
        return build_app_metadata_blob(
            data,
            expected_id,
            metadata_version=metadata_version,
            edk_version=edk_version,
            target=target,
            heap_size=heap_size,
            source_dir=source_dir,
        )

    fail("llext.yaml type must be 'service' or 'app'")


def validate_common_fields(
    data: dict[str, Any],
    expected_id: str | None,
    *,
    metadata_version: int,
    edk_version: str,
    target: str,
    heap_size: int,
) -> tuple[str, str, str, int, str]:
    extension_id = get_required_str(data, "id")
    if not valid_id(extension_id):
        fail("id is invalid: must match [a-z][a-z0-9_.-]* and length <= 31")
    if expected_id is not None and extension_id != expected_id:
        fail(f"id mismatch: yaml id '{extension_id}' != expected id '{expected_id}'")

    name = get_required_str(data, "name")
    version = get_required_str(data, "version")
    if len(version.encode("utf-8")) > VERSION_MAX_LEN:
        fail(f"version exceeds max length ({VERSION_MAX_LEN})")
    if SEMVER_RE.fullmatch(version) is None:
        fail("version must be a SemVer string")

    stack_size = get_required_int(data, "stack-size")
    if stack_size == 0:
        fail("stack-size must be greater than zero")
    if heap_size <= 0 or heap_size > 0xFFFFFFFF:
        fail("heap-size estimate is invalid")
    entry_point = get_required_str(data, "entry-point")
    if not valid_symbol(entry_point):
        fail("entry-point is not a valid C symbol")
    if isinstance(metadata_version, bool) or not isinstance(metadata_version, int):
        fail("metadata-version must be an integer")
    if metadata_version <= 0 or metadata_version > 0xFFFFFFFF:
        fail("metadata-version must be in range 1..4294967295")
    if not isinstance(edk_version, str) or SEMVER_RE.fullmatch(edk_version) is None:
        fail("edk-version must be a SemVer string")
    if len(edk_version.encode("utf-8")) > VERSION_MAX_LEN:
        fail(f"edk-version exceeds max length ({VERSION_MAX_LEN})")
    if not valid_generated_id(target, TARGET_MAX_LEN):
        fail(f"target is invalid or exceeds max length ({TARGET_MAX_LEN})")

    return extension_id, name, version, stack_size, entry_point


def build_service_metadata_blob(
    data: dict[str, Any],
    expected_id: str | None,
    *,
    metadata_version: int,
    edk_version: str,
    target: str,
    heap_size: int,
) -> bytes:

    unsupported = sorted(
        set(data)
        - {"type", "id", "name", "description", "version", "entry-point", "stack-size"}
    )
    if unsupported:
        fail(
            "unsupported or build-injected field in service metadata: "
            + ", ".join(unsupported)
        )

    service_id, name, version, thread_stack, entry_point = validate_common_fields(
        data,
        expected_id,
        metadata_version=metadata_version,
        edk_version=edk_version,
        target=target,
        heap_size=heap_size,
    )
    description = get_optional_str(data, "description")

    parts = [
        struct.pack(
            "<5I",
            SERVICE_METADATA_MAGIC,
            metadata_version,
            SERVICE_METADATA_STRUCT_SIZE,
            thread_stack,
            heap_size,
        ),
        fixed_c_string(service_id, ID_MAX_LEN, "id"),
        fixed_c_string(name, NAME_MAX_LEN, "name"),
        fixed_c_string(description, DESCRIPTION_MAX_LEN, "description", allow_empty=True),
        fixed_c_string(version, VERSION_MAX_LEN, "version"),
        fixed_c_string(entry_point, SYMBOL_MAX_LEN, "entry-point"),
        fixed_c_string(edk_version, VERSION_MAX_LEN, "edk-version"),
        b"\0" * COMPATIBILITY_RESERVED_LEN,
        fixed_c_string(target, TARGET_MAX_LEN, "target"),
        b"\0" * 12,
    ]

    blob = b"".join(parts)
    if len(blob) != SERVICE_METADATA_STRUCT_SIZE:
        fail(
            f"internal error: blob size mismatch "
            f"({len(blob)} != {SERVICE_METADATA_STRUCT_SIZE})"
        )
    return blob


def default_app_icon_path() -> Path:
    if SOURCE_APP_ICON_PATH.is_file():
        return SOURCE_APP_ICON_PATH
    return INSTALLED_APP_ICON_PATH


def read_pillow_icon_data(path: Path) -> tuple[int, int, bytes]:
    try:
        from PIL import Image
    except ImportError as exc:
        raise ValueError("Pillow is not installed") from exc

    try:
        with Image.open(path) as image:
            rgba = image.convert("RGBA")
            pixels = rgba.load()
            has_transparency = any(
                pixels[x, y][3] < 128
                for y in range(rgba.height)
                for x in range(rgba.width)
            )
            bytes_per_row = (rgba.width + 7) // 8
            bitmap = bytearray(bytes_per_row * rgba.height)
            for y in range(rgba.height):
                for x in range(rgba.width):
                    red, green, blue, alpha = pixels[x, y]
                    if has_transparency:
                        foreground = alpha >= 128
                    else:
                        luminance = (red * 299 + green * 587 + blue * 114) // 1000
                        foreground = luminance < 128
                    if foreground:
                        bitmap[y * bytes_per_row + (x // 8)] |= 1 << (x % 8)
            return rgba.width, rgba.height, bytes(bitmap)
    except Exception as exc:
        raise ValueError(f"Pillow cannot convert {path}: {exc}") from exc


def read_app_icon_data(source_dir: Path) -> bytes:
    icon_path = source_dir / "icon.png"
    icon_source_name = "icon.png"
    if not icon_path.exists():
        icon_path = default_app_icon_path()
        icon_source_name = str(icon_path)
    if not icon_path.is_file():
        fail(f"{icon_source_name} is not a file: {icon_path}")

    try:
        width, height, icon_data = read_pillow_icon_data(icon_path)
    except ValueError as exc:
        try:
            return read_grayscale_png_icon_data(icon_path)
        except ValueError as fallback_exc:
            fail(f"failed to convert {icon_source_name}: {icon_path}: {exc}; "
                 f"fallback: {fallback_exc}")

    if width != APP_ICON_WIDTH or height != APP_ICON_HEIGHT:
        fail(
            f"{icon_source_name} must be exactly {APP_ICON_WIDTH}x{APP_ICON_HEIGHT} mono: "
            f"{icon_path} is {width}x{height}"
        )
    if len(icon_data) != APP_ICON_RAW_DATA_LEN:
        fail(
            f"{icon_source_name} payload must be {APP_ICON_RAW_DATA_LEN} raw mono bytes "
            f"after conversion: {icon_path} ({len(icon_data)} bytes)"
        )
    if len(icon_data) > APP_ICON_DATA_MAX_LEN:
        fail(
            f"{icon_source_name} payload exceeds {APP_ICON_DATA_MAX_LEN} bytes "
            f"after conversion: {icon_path} ({len(icon_data)} bytes)"
        )

    return icon_data


def _png_paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_grayscale_png_icon_data(path: Path) -> bytes:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("not a PNG file")

    pos = 8
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()
    while pos + 12 <= len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        chunk_type = data[pos + 4 : pos + 8]
        chunk_data = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _compression, _filter, interlace = (
                struct.unpack(">IIBBBBB", chunk_data)
            )
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if (width, height) != (APP_ICON_WIDTH, APP_ICON_HEIGHT):
        raise ValueError(f"expected {APP_ICON_WIDTH}x{APP_ICON_HEIGHT}, got {width}x{height}")
    if bit_depth != 8 or color_type != 0 or interlace != 0:
        raise ValueError("fallback supports only 8-bit grayscale non-interlaced PNG")

    raw = zlib.decompress(bytes(idat))
    stride = width
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError("unexpected PNG scanline length")

    rows: list[list[int]] = []
    prev = [0] * stride
    offset = 0
    for _y in range(height):
        filter_type = raw[offset]
        offset += 1
        row = list(raw[offset : offset + stride])
        offset += stride
        for x in range(stride):
            left = row[x - 1] if x > 0 else 0
            up = prev[x]
            up_left = prev[x - 1] if x > 0 else 0
            if filter_type == 0:
                recon = row[x]
            elif filter_type == 1:
                recon = row[x] + left
            elif filter_type == 2:
                recon = row[x] + up
            elif filter_type == 3:
                recon = row[x] + ((left + up) // 2)
            elif filter_type == 4:
                recon = row[x] + _png_paeth(left, up, up_left)
            else:
                raise ValueError(f"unsupported PNG filter type {filter_type}")
            row[x] = recon & 0xFF
        rows.append(row)
        prev = row

    xbm = bytearray()
    bytes_per_row = (width + 7) // 8
    for row in rows:
        for byte_index in range(bytes_per_row):
            value = 0
            for bit in range(8):
                x = byte_index * 8 + bit
                if x < width and row[x] < 128:
                    value |= 1 << bit
            xbm.append(value)

    icon_data = bytes(xbm)
    if len(icon_data) != APP_ICON_RAW_DATA_LEN:
        raise ValueError(f"fallback payload must be {APP_ICON_RAW_DATA_LEN} raw mono bytes")
    if len(icon_data) > APP_ICON_DATA_MAX_LEN:
        raise ValueError(f"fallback payload exceeds {APP_ICON_DATA_MAX_LEN} bytes")
    return icon_data


def build_app_metadata_blob(
    data: dict[str, Any],
    expected_id: str | None,
    *,
    metadata_version: int,
    edk_version: str,
    target: str,
    heap_size: int,
    source_dir: Path | None,
) -> bytes:
    unsupported = sorted(
        set(data) - {"type", "id", "name", "version", "entry-point", "stack-size"}
    )
    if unsupported:
        fail(
            "unsupported or build-injected field in app metadata: "
            + ", ".join(unsupported)
        )

    app_id, name, version, stack_size, entry_point = validate_common_fields(
        data,
        expected_id,
        metadata_version=metadata_version,
        edk_version=edk_version,
        target=target,
        heap_size=heap_size,
    )
    icon_data = read_app_icon_data(source_dir if source_dir is not None else Path.cwd())
    icon_pad = b"\0" * (APP_ICON_DATA_MAX_LEN - len(icon_data))

    parts = [
        struct.pack(
            "<6I",
            APP_METADATA_MAGIC,
            metadata_version,
            APP_METADATA_STRUCT_SIZE,
            stack_size,
            heap_size,
            len(icon_data),
        ),
        fixed_c_string(app_id, ID_MAX_LEN, "id"),
        fixed_c_string(name, NAME_MAX_LEN, "name"),
        fixed_c_string(version, VERSION_MAX_LEN, "version"),
        fixed_c_string(entry_point, SYMBOL_MAX_LEN, "entry-point"),
        fixed_c_string(edk_version, VERSION_MAX_LEN, "edk-version"),
        b"\0" * COMPATIBILITY_RESERVED_LEN,
        fixed_c_string(target, TARGET_MAX_LEN, "target"),
        icon_data + icon_pad,
        b"\0" * 20,
    ]

    blob = b"".join(parts)
    if len(blob) != APP_METADATA_STRUCT_SIZE:
        fail(
            f"internal error: app blob size mismatch "
            f"({len(blob)} != {APP_METADATA_STRUCT_SIZE})"
        )
    return blob


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--yaml", required=True, type=Path, dest="yaml_path")
    parser.add_argument("--out", required=True, type=Path, dest="out_path")
    parser.add_argument("--expected-id", default=None, dest="expected_id")
    parser.add_argument(
        "--metadata-version", required=True, type=int, dest="metadata_version"
    )
    parser.add_argument("--edk-version", required=True, dest="edk_version")
    parser.add_argument("--target", required=True, dest="target")
    parser.add_argument("--heap-size", required=True, type=int, dest="heap_size")
    parser.add_argument("--source-dir", type=Path, default=None, dest="source_dir")
    args = parser.parse_args()

    data = read_yaml(args.yaml_path)
    blob = build_metadata_blob(
        data,
        args.expected_id,
        metadata_version=args.metadata_version,
        edk_version=args.edk_version,
        target=args.target,
        heap_size=args.heap_size,
        source_dir=args.source_dir if args.source_dir is not None else args.yaml_path.parent,
    )

    args.out_path.parent.mkdir(parents=True, exist_ok=True)
    args.out_path.write_bytes(blob)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
