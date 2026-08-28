"""Public Meshbus command-line interface."""

from __future__ import annotations

import argparse

from llext import _add_build_arguments, _help_epilog, _run_build


def _run_llext(args, unknown_args):
    return _run_build(args, unknown_args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="meshbus",
        description="Meshbus developer utilities.",
    )
    subparsers = parser.add_subparsers(dest="meshbus_command", required=True)
    llext_parser = subparsers.add_parser(
        "llext",
        help="build a Meshbus LLEXT package",
        description="Build one Meshbus .mba or .mbs package from a released EDK.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=_help_epilog(command="meshbus llext"),
    )
    _add_build_arguments(llext_parser)
    llext_parser.set_defaults(meshbus_handler=_run_llext)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args, unknown_args = parser.parse_known_args(argv)
    return args.meshbus_handler(args, unknown_args)
