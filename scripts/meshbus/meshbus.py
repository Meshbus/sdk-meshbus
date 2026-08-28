# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Unified West extension command for Meshbus host utilities."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from west.commands import WestCommand


SCRIPT_DIR = Path(__file__).resolve().parent
LLEXT_DIR = SCRIPT_DIR.parent / "llext"
FIRMWARE_DIR = SCRIPT_DIR.parent / "firmware"
for directory in (SCRIPT_DIR, LLEXT_DIR, FIRMWARE_DIR):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))

from firmware import _add_firmware_arguments, _run_firmware  # noqa: E402
from edk import _add_edk_arguments, _run_edk  # noqa: E402
from llext import _add_build_arguments, _help_epilog, _run_build  # noqa: E402
RUST_MANIFEST = SCRIPT_DIR / "Cargo.toml"
RUST_RELEASE = SCRIPT_DIR.parents[1] / "build/meshbus-cli/cargo-target/release/meshbus"


def _rust_command(subcommand, unknown_args):
    """Run the Rust CLI while keeping west's required Python adapter minimal."""

    override = os.environ.get("MESHBUS_CLI")
    executable = Path(override).expanduser() if override else RUST_RELEASE
    if executable.is_file():
        command = [str(executable), subcommand, *unknown_args]
    else:
        cargo = os.environ.get("CARGO") or shutil.which("cargo")
        if cargo is None:
            rustup_cargo = Path.home() / ".cargo/bin/cargo"
            cargo = str(rustup_cargo) if rustup_cargo.is_file() else "cargo"
        command = [
            cargo,
            "run",
            "--quiet",
            "--release",
            "--manifest-path",
            str(RUST_MANIFEST),
            "--",
            subcommand,
            *unknown_args,
        ]
    try:
        returncode = subprocess.run(command, check=False).returncode
    except FileNotFoundError as exc:
        from west import log

        log.die(
            f"unable to launch Rust Meshbus CLI ({exc}); build {RUST_MANIFEST} "
            "or set MESHBUS_CLI"
        )
    if returncode != 0:
        raise SystemExit(returncode)
    return 0


class Meshbus(WestCommand):
    """Provide namespaced Meshbus packaging and device commands."""

    def __init__(self):
        super().__init__(
            "meshbus",
            "Meshbus packaging and device utilities",
            (
                "Package Meshbus artifacts or connect to a device over UART "
                "MCUmgr. Product firmware builds belong to the application repository."
            ),
            accepts_unknown_args=True,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        subparsers = parser.add_subparsers(dest="meshbus_command", required=True)

        llext_parser = subparsers.add_parser(
            "llext",
            help="build a Meshbus LLEXT package",
            description="Build one Meshbus .mba or .mbs package with metadata injection.",
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog=_help_epilog(command="west meshbus llext"),
        )
        _add_build_arguments(llext_parser)
        llext_parser.set_defaults(meshbus_handler=_run_build)

        edk_parser = subparsers.add_parser(
            "edk",
            help="create a public Meshbus LLEXT EDK",
            description="Generate, prune, validate, and package an EDK from a Meshbus host build.",
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        _add_edk_arguments(edk_parser)
        edk_parser.set_defaults(meshbus_handler=_run_edk)

        firmware_parser = subparsers.add_parser(
            "firmware",
            help="package, program, and update Meshbus firmware",
            description="Firmware package, debug-probe, and remote delta operations.",
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        firmware_subparsers = firmware_parser.add_subparsers(
            dest="firmware_command", required=True
        )
        package_parser = firmware_subparsers.add_parser(
            "package",
            help="create and verify signed delta packages",
            description="Create and verify signed Meshbus delta packages.",
        )
        _add_firmware_arguments(package_parser)
        package_parser.set_defaults(meshbus_handler=_run_firmware)
        for command_name, command_help in (
            ("delta", "transfer a delta package over Meshbus Management"),
            ("probes", "list connected debug probes"),
            ("inspect", "inspect a debug-probe programming image set"),
            ("flash", "program firmware through a debug probe"),
        ):
            forwarding_parser = firmware_subparsers.add_parser(
                command_name,
                add_help=False,
                help=command_help,
            )
            forwarding_parser.set_defaults(
                meshbus_handler=lambda args, unknown, name=command_name: _rust_command(
                    "firmware", [name, *unknown]
                )
            )

        connect_parser = subparsers.add_parser(
            "connect",
            add_help=False,
            help="connect to Meshbus UART MCUmgr with live device logs",
            description=(
                "Open a Base64 UART MCUmgr session while preserving ordinary "
                "device logging on the same serial port."
            ),
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        connect_parser.set_defaults(
            meshbus_handler=lambda args, unknown: _rust_command("connect", unknown)
        )

        return parser

    def do_run(self, args, unknown_args):
        return args.meshbus_handler(args, unknown_args)
