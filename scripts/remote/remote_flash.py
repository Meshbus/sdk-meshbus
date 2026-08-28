# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Flash subcommand for west remote."""

from __future__ import annotations

import argparse
import shlex
import subprocess
from pathlib import Path, PurePosixPath
from typing import Iterable

import yaml
from west import log


ARTIFACT_KEYS = (
    "elf_file",
    "exe_file",
    "hex_file",
    "bin_file",
    "uf2_file",
    "mot_file",
)

REQUIRED_METADATA_FILES = (
    PurePosixPath("CMakeCache.txt"),
    PurePosixPath("zephyr/.config"),
)

OPTIONAL_METADATA_FILES = (
    PurePosixPath("zephyr/edt.pickle"),
)


class FlashCommand:
    """Flash a remotely built Zephyr image through local west runners."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "flash",
            help="flash a remote Zephyr build through local hardware",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=(
                "Sync a minimal runner bundle from a remote build directory, "
                "then run west flash locally."
            ),
            epilog="""\
Examples:
  west remote flash -s build-host.example.com -d /srv/zephyr-workspace/build/app -r pyocd
  west remote flash -s build-host.example.com -d /srv/zephyr-workspace/build/app -c build/app -r pyocd
  west remote flash -s build-host.example.com -d /srv/zephyr-workspace/build/app -c build/app -r pyocd --context

If --local-build-dir is omitted, the local build directory is created in the
current directory using the remote build directory basename. The remote build
directory must contain zephyr/runners.yaml.
""",
        )
        parser.add_argument(
            "-s",
            "--ssh-host",
            required=True,
            help="SSH host containing the remote Zephyr build directory",
        )
        parser.add_argument(
            "-d",
            "--remote-build-dir",
            required=True,
            help="Remote build directory containing zephyr/runners.yaml",
        )
        parser.add_argument(
            "-c",
            "--local-build-dir",
            default=None,
            help=(
                "Local build directory used for the synced runner bundle "
                "(default: ./<remote-build-dir-basename>)"
            ),
        )
        parser.set_defaults(handler=self.run, pass_unknown_args=True)
        return parser

    def run(self, args, unknown_args):
        remote_build = _remote_path(args.remote_build_dir)
        if not remote_build.name:
            log.die(f"invalid remote build directory: {args.remote_build_dir}")
        if args.local_build_dir:
            local_build = Path(args.local_build_dir).resolve()
        else:
            local_build = Path.cwd() / remote_build.name

        remote_runners = remote_build / "zephyr" / "runners.yaml"
        local_runners = local_build / "zephyr" / "runners.yaml"

        self._check_remote_runners(args.ssh_host, remote_runners)
        log.inf(f"syncing remote runner manifest: {args.ssh_host}:{remote_runners}")
        self._copy_file(args.ssh_host, remote_runners, local_runners)

        runners_yaml = _load_yaml(local_runners)
        artifact_paths = list(_artifact_paths(runners_yaml))
        if not artifact_paths:
            log.wrn("runners.yaml does not list hex/bin/elf/uf2/mot artifacts")

        for relative_path in REQUIRED_METADATA_FILES:
            self._copy_file(
                args.ssh_host,
                remote_build / relative_path,
                local_build / Path(relative_path.as_posix()),
            )

        remote_topdir = _remote_topdir_from_cmake_cache(local_build / "CMakeCache.txt")
        local_topdir = self._local_topdir()

        for relative_path in OPTIONAL_METADATA_FILES:
            self._copy_optional_file(
                args.ssh_host,
                remote_build / relative_path,
                local_build / Path(relative_path.as_posix()),
            )

        for relative_path in artifact_paths:
            self._copy_file(
                args.ssh_host,
                remote_build / "zephyr" / relative_path,
                local_build / "zephyr" / Path(relative_path.as_posix()),
            )

        _rewrite_remote_topdir_paths(local_runners, runners_yaml, remote_topdir, local_topdir)

        flash_args = [arg for arg in unknown_args if arg != "--"]
        cmd = ["west", "flash", "-d", str(local_build), "--no-rebuild", *flash_args]
        log.inf("running local flash command: " + shlex.join(cmd))
        return subprocess.call(cmd)

    def _check_remote_runners(self, host: str, remote_runners: PurePosixPath) -> None:
        cmd = f"test -f {shlex.quote(str(remote_runners))}"
        try:
            subprocess.check_call(["ssh", host, cmd])
        except subprocess.CalledProcessError:
            log.die(f"remote runners.yaml not found: {host}:{remote_runners}")

    def _local_topdir(self) -> Path:
        try:
            output = subprocess.check_output(["west", "topdir"], text=True)
        except subprocess.CalledProcessError as exc:
            log.die(f"failed to discover local west topdir: {exc}")
        return Path(output.strip()).resolve()

    def _copy_file(self, host: str, remote_file: PurePosixPath, local_file: Path) -> None:
        local_file.parent.mkdir(parents=True, exist_ok=True)
        remote_spec = f"{host}:{shlex.quote(str(remote_file))}"
        subprocess.check_call(["scp", remote_spec, str(local_file)])

    def _copy_optional_file(self, host: str, remote_file: PurePosixPath, local_file: Path) -> None:
        check = f"test -f {shlex.quote(str(remote_file))}"
        if subprocess.call(["ssh", host, check]) != 0:
            log.dbg(f"optional remote file not found: {host}:{remote_file}")
            return
        self._copy_file(host, remote_file, local_file)


def _remote_path(path: str) -> PurePosixPath:
    cleaned = path.strip()
    if cleaned != "/":
        cleaned = cleaned.rstrip("/")
    return PurePosixPath(cleaned)


def _load_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        data = yaml.safe_load(stream)
    if not isinstance(data, dict):
        log.die(f"invalid runners.yaml content: {path}")
    return data


def _remote_topdir_from_cmake_cache(cache_path: Path) -> PurePosixPath:
    cache = _load_cmake_cache(cache_path)
    zephyr_base = cache.get("ZEPHYR_BASE")
    if not zephyr_base:
        log.die(f"cannot infer remote west topdir: ZEPHYR_BASE missing in {cache_path}")

    zephyr_base_path = _remote_path(zephyr_base)
    if zephyr_base_path.name != "zephyr":
        log.die(
            "cannot infer remote west topdir: expected ZEPHYR_BASE to end in "
            f"'zephyr', got {zephyr_base}"
        )

    topdir = zephyr_base_path.parent
    log.inf(f"inferred remote west topdir from CMakeCache.txt: {topdir}")
    return topdir


def _load_cmake_cache(cache_path: Path) -> dict[str, str]:
    cache: dict[str, str] = {}
    with cache_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            line = line.strip()
            if not line or line.startswith(("#", "//")) or "=" not in line:
                continue
            key_type, value = line.split("=", 1)
            key = key_type.split(":", 1)[0]
            cache[key] = value
    return cache


def _artifact_paths(runners_yaml: dict) -> Iterable[PurePosixPath]:
    config = runners_yaml.get("config")
    if not isinstance(config, dict):
        log.die("runners.yaml is missing config map")

    seen: set[PurePosixPath] = set()
    for key in ARTIFACT_KEYS:
        value = config.get(key)
        if value is None:
            continue
        if not isinstance(value, str):
            log.die(f"runners.yaml config.{key} must be a string")
        relpath = _safe_relative_path(value, f"config.{key}")
        if relpath not in seen:
            seen.add(relpath)
            yield relpath


def _safe_relative_path(value: str, field: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts:
        log.die(f"unsupported absolute or parent-relative path in runners.yaml {field}: {value}")
    return path


def _rewrite_remote_topdir_paths(
    local_runners: Path,
    runners_yaml: dict,
    remote_topdir: PurePosixPath,
    local_topdir: Path,
) -> None:
    _, rewritten = _rewrite_value(runners_yaml, str(remote_topdir), str(local_topdir))
    if rewritten == 0:
        log.wrn(f"no runners.yaml paths matched remote west topdir: {remote_topdir}")
        return

    with local_runners.open("w", encoding="utf-8") as stream:
        yaml.safe_dump(runners_yaml, stream, default_flow_style=False, sort_keys=False)
    log.inf(f"rewrote {rewritten} runners.yaml path(s) from {remote_topdir} to {local_topdir}")


def _rewrite_value(value, remote_prefix: str, local_prefix: str):
    if isinstance(value, dict):
        count = 0
        for key, child in value.items():
            value[key], child_count = _rewrite_value(child, remote_prefix, local_prefix)
            count += child_count
        return value, count
    if isinstance(value, list):
        count = 0
        for index, child in enumerate(value):
            value[index], child_count = _rewrite_value(child, remote_prefix, local_prefix)
            count += child_count
        return value, count
    if not isinstance(value, str):
        return value, 0

    rewritten = value.replace(remote_prefix, local_prefix)
    if rewritten == value:
        return value, 0
    return rewritten, 1
