# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Build subcommand for west remote."""

from __future__ import annotations

import argparse
import shutil
import shlex
import subprocess
from pathlib import Path, PurePosixPath

import yaml
from west import log

from remote_flash import (
    _artifact_paths,
    _load_yaml,
    _remote_topdir_from_cmake_cache,
)
from remote_session import (
    REMOTE_VENV_ACTIVATE,
    SyncRoot,
    _check_remote_workspace,
    _local_manifest_dir,
    _local_topdir,
    _manifest_path,
    _parse_server,
    _remote_run,
    _rsync_session,
    _rsync_sync_roots,
    _session_id,
    _sync_roots,
    _synced_zephyr_base,
)


HELP_EPILOG = """\
Examples:
  west remote build build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout -- \\
    -p auto -b qemu_x86 tests/subsys/meshbus/services/clock
  west remote build build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout --sync zephyr -- \\
    -p auto -b tracker_t1000_e samples/drivers/lora/send
  west remote build build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout --fetch -- \\
    -p auto -b qemu_x86 tests/subsys/meshbus/services/clock
  west remote build build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout --no-sync -- \\
    -d build/clock -b qemu_x86 tests/subsys/meshbus/services/clock

The command creates or reuses <remote-workspace>/.remote/<session-id>. It syncs
the selected source checkout to <session>/<manifest.path> unless --no-sync is
used, maps paths below that checkout, and runs west build remotely with
-DEXTRA_ZEPHYR_MODULES=<session>/<manifest.path>. If --source is omitted, the
source is <local-west-topdir>/<manifest.path>. Extra workspace-relative roots
passed with --sync are copied to matching session paths. With --sync zephyr,
the remote build uses ZEPHYR_BASE=<session>/zephyr.
"""

FETCH_CORE_FILES = (
    "CMakeCache.txt",
    "compile_commands.json",
    "zephyr/.config",
    "zephyr/runners.yaml",
    "zephyr/edt.pickle",
    "zephyr/zephyr.dts",
    "zephyr/include/generated/zephyr/autoconf.h",
    "zephyr/include/generated/zephyr/devicetree_generated.h",
    "zephyr/zephyr.elf",
    "zephyr/zephyr.map",
    "zephyr/zephyr.hex",
    "zephyr/zephyr.bin",
    "zephyr/zephyr.uf2",
    "zephyr/zephyr.stat",
)


class BuildCommand:
    """Sync a remote session and run west build there."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "build",
            help="run west build in a remote session",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Sync a remote session and run west build against it.",
            epilog=HELP_EPILOG,
        )
        parser.add_argument(
            "server",
            metavar="HOST:/REMOTE/WORKSPACE",
            help="SSH host and remote west workspace path",
        )
        parser.add_argument(
            "session_id",
            help="Session id used as <remote-workspace>/.remote/<session-id>",
        )
        parser.add_argument(
            "--no-sync",
            action="store_true",
            help="Reuse the existing session directory without syncing local sources",
        )
        parser.add_argument(
            "--source",
            default=None,
            help=(
                "Local SDK/manifest checkout to sync and use for path rewrites "
                "(default: <local-west-topdir>/<manifest.path>)"
            ),
        )
        parser.add_argument(
            "--sync",
            action="append",
            default=[],
            metavar="PATH",
            help=(
                "Additional local west-workspace-relative directory to sync, "
                "or reference from an existing session with --no-sync; repeatable"
            ),
        )
        parser.add_argument(
            "--fetch",
            action="store_true",
            help="Fetch a local debug/flash bundle after the remote build finishes",
        )
        parser.add_argument(
            "--clean",
            action="store_true",
            help="Delete the remote build output directory after a successful build/fetch",
        )
        parser.add_argument(
            "--local-build-dir",
            default=None,
            help=(
                "Local directory for --fetch output "
                "(default: <local-west-topdir>/<remote-build-subdir>)"
            ),
        )
        parser.set_defaults(handler=self.run, pass_unknown_args=True)
        return parser

    def run(self, args, unknown_args):
        host, remote_topdir = _parse_server(args.server)
        session_id = _session_id(args.session_id)
        local_topdir = _local_topdir()
        manifest_path = _manifest_path()
        local_manifest = _local_manifest_dir(local_topdir, manifest_path, args.source)

        build_args = _strip_leading_separator(unknown_args)
        if not build_args:
            log.die("missing west build arguments")

        remote_session = remote_topdir / ".remote" / session_id
        remote_manifest = remote_session / manifest_path
        sync_roots = _sync_roots(local_topdir, remote_session, args.sync)
        _check_remote_workspace(host, remote_topdir, manifest_path)
        _remote_run(
            host,
            "mkdir -p "
            + shlex.quote(str(remote_topdir / ".remote"))
            + " "
            + shlex.quote(str(remote_manifest)),
        )

        if not args.no_sync:
            _rsync_session(local_manifest, host, remote_manifest)
            _rsync_sync_roots(sync_roots, host)

        plan = _remote_build_plan(
            build_args,
            manifest_path=manifest_path,
            local_manifest=local_manifest,
            remote_manifest=remote_manifest,
            remote_build_root=remote_session,
            sync_roots=sync_roots,
        )
        remote_cmd = _remote_build_command(
            remote_topdir,
            plan.args,
            zephyr_base=_synced_zephyr_base(remote_session, sync_roots),
        )
        log.inf("running remote build command: ssh " + shlex.join([host, remote_cmd]))
        result = _remote_call(host, remote_cmd)
        if args.fetch:
            local_build = _local_fetch_dir(
                args.local_build_dir,
                local_topdir,
                plan.build_subdir,
            )
            _fetch_build_bundle(
                host,
                plan.build_dir,
                local_build,
                local_topdir=local_topdir,
                local_manifest=local_manifest,
                remote_manifest=remote_manifest,
                sync_roots=sync_roots,
            )
        if args.clean:
            if result == 0:
                _clean_remote_output(host, plan.build_dir)
            else:
                log.wrn(f"preserving failed remote build output: {host}:{plan.build_dir}")
        return result


def _strip_leading_separator(args: list[str]) -> list[str]:
    if args and args[0] == "--":
        return args[1:]
    return args


class BuildPlan:
    def __init__(self, args: list[str], build_dir: PurePosixPath, build_subdir: PurePosixPath):
        self.args = args
        self.build_dir = build_dir
        self.build_subdir = build_subdir


def _remote_build_plan(
    build_args: list[str],
    *,
    manifest_path: PurePosixPath,
    local_manifest: Path,
    remote_manifest: PurePosixPath,
    remote_build_root: PurePosixPath,
    sync_roots: list[SyncRoot],
) -> BuildPlan:
    args, build_dir, build_subdir = _map_build_dir(build_args, remote_build_root)
    args = [
        _translate_path(
            arg,
            manifest_path=manifest_path,
            local_manifest=local_manifest,
            remote_manifest=remote_manifest,
            sync_roots=sync_roots,
        )
        for arg in args
    ]
    args = _append_extra_modules(args, remote_manifest)
    return BuildPlan(args, build_dir, build_subdir)


def _map_build_dir(
    args: list[str],
    remote_build_root: PurePosixPath,
) -> tuple[list[str], PurePosixPath, PurePosixPath]:
    insert_at = _cmake_separator_index(args)
    before_cmake = args[:insert_at]
    after_cmake = args[insert_at:]
    mapped: list[str] = []
    build_subdir = PurePosixPath("build")
    found = False
    index = 0
    while index < len(before_cmake):
        arg = before_cmake[index]
        if arg in {"-d", "--build-dir"}:
            if index + 1 >= len(before_cmake):
                log.die(f"{arg} requires a build directory")
            build_subdir = _build_subdir(before_cmake[index + 1])
            mapped.extend([arg, str(remote_build_root / build_subdir)])
            found = True
            index += 2
            continue
        if arg.startswith("--build-dir="):
            build_subdir = _build_subdir(arg.split("=", 1)[1])
            mapped.append("--build-dir=" + str(remote_build_root / build_subdir))
            found = True
            index += 1
            continue
        mapped.append(arg)
        index += 1

    build_dir = remote_build_root / build_subdir
    if not found:
        mapped.extend(["-d", str(build_dir)])
    return mapped + after_cmake, build_dir, build_subdir


def _build_subdir(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts:
        log.die(f"remote build directory must stay inside the session: {value}")
    if not path.parts:
        log.die("remote build directory must not be empty")
    return path


def _append_extra_modules(args: list[str], remote_manifest: PurePosixPath) -> list[str]:
    extra_arg = f"-DEXTRA_ZEPHYR_MODULES={remote_manifest}"
    separator = _cmake_separator_index(args)
    if separator == len(args):
        return [*args, "--", extra_arg]
    return [*args, extra_arg]


def _cmake_separator_index(args: list[str]) -> int:
    try:
        return args.index("--")
    except ValueError:
        return len(args)


def _translate_path(
    arg: str,
    *,
    manifest_path: PurePosixPath,
    local_manifest: Path,
    remote_manifest: PurePosixPath,
    sync_roots: list[SyncRoot],
) -> str:
    manifest = manifest_path.as_posix()
    if arg == manifest:
        return str(remote_manifest)
    if arg.startswith(manifest + "/"):
        return str(remote_manifest / arg[len(manifest) + 1 :])
    if "=" + manifest + "/" in arg:
        return arg.replace("=" + manifest + "/", "=" + str(remote_manifest) + "/", 1)
    for root in sync_roots:
        relative = root.relative.as_posix()
        if arg == relative:
            return str(root.remote)
        if arg.startswith(relative + "/"):
            return str(root.remote / arg[len(relative) + 1 :])
        if "=" + relative + "/" in arg:
            return arg.replace("=" + relative + "/", "=" + str(root.remote) + "/", 1)
    if arg.startswith("-"):
        return arg
    if "/" in arg and (local_manifest / arg).exists():
        return str(remote_manifest / arg)
    for root in sync_roots:
        if "/" in arg and (root.local / arg).exists():
            return str(root.remote / arg)
    return arg


def _remote_build_command(
    remote_topdir: PurePosixPath,
    build_args: list[str],
    *,
    zephyr_base: PurePosixPath | None,
) -> str:
    env_args = []
    if zephyr_base is not None:
        env_args.append(f"ZEPHYR_BASE={zephyr_base}")
    west_build = "west build " + shlex.join(build_args)
    if env_args:
        west_build = "env " + shlex.join(env_args) + " " + west_build
    command = [
        "set -e",
        f"cd {shlex.quote(str(remote_topdir))}",
        f'. "{REMOTE_VENV_ACTIVATE}"',
        west_build,
    ]
    return "; ".join(command)


def _remote_call(host: str, cmd: str) -> int:
    return subprocess.call(["ssh", host, cmd])


def _clean_remote_output(host: str, remote_output: PurePosixPath) -> None:
    log.inf(f"cleaning remote build output: {host}:{remote_output}")
    _remote_run(host, "rm -rf -- " + shlex.quote(str(remote_output)))


def _local_fetch_dir(
    local_build_dir: str | None,
    local_topdir: Path,
    build_subdir: PurePosixPath,
) -> Path:
    if local_build_dir:
        return Path(local_build_dir).resolve()
    return (local_topdir / Path(build_subdir.as_posix())).resolve()


def _fetch_build_bundle(
    host: str,
    remote_build: PurePosixPath,
    local_build: Path,
    *,
    local_topdir: Path,
    local_manifest: Path,
    remote_manifest: PurePosixPath,
    sync_roots: list[SyncRoot],
) -> None:
    local_build.mkdir(parents=True, exist_ok=True)
    paths = _remote_existing_paths(host, remote_build, FETCH_CORE_FILES)
    _rsync_remote_files(host, remote_build, local_build, paths)

    local_runners = local_build / "zephyr" / "runners.yaml"
    if local_runners.is_file():
        runners_yaml = _load_yaml(local_runners)
        artifact_paths = ["zephyr/" + path.as_posix() for path in _artifact_paths(runners_yaml)]
        artifact_paths = _remote_existing_paths(host, remote_build, artifact_paths)
        _rsync_remote_files(host, remote_build, local_build, artifact_paths)

    local_cache = local_build / "CMakeCache.txt"
    if local_runners.is_file() and local_cache.is_file():
        remote_topdir = _remote_topdir_from_cmake_cache(local_cache)
        runners_yaml = _load_yaml(local_runners)
        remote_zephyr_home = _remote_zephyr_home(host)
        replacements = [
            (str(remote_manifest), str(local_manifest)),
            *[(str(root.remote), str(root.local)) for root in sync_roots],
            (str(remote_build), str(local_build)),
            (str(remote_topdir), str(local_topdir)),
            (remote_zephyr_home, str(Path.home() / ".zephyr")),
        ]
        _rewrite_runner_paths(local_runners, runners_yaml, replacements=replacements)
        _rewrite_cmake_cache(local_cache, replacements)
        _write_noop_build_ninja(local_build)

    log.inf(f"fetched remote build bundle: {local_build}")


def _rewrite_cmake_cache(cache_path: Path, replacements: list[tuple[str, str]]) -> None:
    text = cache_path.read_text(encoding="utf-8")
    rewritten = 0
    for remote_prefix, local_prefix in replacements:
        if not remote_prefix or remote_prefix == local_prefix:
            continue
        count = text.count(remote_prefix)
        if count:
            text = text.replace(remote_prefix, local_prefix)
            rewritten += count

    ninja = shutil.which("ninja")
    if ninja:
        text, count = _rewrite_cmake_cache_entry(
            text,
            "CMAKE_MAKE_PROGRAM:FILEPATH=",
            ninja,
        )
        rewritten += count
    else:
        log.wrn("ninja not found locally; bare west flash may require --no-rebuild")

    cache_path.write_text(text, encoding="utf-8")
    log.inf(f"rewrote {rewritten} CMakeCache.txt path(s) for local use")


def _rewrite_cmake_cache_entry(text: str, prefix: str, value: str) -> tuple[str, int]:
    lines = text.splitlines(keepends=True)
    count = 0
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            newline = "\n" if line.endswith("\n") else ""
            lines[index] = prefix + value + newline
            count += 1
    return "".join(lines), count


def _write_noop_build_ninja(local_build: Path) -> None:
    build_ninja = local_build / "build.ninja"
    build_ninja.write_text(
        "# Generated by west remote build --fetch.\n"
        "# The real build ran remotely; keep local west flash rebuild as a no-op.\n"
        "ninja_required_version = 1.3\n"
        "rule noop\n"
        "  command = true\n"
        "  description = remote build artifact bundle\n"
        "build all: noop\n"
        "default all\n",
        encoding="utf-8",
    )
    log.inf(f"wrote no-op local build file: {build_ninja}")


def _remote_zephyr_home(host: str) -> str:
    output = subprocess.check_output(
        ["ssh", host, 'printf "%s" "$HOME/.zephyr"'],
        text=True,
    )
    return output.strip()


def _rewrite_runner_paths(
    local_runners: Path,
    runners_yaml: dict,
    *,
    replacements: list[tuple[str, str]],
) -> None:
    rewritten = 0
    for remote_prefix, local_prefix in replacements:
        if not remote_prefix or remote_prefix == local_prefix:
            continue
        runners_yaml, count = _rewrite_value(runners_yaml, remote_prefix, local_prefix)
        rewritten += count

    if rewritten == 0:
        log.wrn(f"no runners.yaml paths matched remote prefixes: {local_runners}")
        return

    with local_runners.open("w", encoding="utf-8") as stream:
        yaml.safe_dump(runners_yaml, stream, default_flow_style=False, sort_keys=False)
    log.inf(f"rewrote {rewritten} runners.yaml path(s) for local use")


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


def _remote_existing_paths(
    host: str,
    remote_build: PurePosixPath,
    candidates: tuple[str, ...] | list[str],
) -> list[str]:
    if not candidates:
        return []

    quoted_candidates = " ".join(shlex.quote(path) for path in candidates)
    script = (
        f"cd {shlex.quote(str(remote_build))} || exit 0; "
        f"for p in {quoted_candidates}; do [ -e \"$p\" ] && printf '%s\\n' \"$p\"; done; "
        "find zephyr -maxdepth 1 -type f "
        "\\( -name 'zephyr.signed.*' -o -name '*merged*.*' \\) -print 2>/dev/null || true"
    )
    output = subprocess.check_output(["ssh", host, script], text=True)
    paths: list[str] = []
    seen: set[str] = set()
    for line in output.splitlines():
        path = line.strip()
        if not path or path in seen:
            continue
        seen.add(path)
        paths.append(path)
    return paths


def _rsync_remote_files(
    host: str,
    remote_build: PurePosixPath,
    local_build: Path,
    paths: list[str],
) -> None:
    if not paths:
        log.wrn(f"no remote build files matched for fetch: {host}:{remote_build}")
        return

    cmd = [
        "rsync",
        "-az",
        "--files-from=-",
        f"{host}:{remote_build}/",
        str(local_build) + "/",
    ]
    log.inf("fetching remote build files: " + shlex.join(cmd))
    subprocess.run(
        cmd,
        input="\n".join(paths) + "\n",
        text=True,
        check=True,
    )
