# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""West extension command for creating LittleFS images."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from west import log
from west.commands import WestCommand


DEFAULT_BUILD_DIR = "build"
DEFAULT_PARTITION = "extra"


class LittleFSConfig:
    size: int
    block_size: int
    read_size: int
    prog_size: int
    cache_size: int
    lookahead_size: int
    block_cycles: int
    partition_offset: int | None = None

    def __init__(
        self,
        *,
        size: int,
        block_size: int,
        read_size: int,
        prog_size: int,
        cache_size: int,
        lookahead_size: int,
        block_cycles: int,
        partition_offset: int | None = None,
    ) -> None:
        self.size = size
        self.block_size = block_size
        self.read_size = read_size
        self.prog_size = prog_size
        self.cache_size = cache_size
        self.lookahead_size = lookahead_size
        self.block_cycles = block_cycles
        self.partition_offset = partition_offset


class MkLfs(WestCommand):
    """Create a LittleFS image from host files."""

    def __init__(self):
        super().__init__(
            "mklfs",
            "create a LittleFS image",
            "Create a LittleFS image from host files, optionally using a Zephyr build directory.",
            accepts_unknown_args=False,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog="""\
Examples:
  west mklfs -d build.meshbus_llext_service_hw -o /tmp/extra.bin \\
    --file build-blinky-service-hw/blinky.mbs:/svcs/blinky.mbs

  pyocd commander -t nrf54l -M halt \\
    -c "load /tmp/extra.bin 0x146000" -c reset -c exit
""",
        )
        parser.add_argument(
            "-d",
            "--build-dir",
            default=DEFAULT_BUILD_DIR,
            help=f"Zephyr build directory used to infer partition and filesystem settings (default: {DEFAULT_BUILD_DIR})",
        )
        parser.add_argument(
            "-p",
            "--partition",
            default=DEFAULT_PARTITION,
            help=f"fstab node or fixed partition label to use from zephyr.dts (default: {DEFAULT_PARTITION})",
        )
        parser.add_argument(
            "-o",
            "--output",
            required=True,
            help="Output LittleFS image path",
        )
        parser.add_argument(
            "--file",
            action="append",
            default=[],
            metavar="SRC:DST",
            help="Copy host file SRC into image path DST; may be repeated",
        )
        parser.add_argument(
            "--source-dir",
            action="append",
            default=[],
            metavar="SRC[:DST]",
            help="Copy all files under SRC into DST in the image; DST defaults to /",
        )
        parser.add_argument("--size", type=_parse_int, help="Image size in bytes")
        parser.add_argument("--block-size", type=_parse_int, help="LittleFS block size")
        parser.add_argument("--read-size", type=_parse_int, help="LittleFS read size")
        parser.add_argument("--prog-size", type=_parse_int, help="LittleFS program size")
        parser.add_argument("--cache-size", type=_parse_int, help="LittleFS cache size")
        parser.add_argument("--lookahead-size", type=_parse_int, help="LittleFS lookahead size")
        parser.add_argument("--block-cycles", type=int, help="LittleFS block cycles")
        return parser

    def do_run(self, args, unknown_args):
        if unknown_args:
            log.die(f"unexpected arguments: {' '.join(unknown_args)}")

        cfg = _load_config(args)
        output = Path(args.output).resolve()
        file_entries = [_parse_file_mapping(item) for item in args.file]
        dir_entries = [_parse_dir_mapping(item) for item in args.source_dir]

        if not file_entries and not dir_entries:
            log.die("nothing to add: pass at least one --file or --source-dir")

        _create_image(output, cfg, file_entries, dir_entries)

        log.inf(f"LittleFS image written: {output}")
        log.inf(f"size={cfg.size} block_size={cfg.block_size} blocks={cfg.size // cfg.block_size}")
        if cfg.partition_offset is not None:
            log.inf(f"partition offset: 0x{cfg.partition_offset:x}")
        return 0


def _load_config(args) -> LittleFSConfig:
    if _has_manual_config(args):
        inferred = LittleFSConfig(
            size=args.size,
            block_size=args.block_size,
            read_size=args.read_size,
            prog_size=args.prog_size,
            cache_size=args.cache_size,
            lookahead_size=args.lookahead_size,
            block_cycles=args.block_cycles,
        )
    else:
        inferred = _infer_config(Path(args.build_dir), args.partition)

    return LittleFSConfig(
        size=args.size or inferred.size,
        block_size=args.block_size or inferred.block_size,
        read_size=args.read_size or inferred.read_size,
        prog_size=args.prog_size or inferred.prog_size,
        cache_size=args.cache_size or inferred.cache_size,
        lookahead_size=args.lookahead_size or inferred.lookahead_size,
        block_cycles=args.block_cycles if args.block_cycles is not None else inferred.block_cycles,
        partition_offset=inferred.partition_offset,
    )


def _has_manual_config(args) -> bool:
    fields = (
        args.size,
        args.block_size,
        args.read_size,
        args.prog_size,
        args.cache_size,
        args.lookahead_size,
        args.block_cycles,
    )
    return all(value is not None for value in fields)


def _infer_config(build_dir: Path, partition_name: str) -> LittleFSConfig:
    dts = build_dir / "zephyr" / "zephyr.dts"
    if not dts.is_file():
        log.die(
            f"cannot infer LittleFS settings: missing {dts}; "
            "pass --size/--block-size/--read-size/--prog-size/--cache-size/--lookahead-size manually"
        )

    text = dts.read_text(encoding="utf-8")
    flash_block_size = _read_flash_int(text, "erase-block-size")
    fstab_block = _find_labeled_block(text, partition_name)

    partition_label = partition_name
    if "zephyr,fstab,littlefs" in fstab_block:
        partition_label = _read_partition_ref(fstab_block)
        read_size = _read_int_prop(fstab_block, "read-size")
        prog_size = _read_int_prop(fstab_block, "prog-size")
        cache_size = _read_int_prop(fstab_block, "cache-size")
        lookahead_size = _read_int_prop(fstab_block, "lookahead-size")
        block_cycles = _read_int_prop(fstab_block, "block-cycles")
    else:
        read_size = 16
        prog_size = 16
        cache_size = 64
        lookahead_size = 32
        block_cycles = 512

    partition_block = _find_labeled_block(text, partition_label)
    offset, size = _read_reg(partition_block)
    return LittleFSConfig(
        size=size,
        block_size=flash_block_size,
        read_size=read_size,
        prog_size=prog_size,
        cache_size=cache_size,
        lookahead_size=lookahead_size,
        block_cycles=block_cycles,
        partition_offset=offset,
    )


def _find_labeled_block(text: str, label: str) -> str:
    pattern = re.compile(rf"^\s*{re.escape(label)}:\s+[^\{{]+?\{{", re.MULTILINE)
    match = pattern.search(text)
    if not match:
        log.die(f"unable to find DTS label: {label}")

    depth = 0
    start = match.start()
    for index in range(match.start(), len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]

    log.die(f"unterminated DTS block for label: {label}")


def _read_partition_ref(block: str) -> str:
    match = re.search(r"partition\s*=\s*<\s*&([A-Za-z0-9_]+)\s*>", block)
    if not match:
        log.die("LittleFS fstab node is missing partition reference")
    return match.group(1)


def _read_reg(block: str) -> tuple[int, int]:
    match = re.search(r"reg\s*=\s*<\s*([^>]+?)\s*>", block, flags=re.DOTALL)
    if not match:
        log.die("partition node is missing reg property")
    values = [_parse_int(token) for token in match.group(1).split() if not token.startswith("/*")]
    if len(values) < 2:
        log.die("partition reg property must contain offset and size")
    return values[0], values[1]


def _read_flash_int(text: str, name: str) -> int:
    match = re.search(rf"{re.escape(name)}\s*=\s*<\s*([^>]+?)\s*>", text)
    if not match:
        log.die(f"unable to infer flash {name} from zephyr.dts")
    return _parse_int(match.group(1).split()[0])


def _read_int_prop(block: str, name: str) -> int:
    match = re.search(rf"{re.escape(name)}\s*=\s*<\s*([^>]+?)\s*>", block)
    if not match:
        log.die(f"LittleFS fstab node is missing {name}")
    return _parse_int(match.group(1).split()[0])


def _parse_file_mapping(value: str) -> tuple[Path, str]:
    src, dst = _split_mapping(value, "--file")
    src_path = Path(src)
    if not src_path.is_file():
        log.die(f"--file source is not a file: {src_path}")
    return src_path, _normalize_image_path(dst)


def _parse_dir_mapping(value: str) -> tuple[Path, str]:
    if ":" in value:
        src, dst = _split_mapping(value, "--source-dir")
    else:
        src, dst = value, "/"
    src_path = Path(src)
    if not src_path.is_dir():
        log.die(f"--source-dir source is not a directory: {src_path}")
    return src_path, _normalize_image_path(dst)


def _split_mapping(value: str, option: str) -> tuple[str, str]:
    if ":" not in value:
        log.die(f"{option} must use SRC:DST syntax")
    src, dst = value.split(":", 1)
    if not src or not dst:
        log.die(f"{option} must use non-empty SRC:DST syntax")
    return src, dst


def _normalize_image_path(path: str) -> str:
    normalized = "/" + path.lstrip("/")
    parts = [part for part in normalized.split("/") if part]
    if any(part in {".", ".."} for part in parts):
        log.die(f"invalid image path: {path}")
    return "/" + "/".join(parts) if parts else "/"


def _create_image(
    output: Path,
    cfg: LittleFSConfig,
    file_entries: list[tuple[Path, str]],
    dir_entries: list[tuple[Path, str]],
) -> None:
    try:
        from littlefs import LittleFS
        from littlefs.context import UserContextFile
    except ImportError:
        log.die(
            "west mklfs requires littlefs-python; install it with "
            "`python -m pip install littlefs-python` or add it to PYTHONPATH"
        )

    if cfg.size <= 0 or cfg.block_size <= 0:
        log.die("size and block-size must be positive")
    if cfg.size % cfg.block_size != 0:
        log.die("size must be a multiple of block-size")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"\xff" * cfg.size)

    ctx = UserContextFile(str(output), create=False)
    fs = LittleFS(
        context=ctx,
        mount=False,
        read_size=cfg.read_size,
        prog_size=cfg.prog_size,
        block_size=cfg.block_size,
        block_count=cfg.size // cfg.block_size,
        cache_size=cfg.cache_size,
        lookahead_size=cfg.lookahead_size,
        block_cycles=cfg.block_cycles,
    )

    try:
        fs.format()
        fs.mount()
        for src, dst in file_entries:
            _write_file(fs, src, dst)
        for src_dir, dst_dir in dir_entries:
            _write_dir(fs, src_dir, dst_dir)
        fs.unmount()
    finally:
        ctx.close()


def _write_dir(fs, src_dir: Path, dst_dir: str) -> None:
    for src in sorted(path for path in src_dir.rglob("*") if path.is_file()):
        rel = src.relative_to(src_dir).as_posix()
        dst = _join_image_path(dst_dir, rel)
        _write_file(fs, src, dst)


def _write_file(fs, src: Path, dst: str) -> None:
    parent = str(Path(dst).parent).replace("\\", "/")
    if parent and parent != "/":
        fs.makedirs(parent, exist_ok=True)
    with fs.open(dst, "wb") as outfile:
        outfile.write(src.read_bytes())
    log.inf(f"added {src} -> {dst}")


def _join_image_path(base: str, rel: str) -> str:
    return _normalize_image_path(base.rstrip("/") + "/" + rel)


def _parse_int(value: str) -> int:
    return int(str(value).strip(), 0)
