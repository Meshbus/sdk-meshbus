# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Twister subcommand for west remote."""

from __future__ import annotations

import argparse
import shlex
import subprocess
from pathlib import Path, PurePosixPath

from west import log

from remote_session import (
    REMOTE_VENV_ACTIVATE,
    SyncRoot,
    _check_remote_workspace,
    _local_manifest_dir,
    _local_topdir,
    _manifest_path,
    _parse_server,
    _remote_run,
    _rsync_sync_roots,
    _session_id,
    _sync_roots,
    _synced_zephyr_base,
)


HELP_EPILOG = """\
Examples:
  west remote twister build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout -- \\
    -T tests/subsys/meshbus/services/clock -p qemu_x86 --inline-logs -v -c
  west remote twister build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout --sync zephyr -- \\
    -T samples/drivers/lora -p tracker_t1000_e --inline-logs -v -c
  west remote twister build-host.example.com:/srv/zephyr-workspace abc.test \\
    --source /path/to/sdk-checkout --no-sync -- \\
    -O twister-clock -T tests/subsys/meshbus/services/clock -p qemu_x86 -c

The command creates or reuses <remote-workspace>/.remote/<session-id>. It syncs
the selected source checkout to <session>/<manifest.path> unless --no-sync is
used, maps paths below that checkout, and runs west twister with the session
copy in EXTRA_ZEPHYR_MODULES. If --source is omitted, the source is
<local-west-topdir>/<manifest.path>. Extra workspace-relative roots passed with
--sync are copied to matching session paths. With --sync zephyr, Twister uses
ZEPHYR_BASE=<session>/zephyr.
"""


class TwisterCommand:
    """Sync a remote session and run Twister there."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "twister",
            help="run Twister in a remote session",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Sync a remote session and run west twister against it.",
            epilog=HELP_EPILOG,
        )
        _append_twister_help(parser)
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
            "--no-delete",
            action="store_true",
            help="Do not delete remote session files that are absent locally",
        )
        parser.add_argument(
            "--clean",
            action="store_true",
            help="Delete the remote Twister output directory after a successful run",
        )
        parser.set_defaults(handler=self.run, pass_unknown_args=True)
        return parser

    def run(self, args, unknown_args):
        host, remote_topdir = _parse_server(args.server)
        session_id = _session_id(args.session_id)
        local_topdir = _local_topdir()
        manifest_path = _manifest_path()
        local_manifest = _local_manifest_dir(local_topdir, manifest_path, args.source)

        twister_args = _strip_leading_separator(unknown_args)
        if not twister_args:
            log.die("missing west twister arguments")

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
            _rsync_twister_session(
                local_manifest,
                host,
                remote_manifest,
                delete=not args.no_delete,
            )
            _rsync_sync_roots(sync_roots, host, delete=not args.no_delete)

        plan = _remote_twister_plan(
            twister_args,
            manifest_path=manifest_path,
            local_manifest=local_manifest,
            remote_manifest=remote_manifest,
            remote_session=remote_session,
            sync_roots=sync_roots,
        )
        remote_cmd = _remote_twister_command(
            remote_topdir,
            plan.args,
            remote_manifest=remote_manifest,
            zephyr_base=_synced_zephyr_base(remote_session, sync_roots),
        )
        log.inf("running remote twister command: ssh " + shlex.join([host, remote_cmd]))
        result = subprocess.call(["ssh", host, remote_cmd])
        if args.clean:
            if result == 0:
                _clean_remote_output(host, plan.outdir)
            else:
                log.wrn(f"preserving failed remote Twister output: {host}:{plan.outdir}")
        return result


def _strip_leading_separator(args: list[str]) -> list[str]:
    if args and args[0] == "--":
        return args[1:]
    return args


class TwisterPlan:
    def __init__(self, args: list[str], outdir: PurePosixPath):
        self.args = args
        self.outdir = outdir


def _remote_twister_plan(
    twister_args: list[str],
    *,
    manifest_path: PurePosixPath,
    local_manifest: Path,
    remote_manifest: PurePosixPath,
    remote_session: PurePosixPath,
    sync_roots: list[SyncRoot],
) -> TwisterPlan:
    args, outdir = _map_outdir(twister_args, remote_session)
    args = _translate_paths(
        args,
        manifest_path=manifest_path,
        local_manifest=local_manifest,
        remote_manifest=remote_manifest,
        sync_roots=sync_roots,
    )
    args = _append_extra_modules(args, remote_manifest)
    return TwisterPlan(args, outdir)


def _map_outdir(
    args: list[str],
    remote_session: PurePosixPath,
) -> tuple[list[str], PurePosixPath]:
    separator = _separator_index(args)
    before_test_args = args[:separator]
    after_test_args = args[separator:]
    mapped: list[str] = []
    outdir_subdir = PurePosixPath("twister-out")
    found = False
    index = 0
    while index < len(before_test_args):
        arg = before_test_args[index]
        if arg in {"-O", "--outdir"}:
            if index + 1 >= len(before_test_args):
                log.die(f"{arg} requires an output directory")
            outdir_subdir = _session_subdir(before_test_args[index + 1], "output directory")
            mapped.extend([arg, str(remote_session / outdir_subdir)])
            found = True
            index += 2
            continue
        if arg.startswith("--outdir="):
            outdir_subdir = _session_subdir(arg.split("=", 1)[1], "output directory")
            mapped.append("--outdir=" + str(remote_session / outdir_subdir))
            found = True
            index += 1
            continue
        mapped.append(arg)
        index += 1

    outdir = remote_session / outdir_subdir
    if not found:
        mapped.extend(["-O", str(outdir)])
    return mapped + after_test_args, outdir


def _session_subdir(value: str, label: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts:
        log.die(f"remote twister {label} must stay inside the session: {value}")
    if not path.parts:
        log.die(f"remote twister {label} must not be empty")
    return path


def _translate_paths(
    args: list[str],
    *,
    manifest_path: PurePosixPath,
    local_manifest: Path,
    remote_manifest: PurePosixPath,
    sync_roots: list[SyncRoot],
) -> list[str]:
    separator = _separator_index(args)
    before_test_args = [
        _translate_path(
            arg,
            manifest_path=manifest_path,
            local_manifest=local_manifest,
            remote_manifest=remote_manifest,
            sync_roots=sync_roots,
        )
        for arg in args[:separator]
    ]
    return before_test_args + args[separator:]


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


def _append_extra_modules(args: list[str], remote_manifest: PurePosixPath) -> list[str]:
    extra_arg = f"EXTRA_ZEPHYR_MODULES={remote_manifest}"
    separator = _separator_index(args)
    return [*args[:separator], "-x", extra_arg, *args[separator:]]


def _separator_index(args: list[str]) -> int:
    try:
        return args.index("--")
    except ValueError:
        return len(args)


def _rsync_twister_session(
    local_manifest: Path,
    host: str,
    remote_manifest: PurePosixPath,
    *,
    delete: bool,
) -> None:
    cmd = [
        "rsync",
        "-az",
        "--no-owner",
        "--no-group",
        "--exclude=/.git/",
        "--filter=:- .gitignore",
    ]
    if delete:
        cmd.append("--delete")
    cmd.extend(
        [
            f"{local_manifest}/",
            f"{host}:{remote_manifest}/",
        ]
    )
    log.inf("syncing remote twister session: " + shlex.join(cmd))
    subprocess.check_call(cmd)


def _remote_twister_command(
    remote_topdir: PurePosixPath,
    twister_args: list[str],
    *,
    remote_manifest: PurePosixPath,
    zephyr_base: PurePosixPath | None,
) -> str:
    env_args = [f"EXTRA_ZEPHYR_MODULES={remote_manifest}"]
    if zephyr_base is not None:
        env_args.append(f"ZEPHYR_BASE={zephyr_base}")
    command = [
        "set -e",
        f"cd {shlex.quote(str(remote_topdir))}",
        f'. "{REMOTE_VENV_ACTIVATE}"',
        "env " + shlex.join(env_args) + " west twister " + shlex.join(twister_args),
    ]
    return "; ".join(command)


def _clean_remote_output(host: str, remote_output: PurePosixPath) -> None:
    log.inf(f"cleaning remote Twister output: {host}:{remote_output}")
    _remote_run(host, "rm -rf -- " + shlex.quote(str(remote_output)))


def _append_twister_help(parser: argparse.ArgumentParser) -> None:
    format_help = parser.format_help

    def format_help_with_twister(*args, **kwargs) -> str:
        return format_help(*args, **kwargs) + "\nOriginal west twister help:\n\n" + _twister_help()

    parser.format_help = format_help_with_twister


def _twister_help() -> str:
    try:
        return subprocess.check_output(
            ["west", "twister", "-h"],
            stderr=subprocess.STDOUT,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        return f"Original west twister help unavailable: {exc}\n"
