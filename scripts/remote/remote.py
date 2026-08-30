# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""West extension command for remote helper workflows."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from west import log
from west.commands import WestCommand

REMOTE_COMMAND_DIR = Path(__file__).resolve().parent
if str(REMOTE_COMMAND_DIR) not in sys.path:
    sys.path.insert(0, str(REMOTE_COMMAND_DIR))

from remote_build import BuildCommand
from remote_gdb import GdbCommand
from remote_flash import FlashCommand
from remote_doctor import DoctorCommand
from remote_serial import SerialCommand
from remote_session import SessionCommand
from remote_twister import TwisterCommand


HELP_EPILOG = """\
Examples:
  west remote doctor build-host.example.com:/srv/zephyr-workspace
  west remote session build-host.example.com:/srv/zephyr-workspace abc.test --source /path/to/sdk-checkout
  west remote build build-host.example.com:/srv/zephyr-workspace abc.test --source /path/to/sdk-checkout -- -b qemu_x86 tests/subsys/zui
  west remote twister build-host.example.com:/srv/zephyr-workspace abc.test --source /path/to/sdk-checkout -- -T tests/subsys/zui -p qemu_x86
  west remote flash -s build-host.example.com -d /srv/zephyr-workspace/build/app -r pyocd
  west remote gdb build-host.example.com --target nrf54l
  west remote serial build-host.example.com /dev/tty.usbmodem...

See scripts/remote/README.md for workspace modes and authorization boundaries.
"""


class Remote(WestCommand):
    """Run remote helper subcommands."""

    def __init__(self):
        super().__init__(
            "remote",
            "remote helper commands",
            "Remote helper workflows for local hardware and remote compute.",
            accepts_unknown_args=True,
        )
        self._build = BuildCommand()
        self._doctor = DoctorCommand()
        self._flash = FlashCommand()
        self._gdb = GdbCommand()
        self._serial = SerialCommand()
        self._session = SessionCommand()
        self._twister = TwisterCommand()

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=HELP_EPILOG,
        )
        subparsers = parser.add_subparsers(dest="subcommand", required=True)
        self._build.add_parser(subparsers)
        self._doctor.add_parser(subparsers)
        self._flash.add_parser(subparsers)
        self._gdb.add_parser(subparsers)
        self._serial.add_parser(subparsers)
        self._session.add_parser(subparsers)
        self._twister.add_parser(subparsers)
        return parser

    def do_run(self, args, unknown_args):
        if unknown_args and not getattr(args, "pass_unknown_args", False):
            log.die("unexpected arguments: " + " ".join(unknown_args))
        if getattr(args, "pass_unknown_args", False):
            return args.handler(args, unknown_args)
        return args.handler(args)
