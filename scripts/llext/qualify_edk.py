# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Qualify a release EDK with public-header and official-package evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from typing import Any

import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from edk_sdk import EdkSdkError, load_release_manifest  # noqa: E402
from edk_release import EdkReleaseError, safe_extract_edk, sha256_file  # noqa: E402


PUBLIC_ROOTS = ("display", "meshbus", "zui")
HOST_PATH_PATTERNS = (
    re.compile(rb"/Users/"),
    re.compile(rb"/home/"),
    re.compile(rb"[A-Za-z]:\\\\"),
)


def qualify_edk(
    archive_path: Path,
    host_build: Path,
    *,
    package_roots: tuple[Path, ...] = (),
    packages_output: Path | None = None,
    comparison_archive: Path | None = None,
) -> dict[str, Any]:
    archive_path = archive_path.resolve()
    host_build = host_build.resolve()
    _verify_sidecar(archive_path)
    if comparison_archive is not None:
        comparison_archive = comparison_archive.resolve()
        _verify_sidecar(comparison_archive)
    if package_roots and packages_output is None:
        raise EdkReleaseError("--packages-output is required with --packages-root")
    if packages_output is not None:
        packages_output = packages_output.resolve()
    with tempfile.TemporaryDirectory(prefix="meshbus-llext-edk-qualify-") as temporary:
        root = safe_extract_edk(archive_path, Path(temporary))
        try:
            manifest = load_release_manifest(root)
        except EdkSdkError as exc:
            raise EdkReleaseError(str(exc)) from exc
        _validate_bundle(root, manifest, host_build)
        c_result = _compile_public_headers(root, host_build, language="c")
        cxx_result = _compile_public_headers(root, host_build, language="c++")
        package_result = _validate_packages(
            packages_output,
            package_roots,
            manifest=manifest,
        )
        reproducibility = _validate_reproducibility(
            archive_path,
            comparison_archive,
            manifest,
        )
        return {
            "schema": 1,
            "target": manifest["target"],
            "profile": manifest["profile"],
            "sdk-sha256": manifest["edk"]["sdk-sha256"],
            "checks": {
                "public-header-c": c_result,
                "public-header-cxx": cxx_result,
                "official-packages": package_result,
                "reproducibility": reproducibility,
            },
        }


def _verify_sidecar(archive_path: Path) -> None:
    sidecar = archive_path.with_suffix(archive_path.suffix + ".sha256")
    try:
        value = sidecar.read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise EdkReleaseError(f"unable to read EDK sidecar {sidecar}: {exc}") from exc
    expected = f"{sha256_file(archive_path)}  {archive_path.name}"
    if value != expected:
        raise EdkReleaseError(f"EDK sidecar does not match archive: {sidecar}")


def _validate_bundle(root: Path, manifest: dict[str, Any], host_build: Path) -> None:
    for name in ("LICENSE.txt", "NOTICE.txt"):
        path = root / name
        if not path.is_file() or path.stat().st_size == 0:
            raise EdkReleaseError(f"release EDK is missing required notice file: {name}")

    target = manifest.get("target")
    profile = manifest.get("profile")
    if not isinstance(target, str) or profile not in {"app", "service"}:
        raise EdkReleaseError("release manifest identity is invalid")

    config = _read_kconfig(host_build / "zephyr" / ".config")
    host_target = _config_string(config, "CONFIG_BOARD_TARGET")
    host_profile = _profile(config)
    if target != host_target or profile != host_profile:
        raise EdkReleaseError(
            f"release EDK identity {target}/{profile} does not match host "
            f"{host_target}/{host_profile}"
        )

    forbidden_sdk: list[str] = []
    host_paths: list[str] = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = PurePosixPath(path.relative_to(root).as_posix())
        data = path.read_bytes()
        if any(pattern.search(data) for pattern in HOST_PATH_PATTERNS):
            host_paths.append(relative.as_posix())
        text = relative.as_posix()
        if not (
            text.startswith("include/meshbus/")
            or "/modules/meshbus/" in f"/{text}"
        ):
            continue
        if text.startswith(
            tuple(f"include/meshbus/include/zephyr/{name}/" for name in PUBLIC_ROOTS)
        ):
            continue
        if (
            text.startswith("include/build/host/modules/meshbus/subsys/meshbus/meshbus/")
            and text.endswith(".pb.h")
        ):
            continue
        forbidden_sdk.append(text)
    if forbidden_sdk:
        raise EdkReleaseError(f"forbidden SDK files remain: {forbidden_sdk}")
    if host_paths:
        raise EdkReleaseError(f"absolute host paths remain: {host_paths}")
    _validate_cmake_paths(root)


def _compile_public_headers(root: Path, host_build: Path, *, language: str) -> dict[str, Any]:
    compiler = _compiler(host_build, cxx=language == "c++")
    flags = _cflags(root)
    flags = [flag for flag in flags if not flag.startswith("-std=")]
    flags = [flag for flag in flags if not flag.startswith("-fdiagnostics-color=")]
    flags.extend(("-fdiagnostics-color=never", "-std=c++17" if language == "c++" else "-std=c17"))
    headers = _public_headers(root)
    failures = []
    include_root = root / "include" / "meshbus" / "include"
    for path in headers:
        include = PurePosixPath(path.relative_to(include_root).as_posix()).as_posix()
        result = subprocess.run(
            [str(compiler), *flags, "-fsyntax-only", "-x", language, "-"],
            input=f"#include <{include}>\n",
            text=True,
            capture_output=True,
            timeout=60,
        )
        if result.returncode:
            failures.append({"header": include, "diagnostic": result.stderr[-2000:]})
    if failures:
        names = [record["header"] for record in failures]
        raise EdkReleaseError(f"{language} public-header smoke failed: {names}")
    version = subprocess.run(
        [str(compiler), "-dumpfullversion"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    return {
        "status": "passed",
        "count": len(headers),
        "language": language,
        "compiler": f"{compiler.name}-{version}",
    }


def _validate_packages(
    packages_output: Path | None,
    package_roots: tuple[Path, ...],
    *,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    if not package_roots:
        return {"status": "not-run", "count": 0, "packages": []}
    expected: list[tuple[str, str]] = []
    for root in package_roots:
        for directory in sorted(path for path in root.resolve().iterdir() if path.is_dir()):
            metadata_path = directory / "llext.yaml"
            if not metadata_path.is_file():
                continue
            try:
                metadata = yaml.safe_load(metadata_path.read_text(encoding="utf-8"))
            except (OSError, yaml.YAMLError) as exc:
                raise EdkReleaseError(
                    f"unable to read package metadata {metadata_path}: {exc}"
                ) from exc
            if not isinstance(metadata, dict):
                raise EdkReleaseError(f"package metadata must contain a mapping: {metadata_path}")
            package_id = metadata.get("id")
            profile = metadata.get("type")
            if not isinstance(package_id, str) or profile not in {"app", "service"}:
                raise EdkReleaseError(f"package metadata identity is invalid: {metadata_path}")
            if profile != manifest["profile"]:
                raise EdkReleaseError(
                    f"package {package_id} profile {profile} does not match EDK "
                    f"{manifest['profile']}"
                )
            expected.append((package_id, profile))
    if not expected:
        raise EdkReleaseError("package roots contain no llext.yaml applications")
    if packages_output is None or not packages_output.is_dir():
        raise EdkReleaseError(f"package output directory does not exist: {packages_output}")

    records = []
    for package_id, profile in expected:
        suffix = ".mba" if profile == "app" else ".mbs"
        artifact = packages_output / f"{package_id}{suffix}"
        if not artifact.is_file():
            raise EdkReleaseError(f"official package artifact is missing: {artifact}")
        records.append(
            {
                "id": package_id,
                "artifact": artifact.name,
                "size": artifact.stat().st_size,
                "sha256": sha256_file(artifact),
            }
        )
    return {"status": "passed", "count": len(records), "packages": records}


def _validate_reproducibility(
    archive_path: Path,
    comparison_archive: Path | None,
    manifest: dict[str, Any],
) -> dict[str, Any]:
    if comparison_archive is None:
        return {"status": "not-run"}
    if not comparison_archive.is_file():
        raise EdkReleaseError(f"comparison archive does not exist: {comparison_archive}")
    current = sha256_file(archive_path)
    comparison = sha256_file(comparison_archive)
    if current != comparison or archive_path.read_bytes() != comparison_archive.read_bytes():
        raise EdkReleaseError("repeated EDK archives are not byte-identical")
    return {
        "status": "passed",
        "archive-sha256": current,
        "sdk-sha256": manifest["edk"]["sdk-sha256"],
    }


def _public_headers(root: Path) -> tuple[Path, ...]:
    base = root / "include" / "meshbus" / "include" / "zephyr"
    headers = []
    for name in PUBLIC_ROOTS:
        directory = base / name
        if not directory.is_dir():
            raise EdkReleaseError(f"release EDK is missing public header root: {directory}")
        headers.extend(directory.rglob("*.h"))
    return tuple(sorted(path for path in headers if path.is_file()))


def _cflags(root: Path) -> list[str]:
    text = (root / "cmake.cflags").read_text(encoding="utf-8")
    match = re.search(r'^set\(LLEXT_CFLAGS "(.*)"\)$', text, re.MULTILINE)
    if not match:
        raise EdkReleaseError("release EDK cmake.cflags is missing LLEXT_CFLAGS")
    return [
        token.replace("${CMAKE_CURRENT_LIST_DIR}", str(root))
        for token in match.group(1).split(";")
        if token
    ]


def _validate_cmake_paths(root: Path) -> None:
    missing = []
    marker = str(root) + "/"
    for token in _cflags(root):
        relative = None
        if token.startswith("-I" + marker):
            relative = token[len("-I" + marker) :]
        elif token.startswith("-imacros" + marker):
            relative = token[len("-imacros" + marker) :]
        if relative is None:
            continue
        path = PurePosixPath(relative)
        if path.is_absolute() or ".." in path.parts or not root.joinpath(*path.parts).exists():
            missing.append(relative)
    if missing:
        raise EdkReleaseError(f"release EDK flags contain unsafe or missing paths: {missing}")


def _compiler(host_build: Path, *, cxx: bool) -> Path:
    cache = host_build / "CMakeCache.txt"
    key = "CMAKE_CXX_COMPILER" if cxx else "CMAKE_C_COMPILER"
    value = _cache_value(cache, key)
    if value is None and cxx:
        c_compiler = _cache_value(cache, "CMAKE_C_COMPILER")
        if c_compiler is not None:
            value = str(Path(c_compiler).with_name("arm-zephyr-eabi-g++"))
    if value is None:
        raise EdkReleaseError(f"host build does not identify {key}")
    compiler = Path(value)
    if not compiler.is_file():
        raise EdkReleaseError(f"host compiler does not exist: {compiler}")
    return compiler


def _cache_value(path: Path, key: str) -> str | None:
    pattern = re.compile(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1).strip()
    return None


def _load_mapping(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EdkReleaseError(f"unable to read {label} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise EdkReleaseError(f"{label} must contain a JSON object: {path}")
    return value


def _read_kconfig(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2).strip()
    return values


def _config_string(config: dict[str, str], key: str) -> str:
    raw = config.get(key)
    if not raw:
        raise EdkReleaseError(f"host Kconfig is missing {key}")
    try:
        value = json.loads(raw) if raw.startswith('"') else raw
    except json.JSONDecodeError as exc:
        raise EdkReleaseError(f"host Kconfig contains invalid {key}") from exc
    if not isinstance(value, str) or not value:
        raise EdkReleaseError(f"host Kconfig contains invalid {key}")
    return value


def _profile(config: dict[str, str]) -> str:
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--host-build", required=True, type=Path)
    parser.add_argument("--packages-root", action="append", default=[], type=Path)
    parser.add_argument("--packages-output", type=Path)
    parser.add_argument("--comparison-archive", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    evidence = qualify_edk(
        args.archive,
        args.host_build,
        package_roots=tuple(args.packages_root),
        packages_output=args.packages_output,
        comparison_archive=args.comparison_archive,
    )
    args.out.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"EDK qualification evidence written: {args.out}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (EdkReleaseError, OSError, subprocess.SubprocessError) as exc:
        raise SystemExit(f"error: {exc}") from exc
