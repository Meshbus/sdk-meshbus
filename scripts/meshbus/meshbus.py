# SPDX-License-Identifier: Apache-2.0
"""West requires Python extensions; all Meshbus behavior lives in Rust."""
import argparse
from pathlib import Path
import subprocess
import sys

from west.commands import WestCommand

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from meshbus_cli import cli_command


class Meshbus(WestCommand):
    def __init__(self):
        super().__init__("meshbus", "Meshbus Rust utilities", "Build the local Rust meshbus CLI as needed and run it.", accepts_unknown_args=True)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, add_help=False)
        parser.add_argument("arguments", nargs=argparse.REMAINDER)
        return parser

    def do_run(self, args, unknown_args):
        try:
            result = subprocess.run([*cli_command(auto_build=True), *unknown_args, *args.arguments], check=False)
        except (OSError, RuntimeError) as error:
            self.die(str(error))
        if result.returncode:
            raise SystemExit(result.returncode)
