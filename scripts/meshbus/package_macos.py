# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Build and package the standalone Rust ``meshbus`` executable for macOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]


def _command_output(command: list[str], *, cwd: Path | None = None) -> str:
    return subprocess.run(
        command, cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


def _git_identity(root: Path) -> dict[str, object]:
    return {
        "revision": _command_output(["git", "rev-parse", "HEAD"], cwd=root),
        "dirty": bool(
            _command_output(
                ["git", "status", "--porcelain", "--untracked-files=all"],
                cwd=root,
            )
        ),
    }


def _find_proto_root(explicit: str | None) -> Path:
    root = (
        Path(explicit).expanduser().resolve()
        if explicit
        else Path(
            _command_output(["west", "list", "meshbus-protobufs", "-f", "{abspath}"])
        ).resolve()
    )
    if not (root / "meshbus").is_dir():
        raise RuntimeError(f"invalid meshbus-protobufs root: {root}")
    return root


def _cargo() -> str:
    executable = shutil.which("cargo")
    if executable:
        return executable
    rustup_cargo = Path.home() / ".cargo/bin/cargo"
    if rustup_cargo.is_file():
        return str(rustup_cargo)
    raise RuntimeError("cargo is required; install Rust from https://rustup.rs")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proto-root", help="path to meshbus-protobufs")
    parser.add_argument(
        "--output-root",
        default=str(REPO_ROOT / "build/meshbus-cli"),
        help="generated Cargo and distribution root",
    )
    parser.add_argument(
        "--development",
        action="store_true",
        help="allow dirty sdk-meshbus or schema repositories",
    )
    parser.add_argument(
        "--codesign-identity",
        help="Developer ID Application identity; default is ad-hoc signing",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    if platform.system() != "Darwin":
        raise RuntimeError("this package command must run on macOS")

    proto_root = _find_proto_root(args.proto_root)
    source = _git_identity(REPO_ROOT)
    schema = _git_identity(proto_root)
    if not args.development and (source["dirty"] or schema["dirty"]):
        raise RuntimeError(
            "release packaging requires clean sdk-meshbus and meshbus-protobufs "
            "repositories; pass --development for a local snapshot"
        )

    output_root = Path(args.output_root).expanduser().resolve()
    cargo_target = output_root / "cargo-target"
    dist_root = output_root / "dist"
    environment = os.environ.copy()
    environment["CARGO_TARGET_DIR"] = str(cargo_target)
    environment["MESHBUS_PROTO_ROOT"] = str(proto_root)
    subprocess.run(
        [
            _cargo(),
            "build",
            "--locked",
            "--release",
            "--manifest-path",
            str(SCRIPT_DIR / "Cargo.toml"),
        ],
        cwd=REPO_ROOT,
        env=environment,
        check=True,
    )

    dist_root.mkdir(parents=True, exist_ok=True)
    executable = dist_root / "meshbus"
    shutil.copy2(cargo_target / "release/meshbus", executable)
    subprocess.run(
        [
            "codesign",
            "--force",
            "--options",
            "runtime",
            "--sign",
            args.codesign_identity or "-",
            str(executable),
        ],
        check=True,
    )

    descriptors = sorted(
        (cargo_target / "release/build").glob("meshbus-cli-*/out/meshbus.pb"),
        key=lambda path: path.stat().st_mtime,
    )
    if not descriptors:
        raise RuntimeError("Cargo build did not produce the embedded schema descriptor")
    descriptor = descriptors[-1]
    manifest = {
        "artifact": executable.name,
        "architecture": platform.machine(),
        "backend": "Rust probe-rs",
        "development": bool(source["dirty"] or schema["dirty"]),
        "sha256": _sha256(executable),
        "size": executable.stat().st_size,
        "source": source,
        "schema": {
            **schema,
            "descriptor_sha256": _sha256(descriptor),
        },
    }
    manifest_path = dist_root / "meshbus.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(executable)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
