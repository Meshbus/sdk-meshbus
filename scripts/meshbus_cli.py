# SPDX-License-Identifier: Apache-2.0
"""Resolve a user CLI, with opt-in incremental builds for the west adapter."""
import os
from pathlib import Path
import shutil
import subprocess
import sys


def cli_command(*, require_explicit=False, auto_build=False):
    override = os.environ.get("MESHBUS_CLI")
    if override:
        if require_explicit and not Path(override).is_absolute():
            raise RuntimeError(
                "production format operations require MESHBUS_CLI to be an "
                "absolute executable path"
            )
        executable = shutil.which(override)
        if executable:
            return [executable]
        raise RuntimeError("MESHBUS_CLI does not name an executable: " + override)
    if require_explicit:
        raise RuntimeError(
            "production format operations require MESHBUS_CLI to name the "
            "absolute CI-built executable"
        )
    root = Path(__file__).resolve().parents[2]
    target = Path(os.environ.get("CARGO_TARGET_DIR", root / "build-meshbus-cli"))
    if not target.is_absolute():
        target = root / target
    if auto_build:
        # Cargo owns freshness tracking, including Rust inputs, build.rs and
        # lockfile changes. Always check before using a local executable.
        cargo = os.environ.get("CARGO", "cargo")
        manifest = Path(__file__).resolve().parent / "meshbus/Cargo.toml"
        try:
            subprocess.run(
                [cargo, "build", "--locked", "--release", "--manifest-path", str(manifest),
                 "--target-dir", str(target)],
                cwd=root, stdout=sys.stderr, check=True,
            )
        except FileNotFoundError as error:
            raise RuntimeError(
                "Cargo is required to build the local meshbus CLI. Install Rust/Cargo "
                "or set MESHBUS_CLI to an existing executable."
            ) from error
        except subprocess.CalledProcessError as error:
            raise RuntimeError(
                f"meshbus CLI build failed (exit {error.returncode}); CLI was not started"
            ) from error
    else:
        installed = shutil.which("meshbus")
        if installed:
            return [installed]
    local = target / "release" / ("meshbus.exe" if os.name == "nt" else "meshbus")
    if local.is_file() and os.access(local, os.X_OK):
        return [str(local.resolve())]
    if auto_build:
        raise RuntimeError(f"Cargo completed but the meshbus CLI is missing: {local}")
    raise RuntimeError(
        "meshbus CLI not found. Install meshbus on PATH or set MESHBUS_CLI to its "
        "executable. Developers can build it with 'west meshbus --version'."
    )
