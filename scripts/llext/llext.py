# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""West extension command for Meshbus LLEXT builds."""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path
from typing import List

import yaml

if __name__ != "__main__":
    try:
        from west import log
        from west.commands import WestCommand
    except ImportError:
        log = None
        WestCommand = None
else:
    log = None
    WestCommand = None

if log is None:  # Standalone packager copied into a public release EDK.
    class _StandaloneLog:
        @staticmethod
        def inf(message):
            print(message)

        @staticmethod
        def dbg(_message):
            pass

        @staticmethod
        def die(message):
            raise SystemExit(f"error: {message}")

    class WestCommand:  # type: ignore[no-redef]
        def __init__(self, *_args, **_kwargs):
            pass

    log = _StandaloneLog()

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from elf import ElfError, ElfImage  # noqa: E402
from edk_sdk import EdkSdkError, load_release_manifest  # noqa: E402

METADATA_SECTION = ".meshbus.llext.meta"
SERVICE_SUFFIX = ".mbs"
APP_SUFFIX = ".mba"
EDK_DIRNAME = "llext-edk"
EDK_TARBALL = "llext-edk.tar.xz"
MANAGED_LLEXT_SUBDIR = Path("zephyr") / "llext"
LLEXT_BUILD_SUBDIR = "build"
ELF_SHF_ALLOC = 0x2
ELF_SHT_SYMTAB = 2
ELF_SHT_DYNSYM = 11
ELF_SHT_REL = 9
ELF_SHT_RELA = 4
LLEXT_HEAP_BASE_OVERHEAD = 1024
LLEXT_HEAP_SECTION_OVERHEAD = 64
LLEXT_HEAP_SYMBOL_OVERHEAD = 16
LLEXT_HEAP_RELOC_OVERHEAD = 8
LLEXT_HEAP_ALIGN = 8
LLEXT_PAGE_SIZE_ARM_MPU = 32
LLEXT_MEM_TEXT = 0
LLEXT_MEM_DATA = 1
LLEXT_MEM_RODATA = 2
LLEXT_MEM_BSS = 3
LLEXT_MEM_EXPORT = 4
LLEXT_MEM_SYMTAB = 5
LLEXT_MEM_STRTAB = 6
LLEXT_MEM_SHSTRTAB = 7
LLEXT_DEFAULT_CFLAGS = ("-Os", "-g0", "-fno-merge-constants")


class Llext(WestCommand):
    """Build one Meshbus LLEXT extension package."""

    def __init__(self):
        super().__init__(
            "llext",
            "build a Meshbus LLEXT package",
            "Build one Meshbus LLEXT extension package with metadata injection.",
            accepts_unknown_args=True,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
            epilog=_help_epilog(),
        )
        return _add_build_arguments(parser)

    def do_run(self, args, unknown_args):
        return _run_build(args, unknown_args)


def _help_epilog(command: str = "meshbus llext") -> str:
    return f"""\
Examples:
  west build -p auto -d build.meshbus_llext_service_hw \\
    -b idea_mesh_tracker_c2/nrf54l15/cpuapp \\
    sdk-meshbus/samples/subsys/meshbus/services/llext

  {command} -d build.meshbus_llext_service_hw \\
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky

  {command} --force-edk -d build.meshbus_llext_service_hw \\
    sdk-meshbus/samples/subsys/meshbus/services/llext/services/blinky

  {command} --llext-sdk /opt/meshbus/llext-edk -o build/llext \\
    my_meshbus_app

Use either an existing host build with -d, or an extracted public release EDK
with --llext-sdk and -o. Standalone release-EDK builds do not need firmware
sources, a host .config, or zephyr.elf.
"""


def _add_build_arguments(parser):
    parser.add_argument(
        "-d",
        "--build-dir",
        help="Existing Zephyr firmware build directory for local development",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        help="Package output directory (required without --build-dir)",
    )
    parser.add_argument(
        "--llext-sdk",
        help="Override path to extracted llext-edk directory",
    )
    parser.add_argument(
        "--zephyr-sdk",
        help="Override path to Zephyr SDK root directory",
    )
    parser.add_argument(
        "--force-edk",
        action="store_true",
        help="Regenerate and re-extract llext-edk even when a usable EDK exists",
    )
    parser.add_argument(
        "source_dir",
        help="Path to extension source directory containing CMakeLists.txt and llext.yaml",
    )
    return parser


def _run_build(args, unknown_args: List[str]):
    source_dir = Path(args.source_dir).resolve()
    host_build_dir = Path(args.build_dir).resolve() if args.build_dir else None
    if host_build_dir is None and not args.llext_sdk:
        log.die("pass either --build-dir for local development or --llext-sdk for a release EDK")
    if host_build_dir is None and args.force_edk:
        log.die("--force-edk requires --build-dir")
    if args.output_dir:
        output_dir = Path(args.output_dir).resolve()
    elif host_build_dir is not None:
        output_dir = _managed_llext_dir(host_build_dir)
    else:
        log.die("--output-dir is required when building without --build-dir")
    extension_build_dir = _extension_build_dir(output_dir, source_dir)
    metadata_tool = SCRIPT_DIR / "gen_metadata.py"

    _validate_source(source_dir)
    if host_build_dir is not None:
        _validate_host_build_dir(host_build_dir)
    if not metadata_tool.is_file():
        log.die(f"metadata generator not found: {metadata_tool}")

    llext_sdk = _resolve_llext_sdk(args, host_build_dir)
    zephyr_sdk = _resolve_zephyr_sdk(args, host_build_dir, extension_build_dir)
    metadata_source = _read_yaml(source_dir / "llext.yaml")
    metadata_type = _metadata_type(metadata_source)
    injected = _metadata_injection(llext_sdk, metadata_source, host_build_dir)
    toolchain_bin = _toolchain_bin(zephyr_sdk)
    wrapper_bin = _create_llext_compiler_wrappers(extension_build_dir, toolchain_bin)

    env = os.environ.copy()
    env["LLEXT_EDK_INSTALL_DIR"] = str(llext_sdk)
    env["ZEPHYR_SDK_INSTALL_DIR"] = str(zephyr_sdk)
    env["PATH"] = f"{wrapper_bin}{os.pathsep}{toolchain_bin}{os.pathsep}{env.get('PATH', '')}"

    output_dir.mkdir(parents=True, exist_ok=True)
    extension_build_dir.mkdir(parents=True, exist_ok=True)
    extra_cmake_args = [a for a in unknown_args if a != "--"]

    configure_cmd = [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(extension_build_dir),
        f"-DLLEXT_EDK_INSTALL_DIR={llext_sdk}",
    ] + extra_cmake_args
    build_cmd = ["cmake", "--build", str(extension_build_dir)]

    log.inf("Configuring LLEXT build...")
    subprocess.run(configure_cmd, check=True, env=env)
    log.inf("Building LLEXT package...")
    subprocess.run(build_cmd, check=True, env=env)

    llext_path = _find_llext_output(extension_build_dir, source_dir)
    _normalize_llext_sections(llext_path, toolchain_bin, env)
    package_suffix = SERVICE_SUFFIX if metadata_type == "service" else APP_SUFFIX
    package_path = output_dir / f"{llext_path.stem}{package_suffix}"
    if package_path.exists():
        package_path.unlink()
    heap_size = _estimate_llext_heap_size(llext_path, _edk_config_path(llext_sdk))

    metadata_bin = extension_build_dir / f"{llext_path.stem}.meta.bin"
    objcopy = toolchain_bin / "arm-zephyr-eabi-objcopy"

    log.inf(f"Injecting metadata from {source_dir / 'llext.yaml'} into {llext_path.name}...")
    subprocess.run(
        [
            sys.executable,
            str(metadata_tool),
            "--yaml",
            str(source_dir / "llext.yaml"),
            "--out",
            str(metadata_bin),
            "--expected-id",
            llext_path.stem,
            "--metadata-version",
            str(injected["metadata-version"]),
            "--edk-version",
            injected["edk-version"],
            "--target",
            injected["target"],
            "--heap-size",
            str(heap_size),
            "--source-dir",
            str(source_dir),
        ],
        check=True,
        env=env,
    )
    subprocess.run(
        [str(objcopy), "--remove-section", METADATA_SECTION, str(llext_path)],
        check=True,
        env=env,
    )
    subprocess.run(
        [
            str(objcopy),
            "--add-section",
            f"{METADATA_SECTION}={metadata_bin}",
            str(llext_path),
        ],
        check=True,
        env=env,
    )
    subprocess.run(
        [
            str(objcopy),
            "--set-section-flags",
            f"{METADATA_SECTION}=contents,readonly",
            str(llext_path),
        ],
        check=True,
        env=env,
    )

    llext_path.replace(package_path)
    llext_path = package_path

    log.inf(f"LLEXT build complete: {llext_path}")
    return 0


def _validate_source(source_dir: Path) -> None:
    if not source_dir.is_dir():
        log.die(f"source directory does not exist: {source_dir}")
    if not (source_dir / "CMakeLists.txt").is_file():
        log.die(f"missing CMakeLists.txt in source directory: {source_dir}")
    if not (source_dir / "llext.yaml").is_file():
        log.die(f"missing llext.yaml in source directory: {source_dir}")


def _validate_host_build_dir(host_build_dir: Path) -> None:
    if not _is_zephyr_firmware_build_dir(host_build_dir):
        log.die(
            f"firmware build directory is not configured: {host_build_dir}; "
            "run west build -d <build-dir> for the host firmware first"
        )


def _resolve_llext_sdk(args, host_build_dir: Path | None) -> Path:
    if args.llext_sdk:
        return _validate_llext_sdk(Path(args.llext_sdk).resolve())
    if host_build_dir is None:
        log.die("--llext-sdk is required without --build-dir")

    managed_dir = _managed_llext_dir(host_build_dir)
    managed_edk_dir = managed_dir / EDK_DIRNAME
    managed_tarball = managed_dir / EDK_TARBALL
    host_tarball = host_build_dir / "zephyr" / EDK_TARBALL

    _build_llext_edk(host_build_dir)
    if not host_tarball.is_file():
        log.die(f"llext-edk target did not produce expected tarball: {host_tarball}")

    if (
        not args.force_edk
        and _is_llext_sdk(managed_edk_dir)
        and _edk_tarballs_match(host_tarball, managed_tarball)
    ):
        log.inf(f"Reusing managed LLEXT EDK: {managed_edk_dir}")
        return _validate_llext_sdk(managed_edk_dir)

    managed_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(host_tarball, managed_tarball)
    _extract_llext_edk(managed_tarball, managed_dir, managed_edk_dir)
    return _validate_llext_sdk(managed_edk_dir)


def _edk_tarballs_match(host_tarball: Path, managed_tarball: Path) -> bool:
    if not host_tarball.is_file() or not managed_tarball.is_file():
        return False

    host_stat = host_tarball.stat()
    managed_stat = managed_tarball.stat()
    return (
        host_stat.st_size == managed_stat.st_size
        and host_stat.st_mtime_ns == managed_stat.st_mtime_ns
    )


def _extract_llext_edk(tarball: Path, managed_dir: Path, edk_dir: Path) -> None:
    if edk_dir.exists():
        shutil.rmtree(edk_dir)
    log.inf(f"Extracting LLEXT EDK: {tarball}")
    with tarfile.open(tarball, "r:xz") as archive:
        archive.extractall(managed_dir)


def _build_llext_edk(host_build_dir: Path) -> None:
    if not _is_zephyr_firmware_build_dir(host_build_dir):
        log.die(
            f"firmware build directory is not configured: {host_build_dir}; "
            "run west build -d <build-dir> for the host firmware first or pass --llext-sdk"
        )
    log.inf(f"Generating LLEXT EDK from host build: {host_build_dir}")
    subprocess.run(["west", "build", "-d", str(host_build_dir), "-t", "llext-edk"], check=True)


def _resolve_zephyr_sdk(
    args, host_build_dir: Path | None, extension_build_dir: Path
) -> Path:
    candidates = [
        args.zephyr_sdk,
        _read_cache_path(extension_build_dir / "CMakeCache.txt", "ZEPHYR_SDK_INSTALL_DIR"),
        _read_cache_path(host_build_dir / "CMakeCache.txt", "ZEPHYR_SDK_INSTALL_DIR")
        if host_build_dir is not None
        else None,
        os.environ.get("ZEPHYR_SDK_INSTALL_DIR"),
        *sorted(Path.home().glob(".zephyr/toolchains/zephyr-sdk-*"), reverse=True),
    ]
    for candidate in candidates:
        if not candidate:
            continue
        path = Path(candidate).resolve()
        try:
            return _validate_zephyr_sdk(path)
        except ValueError as exc:
            log.dbg(f"invalid Zephyr SDK candidate {path}: {exc}")
    log.die("unable to find Zephyr SDK; pass --zephyr-sdk or configure a Zephyr host build")


def _managed_llext_dir(build_dir: Path) -> Path:
    return build_dir / MANAGED_LLEXT_SUBDIR


def _extension_build_dir(output_dir: Path, source_dir: Path) -> Path:
    return output_dir / LLEXT_BUILD_SUBDIR / source_dir.name


def _is_zephyr_firmware_build_dir(path: Path) -> bool:
    return (path / "CMakeCache.txt").is_file() and (path / "zephyr" / ".config").is_file()


def _validate_llext_sdk(path: Path) -> Path:
    if not _is_llext_sdk(path):
        log.die(
            f"invalid LLEXT EDK path: {path}; missing cmake.cflags "
            "(did you build or extract llext-edk?)"
        )
    return path


def _is_llext_sdk(path: Path) -> bool:
    return path.is_dir() and (path / "cmake.cflags").is_file()


def _validate_zephyr_sdk(path: Path) -> Path:
    toolchain_bin = _toolchain_bin(path)
    if not (toolchain_bin / "arm-zephyr-eabi-gcc").is_file():
        raise ValueError(f"missing arm-zephyr-eabi-gcc under {toolchain_bin}")
    if not (toolchain_bin / "arm-zephyr-eabi-g++").is_file():
        raise ValueError(f"missing arm-zephyr-eabi-g++ under {toolchain_bin}")
    if not (toolchain_bin / "arm-zephyr-eabi-ld").is_file():
        raise ValueError(f"missing arm-zephyr-eabi-ld under {toolchain_bin}")
    if not (toolchain_bin / "arm-zephyr-eabi-objcopy").is_file():
        raise ValueError(f"missing arm-zephyr-eabi-objcopy under {toolchain_bin}")
    return path


def _create_llext_compiler_wrappers(build_dir: Path, toolchain_bin: Path) -> Path:
    wrapper_bin = build_dir / "toolchain-wrapper"
    wrapper_bin.mkdir(parents=True, exist_ok=True)

    default_flags = " ".join(shlex.quote(flag) for flag in LLEXT_DEFAULT_CFLAGS)
    for executable in ("arm-zephyr-eabi-gcc", "arm-zephyr-eabi-g++"):
        real_compiler = toolchain_bin / executable
        wrapper = wrapper_bin / executable
        wrapper.write_text(
            "#!/bin/sh\n"
            f"exec {shlex.quote(str(real_compiler))} \"$@\" {default_flags}\n",
            encoding="utf-8",
        )
        wrapper.chmod(0o755)

    log.inf(f"Using default LLEXT compile flags: {' '.join(LLEXT_DEFAULT_CFLAGS)}")
    return wrapper_bin


def _toolchain_bin(zephyr_sdk: Path) -> Path:
    return zephyr_sdk / "arm-zephyr-eabi" / "bin"


def _read_cache_path(cache_path: Path, key: str) -> str | None:
    if not cache_path.is_file():
        return None
    pattern = re.compile(rf"^{re.escape(key)}:[^=]*=(.*)$")
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            value = match.group(1).strip()
            return value or None
    return None


def _read_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as infile:
        data = yaml.safe_load(infile)
    if not isinstance(data, dict):
        log.die(f"{path} must contain a mapping object")
    return data


def _metadata_type(metadata_source: dict) -> str:
    llext_type = metadata_source.get("type")
    if llext_type not in ("service", "app"):
        log.die("llext.yaml type must be 'service' or 'app'")
    return str(llext_type)


def _normalize_llext_sections(llext_path: Path, toolchain_bin: Path, env: dict[str, str]) -> None:
    linker = toolchain_bin / "arm-zephyr-eabi-ld"
    normalized_path = llext_path.with_name(f"{llext_path.stem}.normalized{llext_path.suffix}")

    if normalized_path.exists():
        normalized_path.unlink()

    log.inf(f"Normalizing LLEXT section layout: {llext_path.name}")
    subprocess.run(
        [str(linker), "-r", "-o", str(normalized_path), str(llext_path)],
        check=True,
        env=env,
    )
    normalized_path.replace(llext_path)


def _metadata_injection(
    llext_sdk: Path, metadata_source: dict, host_build_dir: Path | None
) -> dict[str, object]:
    metadata_type = _metadata_type(metadata_source)

    for key in (
        "metadata-version",
        "edk-version",
        "target",
        "heap-size",
        "icon-data",
    ):
        if key in metadata_source:
            log.die(f"{key} is build-injected and must not be present in llext.yaml")

    manifest = _read_manifest(llext_sdk)
    target = manifest.get("target") or _read_edk_target(llext_sdk)
    if host_build_dir is not None:
        host_target = _read_build_config_string(host_build_dir, "CONFIG_BOARD_TARGET")
        if target != host_target:
            log.die(
                f"LLEXT EDK target {target!r} does not match host target {host_target!r}"
            )

    manifest_profile = manifest.get("profile")
    if manifest_profile is not None and manifest_profile != metadata_type:
        log.die(
            f"LLEXT EDK profile {manifest_profile!r} does not match package "
            f"profile {metadata_type!r}"
        )

    edk_version = manifest.get("edk-version")
    metadata_version = manifest.get("metadata-version")
    if host_build_dir is not None:
        if edk_version is None:
            edk_version = _read_host_app_version(host_build_dir)
        if metadata_version is None:
            metadata_version = _read_host_metadata_version(host_build_dir)
    if edk_version is None or metadata_version is None:
        log.die(
            "release-EDK package builds require a manifest with "
            "host.version and metadata-version"
        )

    return {
        "target": str(target),
        "edk-version": str(edk_version),
        "metadata-version": int(metadata_version),
    }


def _align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return ((value + alignment - 1) // alignment) * alignment


def _estimate_llext_heap_size(llext_path: Path, config_path: Path) -> int:
    """Estimate bytes needed from the Zephyr LLEXT heap for one ELF."""

    try:
        elf = ElfImage.from_path(llext_path)
    except ElfError as exc:
        log.die(str(exc))

    sections = [section.as_mapping() for section in elf.sections]
    symbol_count = 0
    reloc_count = elf.relocation_count()
    shstrtab = sections[elf.shstrndx]

    regions: dict[int, dict[str, int]] = {}
    mapped_sections: set[int] = set()
    symtab_section = None

    for section in sections:
        if section["type"] == ELF_SHT_SYMTAB:
            _region_merge(regions, LLEXT_MEM_SYMTAB, section)
            mapped_sections.add(section["index"])
            symtab_section = section
            if section["entsize"]:
                symbol_count += section["size"] // section["entsize"]
            if section["link"] < len(sections):
                _region_merge(regions, LLEXT_MEM_STRTAB, sections[section["link"]])
                mapped_sections.add(section["link"])
        elif section["type"] == ELF_SHT_DYNSYM:
            if section["entsize"]:
                symbol_count += section["size"] // section["entsize"]

    _region_merge(regions, LLEXT_MEM_SHSTRTAB, shstrtab)
    mapped_sections.add(elf.shstrndx)

    for section in sections:
        if section["index"] in mapped_sections or section["size"] == 0:
            continue

        mem_idx = _llext_region_for_section(section)
        if mem_idx is None or not (section["flags"] & ELF_SHF_ALLOC):
            continue
        _region_merge(regions, mem_idx, section)

    mmu = _read_edk_config_bool(config_path, "CONFIG_MMU")
    userspace = _read_edk_config_bool(config_path, "CONFIG_USERSPACE")
    arm_mpu = _read_edk_config_bool(config_path, "CONFIG_ARM_MPU")
    arc_mpu = _read_edk_config_bool(config_path, "CONFIG_ARC_MPU")
    power2_mpu = _read_edk_config_bool(
        config_path, "CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT"
    )
    page_size = _read_edk_config_int(config_path, "CONFIG_MMU_PAGE_SIZE") or 4096

    region_total = 0
    region_count = 0
    for region in regions.values():
        size = _region_alloc_size(
            region,
            mmu=mmu,
            userspace=userspace,
            arm_mpu=arm_mpu,
            arc_mpu=arc_mpu,
            power2_mpu=power2_mpu,
            page_size=page_size,
        )
        if size == 0:
            continue
        region_total += size
        region_count += 1

    metadata_total = (len(sections) * 64) + (symbol_count * LLEXT_HEAP_SYMBOL_OVERHEAD)
    if symtab_section is not None and symtab_section["entsize"]:
        export_size = regions.get(LLEXT_MEM_EXPORT, {}).get("size", 0)
        exported_count = 0 if export_size == 0 else max(1, export_size // 8)
        metadata_total += exported_count * LLEXT_HEAP_SYMBOL_OVERHEAD

    estimate = region_total + metadata_total
    estimate += LLEXT_HEAP_BASE_OVERHEAD
    estimate += region_count * LLEXT_HEAP_SECTION_OVERHEAD
    estimate += reloc_count * LLEXT_HEAP_RELOC_OVERHEAD
    estimate = _align_up(estimate, LLEXT_HEAP_ALIGN)

    if estimate <= 0 or estimate > 0xFFFFFFFF:
        log.die(f"invalid LLEXT heap estimate for {llext_path}: {estimate}")

    log.inf(
        "Estimated LLEXT heap: "
        f"{estimate} bytes ({region_total} region bytes, "
        f"{region_count} regions, {symbol_count} symbols, {reloc_count} relocs)"
    )
    return estimate


def _llext_region_for_section(section: dict[str, int | str]) -> int | None:
    name = str(section["name"])
    sh_type = int(section["type"])
    flags = int(section["flags"])

    if name == ".exported_sym":
        return LLEXT_MEM_EXPORT
    if sh_type == 8:
        return LLEXT_MEM_BSS
    if sh_type == 1:
        if flags & 0x4:
            return LLEXT_MEM_TEXT
        if flags & 0x1:
            return LLEXT_MEM_DATA
        return LLEXT_MEM_RODATA
    return None


def _region_merge(regions: dict[int, dict[str, int]], mem_idx: int, section: dict[str, int | str]) -> None:
    size = int(section["size"])
    offset = int(section["offset"])
    align = max(int(section["align"]), 1)
    flags = int(section["flags"])

    if mem_idx not in regions:
        regions[mem_idx] = {
            "offset": offset,
            "size": size,
            "align": align,
            "flags": flags,
        }
        return

    region = regions[mem_idx]
    bot = min(region["offset"], offset)
    top = max(region["offset"] + region["size"], offset + size)
    region["offset"] = bot
    region["size"] = top - bot
    region["align"] = max(region["align"], align)
    region["flags"] |= flags


def _region_alloc_size(
    region: dict[str, int],
    *,
    mmu: bool,
    userspace: bool,
    arm_mpu: bool,
    arc_mpu: bool,
    power2_mpu: bool,
    page_size: int,
) -> int:
    align = max(region["align"], 1)
    prepad = region["offset"] & (align - 1)
    size = region["size"] + prepad

    if region["flags"] & ELF_SHF_ALLOC:
        if mmu:
            size = _align_up(size, page_size)
        elif userspace and power2_mpu:
            block_size = max(size, align, LLEXT_PAGE_SIZE_ARM_MPU)
            size = 1 << (block_size - 1).bit_length()
        elif userspace and (arm_mpu or arc_mpu):
            size = _align_up(size, LLEXT_PAGE_SIZE_ARM_MPU)

    return size


def _read_build_config_bool(host_build_dir: Path, key: str) -> bool:
    raw = _read_build_config_raw(host_build_dir, key)
    return raw == "y"


def _read_build_config_int(host_build_dir: Path, key: str) -> int | None:
    raw = _read_build_config_raw(host_build_dir, key)
    if raw is None:
        return None
    try:
        return int(raw, 0)
    except ValueError:
        return None


def _read_build_config_raw(host_build_dir: Path, key: str) -> str | None:
    config_path = host_build_dir / "zephyr" / ".config"
    pattern = re.compile(rf"^{re.escape(key)}=(.*)$")
    for line in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1).strip()
    return None


def _read_edk_config_bool(config_path: Path, key: str) -> bool:
    raw = _read_edk_config_raw(config_path, key)
    return raw is not None and raw not in {"0", "n"}


def _read_edk_config_int(config_path: Path, key: str) -> int | None:
    raw = _read_edk_config_raw(config_path, key)
    if raw is None:
        return None
    try:
        return int(raw, 0)
    except ValueError:
        return None


def _read_edk_config_raw(config_path: Path, key: str) -> str | None:
    pattern = re.compile(rf"^\s*#define\s+{re.escape(key)}(?:\s+(.*))?\s*$")
    for line in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            return (match.group(1) or "1").strip()
    return None


def _read_build_config_string(host_build_dir: Path, key: str) -> str:
    config_path = host_build_dir / "zephyr" / ".config"
    pattern = re.compile(rf"^{re.escape(key)}=(.*)$")
    for line in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        raw = match.group(1).strip()
        if raw.startswith('"'):
            try:
                value = ast.literal_eval(raw)
            except (SyntaxError, ValueError) as exc:
                log.die(f"invalid {key} string in {config_path}: {exc}")
        else:
            value = raw
        if not isinstance(value, str) or not value:
            log.die(f"{key} must be a non-empty string in {config_path}")
        return value

    log.die(f"missing {key} in firmware build config: {config_path}")


def _read_host_app_version(host_build_dir: Path) -> str:
    path = host_build_dir / "zephyr/include/generated/zephyr/app_version.h"
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r'^\s*#define\s+APP_VERSION_STRING\s+"([^"]+)"', text, re.MULTILINE)
    if not match:
        log.die(f"unable to read APP_VERSION_STRING from {path}")
    return match.group(1)


def _read_host_metadata_version(host_build_dir: Path) -> int:
    path = host_build_dir / "zephyr/include/generated/meshbus_llext_metadata_version.h"
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(
        r"^\s*#define\s+MESHBUS_LLEXT_METADATA_VERSION\s+([1-9][0-9]*)U?\s*$",
        text,
        re.MULTILINE,
    )
    if not match:
        log.die(f"unable to read MESHBUS_LLEXT_METADATA_VERSION from {path}")
    return int(match.group(1))


def _read_manifest(llext_sdk: Path) -> dict[str, object]:
    release_path = llext_sdk / "edk-release.json"
    if release_path.is_file():
        try:
            data = load_release_manifest(llext_sdk)
        except EdkSdkError as exc:
            log.die(str(exc))
        target = data["target"]
        profile = data["profile"]
        host = data["host"]
        return {
            "target": target,
            "profile": profile,
            "edk-version": host["version"],
            "metadata-version": data["metadata-version"],
        }

    for name in (
        "meshbus_llext_manifest.yaml",
        "meshbus_llext_manifest.yml",
        "meshbus_llext_manifest.json",
    ):
        path = llext_sdk / name
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8") as infile:
            if path.suffix == ".json":
                data = json.load(infile)
            else:
                data = yaml.safe_load(infile)
        if not isinstance(data, dict):
            log.die(f"LLEXT manifest must contain a mapping object: {path}")
        return {str(key): str(value) for key, value in data.items() if value is not None}

    return {}


def _edk_config_path(llext_sdk: Path) -> Path:
    cflags_path = llext_sdk / "cmake.cflags"
    text = cflags_path.read_text(encoding="utf-8")
    match = re.search(
        r"-imacros\$\{CMAKE_CURRENT_LIST_DIR\}/([^;\"]*autoconf\.h)", text
    )
    if not match:
        log.die(f"unable to locate generated autoconf.h in {cflags_path}")
    config_path = llext_sdk / match.group(1)
    if not config_path.is_file():
        log.die(f"LLEXT EDK generated config is missing: {config_path}")
    return config_path


def _read_edk_target(llext_sdk: Path) -> str:
    cflags_path = llext_sdk / "cmake.cflags"
    text = cflags_path.read_text(encoding="utf-8")
    board_name = _read_cmake_var(text, "LLEXT_EDK_BOARD_NAME")
    board_qualifiers = _read_cmake_var(text, "LLEXT_EDK_BOARD_QUALIFIERS")
    if board_name:
        if board_qualifiers:
            return f"{board_name}/{board_qualifiers}"
        return board_name

    board_target = _read_cmake_var(text, "LLEXT_EDK_BOARD_TARGET")
    if board_target:
        return board_target

    log.die(f"unable to read LLEXT EDK board target from {cflags_path}")


def _read_cmake_var(text: str, name: str) -> str | None:
    match = re.search(rf"set\({re.escape(name)}\s+\"([^\"]*)\"\)", text)
    if match:
        return match.group(1)
    match = re.search(rf"{re.escape(name)}[^\n=]*=\s*([^\s\)]+)", text)
    if match:
        return match.group(1).strip('"')
    return None


def _find_llext_output(build_dir: Path, source_dir: Path) -> Path:
    artifacts = sorted(build_dir.rglob("*.llext"))
    if not artifacts:
        log.die(f"no .llext artifact found in build directory: {build_dir}")
    if len(artifacts) == 1:
        return artifacts[0]

    preferred = source_dir.name
    preferred_matches = [path for path in artifacts if path.stem == preferred]
    if len(preferred_matches) == 1:
        return preferred_matches[0]

    candidates = ", ".join(str(path) for path in artifacts)
    log.die(
        "multiple .llext artifacts found; unable to select one automatically: "
        f"{candidates}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build one Meshbus LLEXT package from a public release EDK.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=_help_epilog(command="meshbus llext"),
    )
    _add_build_arguments(parser)
    args, unknown = parser.parse_known_args(argv)
    return _run_build(args, unknown)


if __name__ == "__main__":
    raise SystemExit(main())
