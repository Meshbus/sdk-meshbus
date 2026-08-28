# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Shared release-EDK identity and compiler-input validation helpers."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
from typing import Any


MANIFEST_NAME = "edk-release.json"


class EdkSdkError(ValueError):
    """Raised when a release EDK is malformed or has been modified."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sdk_sha256(root: Path) -> str:
    """Hash every compiler- or packager-facing file in one release EDK."""

    records = []
    try:
        for path in sorted(item for item in root.rglob("*") if item.is_file()):
            relative = PurePosixPath(path.relative_to(root).as_posix()).as_posix()
            if not _is_sdk_input(relative):
                continue
            records.append(
                {
                    "path": relative,
                    "sha256": sha256_file(path),
                    "size": path.stat().st_size,
                }
            )
    except OSError as exc:
        raise EdkSdkError(f"unable to hash release EDK compiler inputs: {exc}") from exc

    required = {"cmake.cflags", "Makefile.cflags"}
    present = {record["path"] for record in records}
    missing = sorted(required - present)
    if missing or not any(path.startswith("include/") for path in present):
        raise EdkSdkError(
            f"release EDK compiler inputs are incomplete: missing={missing}, "
            f"has-include={any(path.startswith('include/') for path in present)}"
        )

    encoded = json.dumps(records, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def load_release_manifest(root: Path, *, verify_sdk: bool = True) -> dict[str, Any]:
    """Load and validate the compact public release-EDK manifest."""

    path = root / MANIFEST_NAME
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EdkSdkError(f"unable to read release EDK manifest {path}: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema") != 1:
        raise EdkSdkError(f"release EDK manifest must use schema 1: {path}")

    host = data.get("host")
    edk = data.get("edk")
    toolchain = data.get("toolchain")
    if (
        not isinstance(data.get("publishable"), bool)
        or not _nonempty_string(data.get("target"))
        or data.get("profile") not in {"app", "service"}
        or not _positive_uint32(data.get("metadata-version"))
        or not isinstance(host, dict)
        or not _nonempty_string(host.get("application"))
        or not _nonempty_string(host.get("version"))
        or not isinstance(edk, dict)
        or not _sha256(edk.get("sdk-sha256"))
        or not isinstance(toolchain, dict)
        or not _nonempty_string(toolchain.get("compiler"))
    ):
        raise EdkSdkError(f"release EDK manifest identity fields are invalid: {path}")

    if verify_sdk:
        actual = sdk_sha256(root)
        if actual != edk["sdk-sha256"]:
            raise EdkSdkError(
                f"release EDK SDK digest does not match compiler inputs: {root}"
            )
    return data


def _is_sdk_input(relative: str) -> bool:
    if relative in {"cmake.cflags", "Makefile.cflags"}:
        return True
    if relative.startswith("include/"):
        return True
    return False


def _nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value) and len(value) <= 256


def _positive_uint32(value: Any) -> bool:
    return (
        isinstance(value, int)
        and not isinstance(value, bool)
        and 1 <= value <= 0xFFFFFFFF
    )


def _sha256(value: Any) -> bool:
    if not isinstance(value, str) or len(value) != 64:
        return False
    return all(ch in "0123456789abcdef" for ch in value)
