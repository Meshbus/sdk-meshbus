# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Expose hardware attached to this machine to a separate development host."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from west import log
from west.commands import WestCommand

REMOTE_COMMAND_DIR = Path(__file__).resolve().parent
if str(REMOTE_COMMAND_DIR) not in sys.path:
    sys.path.insert(0, str(REMOTE_COMMAND_DIR))

from remote_gdb import GdbCommand
from remote_serial import SerialCommand


HELP_EPILOG = """\
Examples:
  west remote gdb dev-host.example.com --target nrf54l --probe <probe-id>
  west remote serial dev-host.example.com /dev/tty.usbmodem...

Run these commands on the machine with the USB devices. The SSH destination
is the development host that will consume the forwarded endpoints, regardless
of which machine runs Codex. Use separate processes for multiple devices.

See scripts/remote/README.md for host roles and authorization boundaries.
"""


class Remote(WestCommand):
    """Forward GDB and serial access to the development host."""

    def __init__(self):
        super().__init__(
            "remote",
            "forward USB debug probes and serial ports to a development host",
            "Expose this machine's GDB and serial endpoints on an SSH development host.",
            accepts_unknown_args=True,
        )
        self._gdb = GdbCommand()
        self._serial = SerialCommand()

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=HELP_EPILOG,
        )
        subparsers = parser.add_subparsers(dest="subcommand", required=True)
        self._gdb.add_parser(subparsers)
        self._serial.add_parser(subparsers)
        return parser

    def do_run(self, args, unknown_args):
        if unknown_args:
            log.die("unexpected arguments: " + " ".join(unknown_args))
        return args.handler(args)
