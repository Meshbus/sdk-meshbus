# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Doctor subcommand for west remote."""

from __future__ import annotations

import argparse
import configparser
import shlex
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from west import log


HELP_EPILOG = """\
Examples:
  west remote doctor build-host.example.com:/srv/zephyr-workspace
  west remote doctor build-host.example.com:/srv/zephyr-workspace --fix
  west remote doctor build-host.example.com:/srv/zephyr-workspace --fix --update

The server argument points at the remote west workspace. By default this command
only checks the local and remote workspace contract. With --fix it copies the
local .west/config if missing, seeds the manifest repository if missing, and
creates the remote .remote/ directory. With --fix --update it also updates the
remote SDK/workspace with west.
"""

REMOTE_VENV_ACTIVATE = "$HOME/.zephyr/env/bin/activate"


@dataclass(frozen=True)
class LocalWorkspace:
    topdir: Path
    west_config: Path
    manifest_path: PurePosixPath
    manifest_dir: Path
    rsync: str | None


class DoctorCommand:
    """Diagnose and optionally repair a remote west workspace."""

    def add_parser(self, subparsers):
        parser = subparsers.add_parser(
            "doctor",
            help="diagnose a remote west workspace",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="Check or repair the remote workspace contract used by west remote.",
            epilog=HELP_EPILOG,
        )
        parser.add_argument(
            "server",
            metavar="HOST:/REMOTE/WORKSPACE",
            help="SSH host and remote west workspace path",
        )
        parser.add_argument(
            "--fix",
            action="store_true",
            help="Repair missing remote .west/config, SDK checkout, and .remote/ directory",
        )
        parser.add_argument(
            "--update",
            action="store_true",
            help="With --fix, update the remote SDK, west modules, packages, SDK, and blobs",
        )
        parser.set_defaults(handler=self.run)
        return parser

    def run(self, args):
        if args.update and not args.fix:
            log.die("--update requires --fix")

        host, remote_topdir = _parse_server(args.server)
        local = _local_workspace()

        _print_section("local")
        local_ok = _report_local(local)

        if args.fix:
            _print_section("fix")
            _fix_remote(host, remote_topdir, local, update=args.update)

        _print_section("remote")
        remote = _probe_remote(host, remote_topdir)
        remote_ok = _report_remote(remote, local.manifest_path)
        return 0 if local_ok and remote_ok else 1


def _report_local(local: LocalWorkspace) -> bool:
    ok = True
    _print_check("west topdir", True, str(local.topdir))
    _print_check("west config", local.west_config.is_file(), str(local.west_config))
    ok = ok and local.west_config.is_file()
    _print_check("manifest.path", True, local.manifest_path.as_posix())
    _print_check("manifest repo", local.manifest_dir.is_dir(), str(local.manifest_dir))
    ok = ok and local.manifest_dir.is_dir()
    _print_check("rsync", local.rsync is not None, local.rsync or "not found")
    ok = ok and local.rsync is not None
    return ok


def _report_remote(data: dict[str, str], expected_manifest_path: PurePosixPath) -> bool:
    if "ssh_error" in data:
        _print_check("ssh", False, data["ssh_error"])
        return False

    ok = data.get("ssh_returncode") == "0"
    _print_check("ssh", ok, f"returncode={data.get('ssh_returncode', 'unknown')}")
    if data.get("host"):
        _print_check("hostname", True, data["host"])

    workspace_ok = data.get("workspace") == "yes"
    config_ok = data.get("west_config") == "yes"
    manifest_path = data.get("config_manifest_path", "")
    manifest_ok = manifest_path == expected_manifest_path.as_posix()
    sdk_ok = data.get("sdk") == "yes"
    remote_dir_ok = data.get("remote_dir") == "yes"
    venv_ok = data.get("venv") == "yes"
    west_ok = bool(data.get("west"))
    west_topdir_ok = data.get("west_topdir") == data.get("remote_topdir")
    west_manifest_ok = data.get("west_manifest_path") == expected_manifest_path.as_posix()
    twister_ok = data.get("twister") == "yes"
    rsync_ok = bool(data.get("rsync"))

    _print_check("workspace", workspace_ok, data.get("remote_topdir", ""))
    _print_check("west config", config_ok, data.get("west_config_path", ""))
    _print_check("config manifest.path", manifest_ok, manifest_path or "not found")
    _print_check("manifest repo", sdk_ok, data.get("sdk_path", ""))
    _print_check("remote dir", remote_dir_ok, data.get("remote_dir_path", ""))
    _print_check("venv", venv_ok, data.get("venv_path") or data.get("venv", "unknown"))
    _print_check("west", west_ok, data.get("west") or "not found")
    if data.get("west_topdir"):
        _print_check("west topdir", west_topdir_ok, data["west_topdir"])
    if data.get("west_manifest_path"):
        _print_check("west manifest.path", west_manifest_ok, data["west_manifest_path"])
    _print_check("twister", twister_ok, data.get("twister", "unknown"))
    _print_check("rsync", rsync_ok, data.get("rsync") or "not found")

    if data.get("ssh_output"):
        print(data["ssh_output"])

    return (
        ok
        and workspace_ok
        and config_ok
        and manifest_ok
        and sdk_ok
        and remote_dir_ok
        and venv_ok
        and west_ok
        and west_topdir_ok
        and west_manifest_ok
        and twister_ok
        and rsync_ok
    )


def _fix_remote(
    host: str,
    remote_topdir: PurePosixPath,
    local: LocalWorkspace,
    *,
    update: bool,
) -> None:
    remote_config = remote_topdir / ".west" / "config"
    remote_sdk = remote_topdir / local.manifest_path
    remote_dir = remote_topdir / ".remote"

    _remote_run(
        host,
        "mkdir -p "
        + shlex.quote(str(remote_topdir / ".west"))
        + " "
        + shlex.quote(str(remote_dir)),
    )
    _print_check("workspace directories", True, f"{remote_topdir}/.west, {remote_dir}")

    if not _remote_test(host, f"test -f {shlex.quote(str(remote_config))}"):
        _rsync_file(local.west_config, host, remote_config)
        _print_check("west config", True, f"copied {remote_config}")
    else:
        _print_check("west config", True, "already exists")

    remote_manifest_path = _remote_manifest_path_from_config(host, remote_config)
    if remote_manifest_path != local.manifest_path:
        log.die(
            "remote manifest.path differs from local: "
            f"{remote_manifest_path.as_posix()} != {local.manifest_path.as_posix()}"
        )

    seeded_sdk = False
    if not _remote_test(host, f"test -d {shlex.quote(str(remote_sdk))}"):
        _remote_run(host, f"mkdir -p {shlex.quote(str(remote_sdk.parent))}")
        _rsync_tree(local.manifest_dir, host, remote_sdk, include_git=True)
        seeded_sdk = True
        _print_check("manifest repo", True, f"seeded {remote_sdk}")
    else:
        _print_check("manifest repo", True, "already exists")

    if seeded_sdk or update:
        _remote_git_reset_pull(host, remote_sdk)
        _print_check("git reset/pull", True, str(remote_sdk))

    if update:
        _remote_workspace_update(host, remote_topdir)
        _print_check("west update", True, str(remote_topdir))

    _remote_run(host, f"mkdir -p {shlex.quote(str(remote_dir))}")
    _print_check("remote dir", True, str(remote_dir))


def _probe_remote(host: str, remote_topdir: PurePosixPath) -> dict[str, str]:
    script = f"""
remote_topdir={shlex.quote(str(remote_topdir))}
remote_venv="{REMOTE_VENV_ACTIVATE}"
remote_config="$remote_topdir/.west/config"

kv() {{
    printf '%s=%s\\n' "$1" "$2"
}}

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

kv host "$(hostname 2>/dev/null || printf unknown)"
kv remote_topdir "$remote_topdir"
kv west_config_path "$remote_config"

if [ -d "$remote_topdir" ]; then
    kv workspace yes
    cd "$remote_topdir" || exit 1
else
    kv workspace no
fi

if [ -f "$remote_config" ]; then
    kv west_config yes
    config_manifest_path="$(manifest_path_from_config "$remote_config")"
    kv config_manifest_path "$config_manifest_path"
else
    kv west_config no
    config_manifest_path=""
    kv config_manifest_path ""
fi

sdk_path="$remote_topdir/$config_manifest_path"
remote_dir_path="$remote_topdir/.remote"
kv sdk_path "$sdk_path"
kv remote_dir_path "$remote_dir_path"

if [ -n "$config_manifest_path" ] && [ -d "$sdk_path" ]; then
    kv sdk yes
else
    kv sdk no
fi

if [ -d "$remote_dir_path" ]; then
    kv remote_dir yes
else
    kv remote_dir no
fi

kv venv_path "$remote_venv"
if [ -f "$remote_venv" ]; then
    kv venv yes
    . "$remote_venv"
else
    kv venv no
fi

west_path=$(command -v west 2>/dev/null || true)
if [ -n "$west_path" ] && [ -d "$remote_topdir" ]; then
    kv west "$west_path"
    kv west_topdir "$(west topdir 2>/dev/null || true)"
    kv west_manifest_path "$(west config manifest.path 2>/dev/null || true)"
    if west twister --help >/dev/null 2>&1; then
        kv twister yes
    else
        kv twister no
    fi
else
    kv west "$west_path"
    kv west_topdir ""
    kv west_manifest_path ""
    kv twister no
fi

rsync_path=$(command -v rsync 2>/dev/null || true)
kv rsync "$rsync_path"
"""
    try:
        result = subprocess.run(
            ["ssh", host, "sh", "-s"],
            check=False,
            input=script,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    except OSError as exc:
        return {"ssh_error": str(exc)}

    data = {"ssh_returncode": str(result.returncode)}
    for line in result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key] = value
    if result.returncode != 0:
        data["ssh_output"] = result.stdout.strip()
    return data


def _remote_git_reset_pull(host: str, remote_sdk: PurePosixPath) -> None:
    _remote_run(
        host,
        "set -e; "
        + f"git -C {shlex.quote(str(remote_sdk))} reset --hard; "
        + f"git -C {shlex.quote(str(remote_sdk))} pull",
    )


def _remote_workspace_update(host: str, remote_topdir: PurePosixPath) -> None:
    command = [
        "set -e",
        f"cd {shlex.quote(str(remote_topdir))}",
        f'. "{REMOTE_VENV_ACTIVATE}"',
        "west update",
        "west packages pip --install",
        "west sdk install --install-base ~/.zephyr/toolchains",
        "west blobs fetch",
    ]
    _remote_run(host, "; ".join(command))


def _remote_manifest_path_from_config(
    host: str,
    remote_config: PurePosixPath,
) -> PurePosixPath:
    cmd = (
        "awk '"
        "/^\\[manifest\\]/ { in_manifest = 1; next } "
        "/^\\[/ { in_manifest = 0 } "
        "in_manifest && /^[[:space:]]*path[[:space:]]*=/ { "
        "sub(/^[[:space:]]*path[[:space:]]*=[[:space:]]*/, \"\", $0); "
        "sub(/[[:space:]]*$/, \"\", $0); "
        "print $0; exit }"
        "' "
        + shlex.quote(str(remote_config))
    )
    output = _remote_output(host, cmd).strip()
    if not output:
        log.die(f"remote .west/config is missing manifest.path: {remote_config}")
    return _relative_sync_path(output)


def _remote_test(host: str, cmd: str) -> bool:
    return subprocess.call(["ssh", host, cmd]) == 0


def _remote_run(host: str, cmd: str) -> None:
    subprocess.check_call(["ssh", host, cmd])


def _remote_output(host: str, cmd: str) -> str:
    return subprocess.check_output(["ssh", host, cmd], text=True)


def _rsync_file(local_file: Path, host: str, remote_file: PurePosixPath) -> None:
    cmd = [
        "rsync",
        "-az",
        "--no-owner",
        "--no-group",
        str(local_file),
        f"{host}:{remote_file}",
    ]
    log.inf("syncing remote file: " + shlex.join(cmd))
    subprocess.check_call(cmd)


def _rsync_tree(
    local_dir: Path,
    host: str,
    remote_dir: PurePosixPath,
    *,
    include_git: bool,
) -> None:
    cmd = [
        "rsync",
        "-az",
        "--delete",
        "--no-owner",
        "--no-group",
        "--filter=:- .gitignore",
    ]
    if not include_git:
        cmd.append("--exclude=/.git/")
    cmd.extend(
        [
            f"{local_dir}/",
            f"{host}:{remote_dir}/",
        ]
    )
    log.inf("syncing remote tree: " + shlex.join(cmd))
    subprocess.check_call(cmd)


def _print_section(name: str) -> None:
    print(f"\n[{name}]")


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


def _local_workspace() -> LocalWorkspace:
    topdir = _local_topdir()
    west_config = topdir / ".west" / "config"
    if west_config.is_file():
        config_manifest_path = _manifest_path_from_config(west_config)
    else:
        config_manifest_path = None
    manifest_path = _manifest_path()
    if config_manifest_path is not None and config_manifest_path != manifest_path:
        log.die(
            "local .west/config manifest.path differs from west config: "
            f"{config_manifest_path.as_posix()} != {manifest_path.as_posix()}"
        )
    return LocalWorkspace(
        topdir=topdir,
        west_config=west_config,
        manifest_path=manifest_path,
        manifest_dir=topdir / Path(manifest_path.as_posix()),
        rsync=shutil.which("rsync"),
    )


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


def _manifest_path_from_config(config_path: Path) -> PurePosixPath:
    parser = configparser.ConfigParser()
    parser.read(config_path)
    if not parser.has_option("manifest", "path"):
        log.die(f"local .west/config is missing manifest.path: {config_path}")
    return _relative_sync_path(parser.get("manifest", "path"))
