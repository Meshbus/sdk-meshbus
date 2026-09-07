# SPDX-License-Identifier: Apache-2.0
"""Locate an existing user CLI. Resolution never builds or downloads anything."""
import os
from pathlib import Path
import shutil


def cli_command(*, require_explicit=False):
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
    installed = shutil.which("meshbus")
    if installed:
        return [installed]
    root = Path(__file__).resolve().parents[2]
    target = Path(os.environ.get("CARGO_TARGET_DIR", root / "build/meshbus-cli/cargo"))
    local = target / "release" / ("meshbus.exe" if os.name == "nt" else "meshbus")
    if local.is_file() and os.access(local, os.X_OK):
        return [str(local.resolve())]
    raise RuntimeError(
        "meshbus CLI not found. Install meshbus on PATH or set MESHBUS_CLI to its "
        "executable. Developers can build it with 'west release cli --output "
        "build/client-candidate'. No automatic Rust build is performed."
    )
