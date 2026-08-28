# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Release identity and manifest helpers for LLEXT EDKs."""

from __future__ import annotations

import ast
from dataclasses import dataclass
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
from typing import Any

import yaml

from edk_release import EdkReleaseError, HEADER_POLICY
from edk_sdk import MANIFEST_NAME, EdkSdkError, sdk_sha256


MANIFEST_SCHEMA = 1
NOTICE_NAME = "NOTICE.txt"
LICENSE_NAME = "LICENSE.txt"
METADATA_VERSION_RELATIVE = Path("subsys/meshbus/services/llext/METADATA_VERSION")
VERSION_RE = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?")
SAFE_COMPONENT_RE = re.compile(r"[^a-z0-9.]+")


@dataclass(frozen=True)
class GitState:
    revision: str
    dirty: bool


@dataclass(frozen=True)
class HostRelease:
    application: str
    version: str
    build_revision: str
    source_revision: str
    source_dirty: bool
    target: str
    profile: str
    metadata_version: int
    zephyr_version: str
    zephyr_revision: str
    zephyr_dirty: bool
    toolchain_name: str
    toolchain_identity: str
    compiler_name: str
    source_root: Path
    workspace_root: Path
    zephyr_root: Path
    toolchain_root: Path

    @property
    def publishable(self) -> bool:
        return not self.source_dirty and not self.zephyr_dirty


def read_host_release(build_dir: Path) -> HostRelease:
    """Read and cross-check release identity from one configured host build."""

    build_info_path = build_dir / "build_info.yml"
    try:
        build_info = yaml.safe_load(build_info_path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as exc:
        raise EdkReleaseError(f"unable to read host build info {build_info_path}: {exc}") from exc
    if not isinstance(build_info, dict):
        raise EdkReleaseError("host build_info.yml must contain a mapping")

    cmake = _mapping(build_info, "cmake")
    application_info = _mapping(cmake, "application")
    source_dir = _absolute_directory(application_info.get("source-dir"), "application source-dir")
    source_root = _git_root(source_dir)
    if not source_dir.is_relative_to(source_root):
        raise EdkReleaseError("application source directory is outside its Git repository")
    application = source_dir.name
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", application):
        raise EdkReleaseError(f"invalid host application identity: {application!r}")

    version_header = build_dir / "zephyr/include/generated/zephyr/app_version.h"
    version, build_revision = parse_app_version_header(version_header)
    config = _read_kconfig(build_dir / "zephyr/.config")
    target = _config_string(config, "CONFIG_BOARD_TARGET")
    profile = _profile_from_config(config)
    metadata_version = read_metadata_version(source_root / METADATA_VERSION_RELATIVE)

    board = _mapping(cmake, "board")
    board_name = _required_string(board.get("name"), "cmake.board.name")
    qualifiers = board.get("qualifiers")
    if qualifiers is None:
        qualifiers = ""
    if not isinstance(qualifiers, str):
        raise EdkReleaseError("cmake.board.qualifiers must be a string")
    build_info_target = f"{board_name}/{qualifiers}" if qualifiers else board_name
    if target != build_info_target:
        raise EdkReleaseError(
            f"host target mismatch: .config={target!r}, build_info={build_info_target!r}"
        )

    source_state = _git_state(source_root)
    if re.fullmatch(r"[0-9a-fA-F]{7,40}", build_revision) and not source_state.revision.startswith(
        build_revision.lower()
    ):
        raise EdkReleaseError(
            f"host build revision {build_revision!r} does not match source revision "
            f"{source_state.revision}"
        )

    zephyr = _mapping(cmake, "zephyr")
    zephyr_version = _required_string(zephyr.get("version"), "cmake.zephyr.version")
    zephyr_base = _absolute_directory(zephyr.get("zephyr-base"), "cmake.zephyr.zephyr-base")
    zephyr_state = _git_state(_git_root(zephyr_base))

    toolchain = _mapping(cmake, "toolchain")
    toolchain_name = _required_string(toolchain.get("name"), "cmake.toolchain.name")
    toolchain_path = _absolute_directory(toolchain.get("path"), "cmake.toolchain.path")
    compiler = _cache_path(build_dir / "CMakeCache.txt", "CMAKE_C_COMPILER")
    compiler_version = _compiler_version(compiler)
    toolchain_identity = f"{toolchain_path.name}/{compiler.name}-{compiler_version}"
    workspace_root = _absolute_directory(_mapping(build_info, "west").get("topdir"), "west.topdir")

    return HostRelease(
        application=application,
        version=version,
        build_revision=build_revision,
        source_revision=source_state.revision,
        source_dirty=source_state.dirty,
        target=target,
        profile=profile,
        metadata_version=metadata_version,
        zephyr_version=zephyr_version,
        zephyr_revision=zephyr_state.revision,
        zephyr_dirty=zephyr_state.dirty,
        toolchain_name=toolchain_name,
        toolchain_identity=toolchain_identity,
        compiler_name=compiler.name,
        source_root=source_root,
        workspace_root=workspace_root,
        zephyr_root=zephyr_base,
        toolchain_root=toolchain_path,
    )


def parse_app_version_header(path: Path) -> tuple[str, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise EdkReleaseError(f"unable to read generated application version {path}: {exc}") from exc
    version = _macro_value(text, "APP_VERSION_STRING")
    build_revision = _macro_value(text, "APP_BUILD_VERSION")
    if not VERSION_RE.fullmatch(version):
        raise EdkReleaseError(f"invalid APP_VERSION_STRING: {version!r}")
    if not re.fullmatch(r"[0-9A-Za-z._+:-]{1,128}", build_revision):
        raise EdkReleaseError(f"invalid APP_BUILD_VERSION: {build_revision!r}")
    return version, build_revision


def read_metadata_version(path: Path) -> int:
    """Read one positive metadata wire-format version from a tracked file."""

    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise EdkReleaseError(f"unable to read metadata version {path}: {exc}") from exc
    if re.fullmatch(r"[1-9][0-9]*\n?", text) is None:
        raise EdkReleaseError(
            f"metadata version must contain one positive integer: {path}"
        )
    value = int(text.strip())
    if value > 0xFFFFFFFF:
        raise EdkReleaseError(f"metadata version exceeds uint32 range: {path}")
    return value


def validate_edk_target(root: Path, expected: str) -> None:
    text = (root / "cmake.cflags").read_text(encoding="utf-8")
    name = _cmake_value(text, "LLEXT_EDK_BOARD_NAME")
    qualifiers = _cmake_value(text, "LLEXT_EDK_BOARD_QUALIFIERS")
    target = f"{name}/{qualifiers}" if name and qualifiers else name
    if not target:
        target = _cmake_value(text, "LLEXT_EDK_BOARD_TARGET")
    if target != expected:
        raise EdkReleaseError(f"standard EDK target {target!r} does not match host {expected!r}")


def canonicalize_host_paths(
    root: Path, host: HostRelease, build_dir: Path
) -> dict[str, Any]:
    """Remove checkout-specific absolute paths from generated EDK headers."""

    replacements = {
        str(build_dir.resolve()): "<host-build>",
        str(host.source_root): "<sdk-meshbus>",
        str(host.zephyr_root): "<zephyr>",
        str(host.toolchain_root): "<toolchain>",
        str(host.workspace_root): "<workspace>",
    }
    ordered = sorted(replacements.items(), key=lambda item: len(item[0]), reverse=True)
    generated_prefix = PurePosixPath("include/zephyr/include/generated")
    changed: dict[str, int] = {}
    forbidden: list[str] = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        data = path.read_bytes()
        relative = PurePosixPath(path.relative_to(root).as_posix())
        count = 0
        rewritten = data
        for source, replacement in ordered:
            source_bytes = source.encode("utf-8")
            occurrences = rewritten.count(source_bytes)
            if occurrences:
                count += occurrences
                rewritten = rewritten.replace(source_bytes, replacement.encode("utf-8"))
        if not count:
            continue
        if relative.parts[: len(generated_prefix.parts)] != generated_prefix.parts:
            forbidden.append(relative.as_posix())
            continue
        path.write_bytes(rewritten)
        changed[relative.as_posix()] = count
    if forbidden:
        raise EdkReleaseError(
            f"absolute host paths occur outside generated headers: {sorted(forbidden)}"
        )
    _validate_no_host_paths(root, tuple(replacements))
    return {
        "status": "passed",
        "rewritten-files": changed,
        "replacement-count": sum(changed.values()),
    }


def artifact_name(host: HostRelease, *, development: bool) -> str:
    components = (
        normalize_artifact_component(host.application),
        normalize_artifact_component(host.version),
        normalize_artifact_component(host.target),
        normalize_artifact_component(host.profile),
    )
    marker = "-dev" if development else ""
    return "-".join(components) + f"{marker}-edk.tar.xz"


def validate_release_mode(
    host: HostRelease,
    *,
    development: bool,
    force: bool,
) -> None:
    if force and not development:
        raise EdkReleaseError("--force is permitted only with --development")
    if not development and not host.publishable:
        states = []
        if host.source_dirty:
            states.append("sdk-meshbus source is dirty")
        if host.zephyr_dirty:
            states.append("Zephyr source is dirty")
        raise EdkReleaseError("formal release requires clean source state: " + ", ".join(states))


def normalize_artifact_component(value: str) -> str:
    normalized = SAFE_COMPONENT_RE.sub("-", value.lower()).strip("-.")
    normalized = re.sub(r"-+", "-", normalized)
    if not normalized or len(normalized) > 128:
        raise EdkReleaseError(f"cannot normalize artifact component: {value!r}")
    return normalized


def write_release_metadata(
    root: Path,
    *,
    host: HostRelease,
    development: bool,
) -> dict[str, Any]:
    """Write the compact public EDK manifest and distribution notices."""

    (root / NOTICE_NAME).write_text(_notice_text(), encoding="utf-8")
    try:
        (root / LICENSE_NAME).write_bytes((host.zephyr_root / "LICENSE").read_bytes())
    except OSError as exc:
        raise EdkReleaseError(f"unable to package Zephyr license text: {exc}") from exc
    try:
        sdk_digest = sdk_sha256(root)
    except EdkSdkError as exc:
        raise EdkReleaseError(str(exc)) from exc

    manifest = {
        "schema": MANIFEST_SCHEMA,
        "metadata-version": host.metadata_version,
        "publishable": not development and host.publishable,
        "host": {
            "application": host.application,
            "version": host.version,
            "build-revision": host.build_revision,
            "source-revision": host.source_revision,
        },
        "target": host.target,
        "profile": host.profile,
        "edk": {
            "header-policy": HEADER_POLICY,
            "sdk-sha256": sdk_digest,
        },
        "zephyr": {
            "version": host.zephyr_version,
            "revision": host.zephyr_revision,
        },
        "toolchain": {
            "name": host.toolchain_name,
            "identity": host.toolchain_identity,
            "compiler": host.compiler_name,
        },
    }
    _write_json(root / MANIFEST_NAME, manifest)
    return manifest


def validate_existing_identity(path: Path, expected: HostRelease) -> None:
    """Reject development replacement when an existing archive has another identity."""

    import tarfile

    try:
        with tarfile.open(path, "r:*") as archive:
            member = archive.getmember(f"llext-edk/{MANIFEST_NAME}")
            stream = archive.extractfile(member)
            if stream is None:
                raise EdkReleaseError("existing EDK manifest is unreadable")
            manifest = json.load(stream)
    except (OSError, KeyError, json.JSONDecodeError, tarfile.TarError) as exc:
        raise EdkReleaseError(f"cannot verify existing EDK identity {path}: {exc}") from exc
    identity = (
        manifest.get("host", {}).get("application"),
        manifest.get("host", {}).get("version"),
        manifest.get("target"),
        manifest.get("profile"),
    )
    expected_identity = (expected.application, expected.version, expected.target, expected.profile)
    if identity != expected_identity:
        raise EdkReleaseError(
            f"existing EDK identity {identity!r} does not match requested {expected_identity!r}"
        )


def _notice_text() -> str:
    return """Meshbus LLEXT Release EDK

This archive is a compiler-facing development kit generated from the host
firmware build identified in edk-release.json. It contains headers originating
from Meshbus SDK, Zephyr, and the west modules selected by that firmware build.

Files retain their original copyright and SPDX license notices. The retained
Meshbus SDK public headers use SPDX-License-Identifier: Apache-2.0. Third-party files
remain governed by the notices in those files and their upstream projects.
LICENSE.txt contains the Apache License 2.0 text used by Zephyr and the retained
Meshbus SDK public headers; it does not replace file-specific third-party notices.

The EDK version records build provenance. Runtime compatibility is determined by
the target check followed by normal ELF symbol resolution and relocation.
"""


def _write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _mapping(parent: dict[str, Any], key: str) -> dict[str, Any]:
    value = parent.get(key)
    if not isinstance(value, dict):
        raise EdkReleaseError(f"build info field {key} must be a mapping")
    return value


def _required_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 256:
        raise EdkReleaseError(f"{field} must be a non-empty string")
    return value


def _absolute_directory(value: Any, field: str) -> Path:
    text = _required_string(value, field)
    path = Path(text)
    if not path.is_absolute() or not path.is_dir():
        raise EdkReleaseError(f"{field} must identify an existing absolute directory")
    return path.resolve()


def _git_root(path: Path) -> Path:
    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise EdkReleaseError(f"unable to identify Git repository for {path}: {exc}") from exc
    return Path(result.stdout.strip()).resolve()


def _git_state(root: Path) -> GitState:
    try:
        revision = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip().lower()
        status = subprocess.run(
            ["git", "-C", str(root), "status", "--porcelain=v1", "--untracked-files=normal"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise EdkReleaseError(f"unable to read Git state for {root}: {exc}") from exc
    if not re.fullmatch(r"[0-9a-f]{40,64}", revision):
        raise EdkReleaseError(f"invalid Git revision for {root}: {revision!r}")
    return GitState(revision=revision, dirty=bool(status.strip()))


def _macro_value(text: str, name: str) -> str:
    match = re.search(rf"^\s*#define\s+{re.escape(name)}(?:\s+(.*?))?\s*$", text, re.MULTILINE)
    if not match or not match.group(1):
        raise EdkReleaseError(f"generated application version is missing {name}")
    raw = match.group(1).strip()
    if raw.startswith('"'):
        try:
            value = ast.literal_eval(raw)
        except (SyntaxError, ValueError) as exc:
            raise EdkReleaseError(f"invalid generated version macro {name}: {raw}") from exc
        if not isinstance(value, str):
            raise EdkReleaseError(f"generated version macro {name} is not a string")
        return value
    return raw


def _read_kconfig(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise EdkReleaseError(f"unable to read host Kconfig {path}: {exc}") from exc
    values: dict[str, str] = {}
    for line in lines:
        match = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2).strip()
    return values


def _config_string(config: dict[str, str], key: str) -> str:
    raw = config.get(key)
    if raw is None:
        raise EdkReleaseError(f"host Kconfig is missing {key}")
    if raw.startswith('"'):
        try:
            value = ast.literal_eval(raw)
        except (SyntaxError, ValueError) as exc:
            raise EdkReleaseError(f"invalid host Kconfig string {key}") from exc
    else:
        value = raw
    if not isinstance(value, str) or not value or len(value) > 256:
        raise EdkReleaseError(f"host Kconfig {key} must be a non-empty string")
    return value


def _profile_from_config(config: dict[str, str]) -> str:
    profiles = [
        name
        for name, key in (
            ("app", "CONFIG_MESHBUS_LLEXT_APP_SERVICES"),
            ("service", "CONFIG_MESHBUS_LLEXT_BOOT_SERVICES"),
        )
        if config.get(key) == "y"
    ]
    if len(profiles) != 1:
        raise EdkReleaseError(f"host must select exactly one LLEXT profile: {profiles}")
    return profiles[0]


def _cache_path(path: Path, key: str) -> Path:
    pattern = re.compile(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            value = Path(match.group(1).strip())
            if value.is_absolute() and value.is_file():
                return value.resolve()
            break
    raise EdkReleaseError(f"host CMake cache is missing a valid {key}")


def _compiler_version(compiler: Path) -> str:
    try:
        result = subprocess.run(
            [str(compiler), "-dumpfullversion"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise EdkReleaseError(f"unable to identify compiler {compiler}: {exc}") from exc
    version = result.stdout.strip()
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", version):
        raise EdkReleaseError(f"unexpected compiler version from {compiler}: {version!r}")
    return version


def _cmake_value(text: str, name: str) -> str | None:
    match = re.search(rf"set\({re.escape(name)}\s+\"([^\"]*)\"\)", text)
    return match.group(1) if match else None


def _validate_no_host_paths(root: Path, prefixes: tuple[str, ...]) -> None:
    findings: list[str] = []
    encoded = tuple(prefix.encode("utf-8") for prefix in prefixes)
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        data = path.read_bytes()
        if any(prefix in data for prefix in encoded):
            findings.append(PurePosixPath(path.relative_to(root).as_posix()).as_posix())
    if findings:
        raise EdkReleaseError(f"absolute host paths remain in EDK files: {findings}")
