# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""West command for creating a pruned Meshbus LLEXT release EDK."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import yaml
from west import log
from west.commands import WestCommand

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from edk_provenance import (  # noqa: E402
    artifact_name,
    canonicalize_host_paths,
    read_host_release,
    validate_edk_target,
    validate_existing_identity,
    validate_release_mode,
    write_release_metadata,
)
from edk_release import (  # noqa: E402
    EdkReleaseError,
    filter_edk_tree,
    safe_extract_edk,
    write_deterministic_archive,
    write_sha256_sidecar,
)


class LlextEdk(WestCommand):
    """Create a public EDK from an existing Meshbus host build."""

    def __init__(self):
        super().__init__(
            "llext-edk",
            "create a public Meshbus LLEXT EDK",
            "Generate, prune, validate, and package the EDK from a Meshbus host build.",
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        return _add_edk_arguments(parser)

    def do_run(self, args, unknown_args):
        return _run_edk(args, unknown_args)


def _add_edk_arguments(parser):
    parser.add_argument("-d", "--build-dir", required=True, help="Configured host build")
    parser.add_argument("-o", "--output-dir", required=True, help="Release output directory")
    parser.add_argument(
        "--development",
        action="store_true",
        help="Allow dirty source and label the result non-publishable with a -dev name",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace the same existing development artifact identity",
    )
    return parser


def _run_edk(args, unknown_args):
    if unknown_args:
        log.die(f"unexpected arguments: {' '.join(unknown_args)}")
    build_dir = Path(args.build_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    try:
        with _temporary_west_build_info(build_dir):
            return _run_edk_from_build(args, build_dir, output_dir)
    except (EdkReleaseError, OSError, subprocess.SubprocessError) as exc:
        log.die(f"release host validation failed: {exc}")


def _run_edk_from_build(args, build_dir: Path, output_dir: Path):
    _validate_host(build_dir)

    try:
        host = read_host_release(build_dir)
        validate_release_mode(
            host,
            development=args.development,
            force=args.force,
        )
    except EdkReleaseError as exc:
        log.die(f"release host validation failed: {exc}")

    output = output_dir / artifact_name(host, development=args.development)
    sidecar = output.with_suffix(output.suffix + ".sha256")
    if (output.exists() or sidecar.exists()) and not args.force:
        log.die(f"release EDK output already exists: {output}; pass --force to replace it")
    if output.exists() and args.force:
        try:
            validate_existing_identity(output, host)
        except EdkReleaseError as exc:
            log.die(f"refusing to replace existing release EDK: {exc}")

    log.inf(f"Generating standard EDK from host build: {build_dir}")
    try:
        source = _refresh_standard_edk(build_dir)
    except (EdkReleaseError, OSError, subprocess.SubprocessError) as exc:
        log.die(f"standard EDK generation failed: {exc}")

    try:
        with tempfile.TemporaryDirectory(prefix="meshbus-llext-edk-release-") as temporary:
            root = safe_extract_edk(source, Path(temporary))
            report = filter_edk_tree(root)
            validate_edk_target(root, host.target)
            canonicalize_host_paths(root, host, build_dir)
            manifest = write_release_metadata(
                root,
                host=host,
                development=args.development,
            )
            write_deterministic_archive(root, output)
        write_sha256_sidecar(output)
    except (EdkReleaseError, OSError) as exc:
        log.die(f"release EDK generation failed: {exc}")

    log.inf(
        "Release EDK complete: "
        f"{len(report.retained_public_headers)} public headers, "
        f"{len(report.retained_generated_headers)} generated dependencies, "
        f"{len(report.removed_sdk_files)} SDK files removed"
    )
    log.inf(
        "Release identity: "
        f"{host.application} {host.version}, {host.target}, {host.profile}, "
        f"metadata={manifest['metadata-version']}, "
        f"publishable={manifest['publishable']}"
    )
    log.inf(f"Release EDK written: {output}")
    return 0


@contextmanager
def _temporary_west_build_info(build_dir: Path):
    """Add missing west topdir metadata for a sysbuild child, then restore it."""

    path = build_dir / "build_info.yml"
    if not path.is_file():
        yield
        return
    original = path.read_bytes()
    try:
        data = yaml.safe_load(original)
    except yaml.YAMLError as exc:
        raise EdkReleaseError(f"unable to parse host build info {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise EdkReleaseError(f"host build info must contain a mapping: {path}")
    west = data.get("west")
    if isinstance(west, dict) and west.get("topdir"):
        yield
        return
    if west is not None:
        raise EdkReleaseError(f"host build info contains invalid west metadata: {path}")

    topdir = _west_topdir()
    suffix = yaml.safe_dump(
        {"west": {"topdir": topdir}},
        default_flow_style=False,
        sort_keys=False,
    ).encode("utf-8")
    patched = original
    if patched and not patched.endswith(b"\n"):
        patched += b"\n"
    path.write_bytes(patched + suffix)
    try:
        yield
    finally:
        path.write_bytes(original)


def _west_topdir() -> str:
    try:
        result = subprocess.run(
            ["west", "topdir"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise EdkReleaseError(f"unable to resolve west topdir: {exc}") from exc
    topdir = result.stdout.strip()
    if not topdir:
        raise EdkReleaseError("west topdir returned an empty path")
    return topdir


def _validate_host(build_dir: Path) -> None:
    config = build_dir / "zephyr/.config"
    if not (build_dir / "CMakeCache.txt").is_file() or not config.is_file():
        log.die(f"firmware build directory is not configured: {build_dir}")
    values = _read_kconfig(config)
    if values.get("CONFIG_MESHBUS") != "y":
        log.die("host build does not enable CONFIG_MESHBUS")
    if values.get("CONFIG_MESHBUS_LLEXT") != "y":
        log.die("host build does not enable CONFIG_MESHBUS_LLEXT")
    profiles = [
        name
        for name, key in (
            ("app", "CONFIG_MESHBUS_LLEXT_APP_SERVICES"),
            ("service", "CONFIG_MESHBUS_LLEXT_BOOT_SERVICES"),
        )
        if values.get(key) == "y"
    ]
    if len(profiles) != 1:
        log.die(f"host build must enable exactly one Meshbus LLEXT profile: {profiles}")
    required = (
        build_dir / "build_info.yml",
        build_dir / "zephyr/zephyr.elf",
        build_dir / "zephyr/include/generated/zephyr/app_version.h",
        build_dir / "zephyr/include/generated/meshbus_llext_metadata_version.h",
    )
    missing = [str(path.relative_to(build_dir)) for path in required if not path.is_file()]
    if missing:
        log.die(f"host build is missing required final artifacts: {missing}")


def _refresh_standard_edk(build_dir: Path) -> Path:
    """Regenerate the upstream EDK because its target omits header dependencies."""

    source = build_dir / "zephyr/llext-edk.tar.xz"
    try:
        source.unlink(missing_ok=True)
    except OSError as exc:
        raise EdkReleaseError(f"unable to remove stale standard EDK {source}: {exc}") from exc
    subprocess.run(["west", "build", "-d", str(build_dir), "-t", "llext-edk"], check=True)
    if not source.is_file():
        raise EdkReleaseError(f"standard EDK target did not produce {source}")
    return source


def _read_kconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    pattern = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            values[match.group(1)] = match.group(2).strip()
    return values
