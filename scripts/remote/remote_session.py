# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Session subcommand for west remote."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import shlex
import shutil
import subprocess
from pathlib import Path, PurePosixPath

from west import log


HELP_EPILOG = """\
Examples:
  west remote session build-host.example.com:/srv/zephyr-workspace abc.test
  west remote session build-host.example.com:/srv/zephyr-workspace abc.test --source /path/to/sdk-meshbus
  west remote session build-host.example.com:/srv/zephyr-workspace abc.test --no-sync
  west remote session list build-host.example.com:/srv/zephyr-workspace
  west remote session delete build-host.example.com:/srv/zephyr-workspace abc.test

The default form creates or reuses <remote-workspace>/.remote/<session-id>.
It syncs the local manifest repository into
<remote-workspace>/.remote/<session-id>/<manifest.path>, excluding .git and
paths ignored by .gitignore. Extra workspace-relative directories can be synced
with --sync, for example --sync zephyr. The list and delete forms inspect or
remove whole session directories under <remote-workspace>/.remote.
"""

SESSION_ID_RE = re.compile(r"^[A-Za-z0-9._-]+$")
REMOTE_VENV_ACTIVATE = "$HOME/.zephyr/env/bin/activate"


@dataclass(frozen=True)
class SyncRoot:
    relative: PurePosixPath
    local: Path
    remote: PurePosixPath


class SessionCommand:
    """Create or update a remote session workspace."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "session",
            help="manage remote session directories",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Create, list, or delete remote session directories.",
            epilog=HELP_EPILOG,
        )
        parser.add_argument(
            "items",
            nargs="+",
            metavar="ARGS",
            help=(
                "Use SERVER SESSION to create/update, "
                "list SERVER to list, or delete SERVER SESSION to delete"
            ),
        )
        parser.add_argument(
            "--no-sync",
            action="store_true",
            help="Create/check the session directory without syncing local sources",
        )
        parser.add_argument(
            "--source",
            default=None,
            help=(
                "Local manifest repository to sync "
                "(default: <local-west-topdir>/<manifest.path>)"
            ),
        )
        parser.add_argument(
            "--sync",
            action="append",
            default=[],
            metavar="PATH",
            help=(
                "Additional local west-workspace-relative directory to sync "
                "into the same relative path under the session; repeatable"
            ),
        )
        parser.set_defaults(handler=self.run)
        return parser

    def run(self, args):
        if args.items[0] == "list":
            if args.no_sync:
                log.die("--no-sync is not valid with session list")
            if args.source:
                log.die("--source is not valid with session list")
            if args.sync:
                log.die("--sync is not valid with session list")
            if len(args.items) != 2:
                log.die("usage: west remote session list HOST:/REMOTE/WORKSPACE")
            return _list_sessions(args.items[1])

        if args.items[0] == "delete":
            if args.no_sync:
                log.die("--no-sync is not valid with session delete")
            if args.source:
                log.die("--source is not valid with session delete")
            if args.sync:
                log.die("--sync is not valid with session delete")
            if len(args.items) != 3:
                log.die("usage: west remote session delete HOST:/REMOTE/WORKSPACE SESSION")
            return _delete_session(args.items[1], args.items[2])

        if len(args.items) != 2:
            log.die("usage: west remote session HOST:/REMOTE/WORKSPACE SESSION [--no-sync]")
        if args.no_sync and args.source:
            log.die("--source has no effect with session --no-sync")
        if args.no_sync and args.sync:
            log.die("--sync has no effect with session --no-sync")

        return _create_session(
            args.items[0],
            args.items[1],
            no_sync=args.no_sync,
            source=args.source,
            sync=args.sync,
        )


def _create_session(
    server: str,
    session_id_arg: str,
    *,
    no_sync: bool,
    source: str | None,
    sync: list[str],
) -> int:
    host, remote_topdir = _parse_server(server)
    session_id = _session_id(session_id_arg)
    local_topdir = _local_topdir()
    manifest_path = _manifest_path()
    remote_session = remote_topdir / ".remote" / session_id
    remote_manifest = remote_session / manifest_path
    local_manifest = None
    sync_roots: list[SyncRoot] = []
    if not no_sync:
        local_manifest = _local_manifest_dir(local_topdir, manifest_path, source)
        sync_roots = _sync_roots(local_topdir, remote_session, sync)
        if shutil.which("rsync") is None:
            log.die("rsync not found locally")

    _check_remote_workspace(host, remote_topdir, manifest_path)
    _remote_run(
        host,
        "mkdir -p "
        + shlex.quote(str(remote_topdir / ".remote"))
        + " "
        + shlex.quote(str(remote_manifest)),
    )

    _print_check("session", True, str(remote_session))
    _print_check("manifest repo", True, str(remote_manifest))
    if local_manifest is not None:
        _rsync_session(local_manifest, host, remote_manifest)
    _rsync_sync_roots(sync_roots, host)

    _print_check("ready", True, str(remote_session))
    return 0


def _list_sessions(server: str) -> int:
    host, remote_topdir = _parse_server(server)
    remote_dir = remote_topdir / ".remote"
    script = f"""
remote_topdir={shlex.quote(str(remote_topdir))}
remote_dir={shlex.quote(str(remote_dir))}
if [ ! -d "$remote_topdir" ]; then
    printf 'remote workspace not found: %s\\n' "$remote_topdir" >&2
    exit 1
fi
if [ ! -d "$remote_dir" ]; then
    printf 'no remote sessions: %s\\n' "$remote_dir"
    exit 0
fi
printf '%-32s %-10s %s\\n' NAME SIZE MTIME
found=0
for path in "$remote_dir"/*; do
    [ -d "$path" ] || continue
    found=1
    name="${{path##*/}}"
    size="$(du -sh "$path" 2>/dev/null | awk '{{ print $1 }}')"
    mtime="$(stat -c '%y' "$path" 2>/dev/null | sed 's/\\..*//')"
    printf '%-32s %-10s %s\\n' "$name" "${{size:-unknown}}" "${{mtime:-unknown}}"
done
if [ "$found" = 0 ]; then
    printf 'no remote sessions: %s\\n' "$remote_dir"
fi
"""
    subprocess.run(["ssh", host, "sh", "-s"], input=script, text=True, check=True)
    return 0


def _delete_session(server: str, session_id_arg: str) -> int:
    host, remote_topdir = _parse_server(server)
    session_id = _session_id(session_id_arg)
    remote_session = remote_topdir / ".remote" / session_id
    _remote_run(
        host,
        "test -d "
        + shlex.quote(str(remote_topdir))
        + " && rm -rf -- "
        + shlex.quote(str(remote_session)),
    )
    _print_check("deleted", True, str(remote_session))
    return 0


def _check_remote_workspace(
    host: str,
    remote_topdir: PurePosixPath,
    expected_manifest_path: PurePosixPath,
) -> None:
    script = f"""
remote_topdir={shlex.quote(str(remote_topdir))}
remote_venv="{REMOTE_VENV_ACTIVATE}"
remote_config="$remote_topdir/.west/config"

manifest_path_from_config() {{
    awk '
        /^\\[manifest\\]/ {{ in_manifest = 1; next }}
        /^\\[/ {{ in_manifest = 0 }}
        in_manifest && /^[[:space:]]*path[[:space:]]*=/ {{
            sub(/^[[:space:]]*path[[:space:]]*=[[:space:]]*/, "", $0)
            sub(/[[:space:]]*$/, "", $0)
            print $0
            exit
        }}
    ' "$1" 2>/dev/null
}}

test -d "$remote_topdir"
test -f "$remote_config"
test -f "$remote_venv"
. "$remote_venv"
cd "$remote_topdir"
test "$(west topdir)" = "$remote_topdir"
test "$(west config manifest.path)" = {shlex.quote(expected_manifest_path.as_posix())}
test "$(manifest_path_from_config "$remote_config")" = {shlex.quote(expected_manifest_path.as_posix())}
command -v rsync >/dev/null
"""
    try:
        subprocess.run(["ssh", host, "sh", "-s"], input=script, text=True, check=True)
    except subprocess.CalledProcessError as exc:
        log.die(f"remote workspace session check failed for {host}:{remote_topdir}: {exc}")


def _rsync_session(local_manifest: Path, host: str, remote_session: PurePosixPath) -> None:
    _rsync_tree(
        local_manifest,
        host,
        remote_session,
        delete=True,
        honor_gitignore=True,
        label="remote session",
    )


def _sync_roots(
    local_topdir: Path,
    remote_session: PurePosixPath,
    sync_args: list[str],
) -> list[SyncRoot]:
    roots: list[SyncRoot] = []
    seen: set[PurePosixPath] = set()
    for value in sync_args:
        relative = _relative_sync_path(value)
        if relative in seen:
            continue
        seen.add(relative)
        local = (local_topdir / Path(relative.as_posix())).resolve()
        _check_local_sync_dir(local_topdir, local, relative)
        roots.append(SyncRoot(relative=relative, local=local, remote=remote_session / relative))
    return roots


def _check_local_sync_dir(local_topdir: Path, local: Path, relative: PurePosixPath) -> None:
    try:
        local.relative_to(local_topdir)
    except ValueError:
        log.die(f"sync path resolves outside the local west workspace: {relative}")
    if not local.is_dir():
        log.die(f"sync path is not a local directory: {relative}")


def _rsync_sync_roots(roots: list[SyncRoot], host: str, *, delete: bool = True) -> None:
    for root in roots:
        _remote_run(host, "mkdir -p " + shlex.quote(str(root.remote.parent)))
        _rsync_tree(
            root.local,
            host,
            root.remote,
            delete=delete,
            honor_gitignore=False,
            label=f"sync path {root.relative}",
        )


def _synced_zephyr_base(remote_session: PurePosixPath, sync_roots: list[SyncRoot]) -> PurePosixPath | None:
    for root in sync_roots:
        if root.relative == PurePosixPath("zephyr"):
            return remote_session / "zephyr"
    return None


def _rsync_tree(
    local: Path,
    host: str,
    remote: PurePosixPath,
    *,
    delete: bool,
    honor_gitignore: bool,
    label: str,
) -> None:
    cmd = [
        "rsync",
        "-az",
        "--no-owner",
        "--no-group",
        "--exclude=/.git/",
    ]
    if honor_gitignore:
        cmd.append("--filter=:- .gitignore")
    if delete:
        cmd.append("--delete")
    cmd.extend(
        [
            f"{local}/",
            f"{host}:{remote}/",
        ]
    )
    log.inf(f"syncing {label}: " + shlex.join(cmd))
    subprocess.check_call(cmd)


def _remote_run(host: str, cmd: str) -> None:
    subprocess.check_call(["ssh", host, cmd])


def _print_check(name: str, ok: bool, detail: str = "") -> None:
    status = "ok" if ok else "fail"
    if detail:
        print(f"{status:4} {name}: {detail}")
    else:
        print(f"{status:4} {name}")


def _parse_server(server: str) -> tuple[str, PurePosixPath]:
    if ":" not in server:
        log.die("server must use HOST:/absolute/remote/workspace syntax")

    host, path = server.split(":", 1)
    if not host:
        log.die("server is missing SSH host")
    remote_topdir = _remote_path(path)
    if not remote_topdir.is_absolute():
        log.die(f"remote workspace path must be absolute: {path}")
    if not remote_topdir.name:
        log.die(f"invalid remote workspace path: {path}")
    return host, remote_topdir


def _session_id(value: str) -> str:
    if not SESSION_ID_RE.match(value) or value in {".", ".."}:
        log.die(
            "session id must contain only letters, numbers, '.', '_' and '-', "
            f"and must not be '.' or '..': {value}"
        )
    return value


def _relative_sync_path(path: str) -> PurePosixPath:
    sync_path = PurePosixPath(path)
    if sync_path.is_absolute() or ".." in sync_path.parts:
        log.die(f"sync path must be relative and stay inside the workspace: {path}")
    if not sync_path.parts:
        log.die("sync path must not be empty")
    return sync_path


def _remote_path(path: str) -> PurePosixPath:
    cleaned = path.strip()
    if cleaned != "/":
        cleaned = cleaned.rstrip("/")
    return PurePosixPath(cleaned)


def _local_topdir() -> Path:
    try:
        output = subprocess.check_output(["west", "topdir"], text=True)
    except subprocess.CalledProcessError as exc:
        log.die(f"failed to discover local west topdir: {exc}")
    return Path(output.strip()).resolve()


def _manifest_path() -> PurePosixPath:
    try:
        output = subprocess.check_output(["west", "config", "manifest.path"], text=True)
    except subprocess.CalledProcessError as exc:
        log.die(f"failed to discover local west manifest.path: {exc}")

    path = output.strip()
    if not path:
        log.die("local west config manifest.path is empty")
    return _relative_sync_path(path)


def _local_manifest_dir(
    local_topdir: Path,
    manifest_path: PurePosixPath,
    source: str | None,
) -> Path:
    if source:
        local_manifest = Path(source).expanduser().resolve()
    else:
        local_manifest = (local_topdir / Path(manifest_path.as_posix())).resolve()

    if not local_manifest.is_dir():
        log.die(f"local manifest repo is not a directory: {local_manifest}")
    if not (local_manifest / "west.yml").is_file():
        log.die(f"local manifest repo is missing west.yml: {local_manifest}")
    if not (local_manifest / "zephyr" / "module.yml").is_file():
        log.die(f"local manifest repo is missing zephyr/module.yml: {local_manifest}")
    return local_manifest
