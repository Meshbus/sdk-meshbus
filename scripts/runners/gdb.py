# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Zephyr runner that flashes through an already-running GDB server."""

from __future__ import annotations

import argparse
import shlex

from runners.core import RunnerCaps, RunnerConfig, ZephyrBinaryRunner


DEFAULT_GDB_HOST = "127.0.0.1"
DEFAULT_GDB_PORT = 3333


class GdbBinaryRunner(ZephyrBinaryRunner):
    """Use GDB remote protocol for flashing a Zephyr ELF."""

    def __init__(
        self,
        cfg: RunnerConfig,
        *,
        gdb_host: str = DEFAULT_GDB_HOST,
        gdb_port: int = DEFAULT_GDB_PORT,
        reset: bool = True,
        connect_timeout: int = 30,
        monitor_before_load: list[str] | None = None,
        monitor_after_load: list[str] | None = None,
        dry_run: bool = False,
    ):
        super().__init__(cfg)
        self.gdb = cfg.gdb
        self.elf_file = cfg.elf_file
        self.gdb_host = gdb_host
        self.gdb_port = gdb_port
        self.reset = reset
        self.connect_timeout = connect_timeout
        self.monitor_before_load = monitor_before_load or ["halt"]
        self.monitor_after_load = monitor_after_load or ["reset"]
        self.dry_run = dry_run

        if self.gdb is None:
            raise ValueError("GDB runner requires CMAKE_GDB in the build configuration")
        if self.elf_file is None:
            raise ValueError("GDB runner requires an ELF file in the build directory")

    @classmethod
    def name(cls):
        return "gdb"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, reset=True, dry_run=True)

    @classmethod
    def do_add_parser(cls, parser: argparse.ArgumentParser):
        parser.add_argument(
            "--gdb-host",
            default=DEFAULT_GDB_HOST,
            help=f"GDB server host (default: {DEFAULT_GDB_HOST})",
        )
        parser.add_argument(
            "--gdb-port",
            type=int,
            default=DEFAULT_GDB_PORT,
            help=f"GDB server port (default: {DEFAULT_GDB_PORT})",
        )
        parser.add_argument(
            "--connect-timeout",
            type=int,
            default=30,
            help="GDB remote connection timeout in seconds (default: 30)",
        )
        parser.add_argument(
            "--monitor-before-load",
            action="append",
            default=None,
            metavar="COMMAND",
            help=(
                "GDB monitor command before load; may be repeated "
                "(default: halt)"
            ),
        )
        parser.add_argument(
            "--monitor-after-load",
            action="append",
            default=None,
            metavar="COMMAND",
            help=(
                "GDB monitor command after load; may be repeated "
                "(default: reset when reset is enabled)"
            ),
        )

    @classmethod
    def do_create(cls, cfg: RunnerConfig, args: argparse.Namespace):
        reset = True if args.reset is None else args.reset
        monitor_after_load = args.monitor_after_load
        if monitor_after_load is None and not reset:
            monitor_after_load = []
        return GdbBinaryRunner(
            cfg,
            gdb_host=args.gdb_host,
            gdb_port=args.gdb_port,
            reset=reset,
            connect_timeout=args.connect_timeout,
            monitor_before_load=args.monitor_before_load,
            monitor_after_load=monitor_after_load,
            dry_run=args.dry_run,
        )

    def do_run(self, command: str, **kwargs):
        if command != "flash":
            raise AssertionError(command)
        self.do_flash(**kwargs)

    def do_flash(self, **kwargs):
        endpoint = f"{self.gdb_host}:{self.gdb_port}"
        cmd = [
            self.gdb,
            "--quiet",
            "--batch",
            self.elf_file,
            "-ex",
            "set confirm off",
            "-ex",
            f"set remotetimeout {self.connect_timeout}",
            "-ex",
            f"target extended-remote {endpoint}",
        ]
        for monitor in self.monitor_before_load:
            cmd.extend(["-ex", f"monitor {monitor}"])
        cmd.extend(["-ex", "load"])
        for monitor in self.monitor_after_load:
            cmd.extend(["-ex", f"monitor {monitor}"])
        cmd.extend([
            "-ex",
            "detach",
            "-ex",
            "quit",
        ])

        self.logger.info(f"Flashing through GDB remote endpoint: {endpoint}")
        if self.dry_run:
            self.logger.info("GDB command: " + shlex.join(cmd))
            return
        self.require(self.gdb)
        self.check_call(cmd)
