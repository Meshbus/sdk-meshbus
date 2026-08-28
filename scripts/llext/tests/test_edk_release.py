# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from edk_release import (  # noqa: E402
    EDK_ROOT_NAME,
    EdkReleaseError,
    create_pruned_edk,
    filter_edk_tree,
    safe_extract_edk,
    sha256_file,
    write_deterministic_archive,
)


class EdkFixture:
    def __init__(self, directory: Path, build_name: str = "fixture-build"):
        self.directory = directory
        self.root = directory / EDK_ROOT_NAME
        self.build_name = build_name

    def write(self, relative: str, contents: str = "fixture\n") -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def populate(self, *, missing_generated: bool = False, dangling_flag: bool = False) -> None:
        build = f"include/build/{self.build_name}"
        includes = [
            "include/meshbus/include",
            f"{build}/modules/meshbus/subsys/meshbus",
            "include/meshbus/subsys/meshbus",
            f"{build}/modules/mbedtls",
            f"{build}/modules/empty/include",
            f"{build}/modules/normalized/child/..",
            "include/zephyr/include",
        ]
        if dangling_flag:
            includes.append("include/zephyr/missing")

        cmake_flags = ";".join(
            f"-I${{CMAKE_CURRENT_LIST_DIR}}/{path}" for path in includes
        )
        make_flags = " ".join(
            f'"-I$(LLEXT_EDK_INSTALL_DIR)/{path}"' for path in includes
        )
        self.write(
            "cmake.cflags",
            "# Target information\n"
            'set(LLEXT_EDK_BOARD_TARGET "fixture_target")\n'
            "# Compile flags\n"
            f'set(LLEXT_CFLAGS "{cmake_flags}")\n'
            f'set(LLEXT_ALL_INCLUDE_CFLAGS "{cmake_flags}")\n'
            f'set(LLEXT_INCLUDE_CFLAGS "{cmake_flags}")\n'
            'set(LLEXT_GENERATED_INCLUDE_CFLAGS "")\n'
            'set(LLEXT_BASE_CFLAGS "-std=c17")\n'
            'set(LLEXT_GENERATED_IMACROS_CFLAGS "")\n',
        )
        self.write(
            "Makefile.cflags",
            "# Target information\n"
            'LLEXT_EDK_BOARD_TARGET = "fixture_target"\n'
            "# Compile flags\n"
            f"LLEXT_CFLAGS = {make_flags}\n"
            f"LLEXT_ALL_INCLUDE_CFLAGS = {make_flags}\n"
            f"LLEXT_INCLUDE_CFLAGS = {make_flags}\n"
            "LLEXT_GENERATED_INCLUDE_CFLAGS = \n"
            'LLEXT_BASE_CFLAGS = "-std=c17"\n'
            "LLEXT_GENERATED_IMACROS_CFLAGS = \n",
        )

        self.write("include/meshbus/include/zephyr/display/display.h")
        self.write(
            "include/meshbus/include/zephyr/meshbus/api.h",
            '#include "meshbus/foo.pb.h"\n',
        )
        self.write("include/meshbus/include/zephyr/zui/zui.h")
        self.write("include/meshbus/include/zephyr/thingsboard/thingsboard.h")
        self.write("include/meshbus/subsys/meshbus/private.h")
        if not missing_generated:
            self.write(
                f"{build}/modules/meshbus/subsys/meshbus/meshbus/foo.pb.h"
            )
        self.write(
            f"{build}/modules/meshbus/subsys/meshbus/meshbus/unused.pb.h"
        )
        self.write(f"{build}/modules/mbedtls/config.h")
        (self.root / f"{build}/modules/empty/include").mkdir(parents=True)
        (self.root / f"{build}/modules/normalized/child").mkdir(parents=True)
        self.write("include/zephyr/include/zephyr/kernel.h")

    def archive(self, name: str = "source.tar.xz") -> Path:
        path = self.directory / name
        write_deterministic_archive(self.root, path)
        return path


class EdkReleaseTests(unittest.TestCase):
    def test_filter_keeps_public_closure_and_rewrites_flags(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            fixture = EdkFixture(directory)
            fixture.populate()
            report = filter_edk_tree(fixture.root)

            self.assertEqual(len(report.retained_public_headers), 3)
            self.assertEqual(
                report.retained_generated_headers,
                (
                    "include/build/host/modules/meshbus/subsys/meshbus/meshbus/foo.pb.h",
                ),
            )
            self.assertTrue(
                (
                    fixture.root
                    / "include/build/host/modules/meshbus/subsys/meshbus/meshbus/foo.pb.h"
                ).is_file()
            )
            self.assertTrue(
                (fixture.root / "include/build/host/modules/mbedtls/config.h").is_file()
            )
            self.assertTrue(
                (fixture.root / "include/build/host/modules/empty/include").is_dir()
            )
            self.assertFalse(
                (
                    fixture.root
                    / "include/build/host/modules/meshbus/subsys/meshbus/meshbus/unused.pb.h"
                ).exists()
            )
            self.assertFalse(
                (
                    fixture.root
                    / "include/meshbus/include/zephyr/thingsboard/thingsboard.h"
                ).exists()
            )
            cmake = (fixture.root / "cmake.cflags").read_text(encoding="utf-8")
            makefile = (fixture.root / "Makefile.cflags").read_text(encoding="utf-8")
            for flags in (cmake, makefile):
                self.assertIn("include/build/host", flags)
                self.assertNotIn("fixture-build", flags)
                self.assertNotIn("include/meshbus/subsys", flags)
                self.assertNotIn("/child/..", flags)

    def test_equivalent_build_paths_produce_identical_archives(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            outputs: list[Path] = []
            for index, build_name in enumerate(("build-a", "elsewhere-build-b")):
                fixture_dir = directory / f"fixture-{index}"
                fixture = EdkFixture(fixture_dir, build_name)
                fixture.populate()
                source = fixture.archive()
                output = directory / f"release-{index}.tar.xz"
                create_pruned_edk(source, output)
                outputs.append(output)

            self.assertEqual(sha256_file(outputs[0]), sha256_file(outputs[1]))
            self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())

            for index, output in enumerate(outputs):
                extract_dir = directory / f"extract-{index}"
                root = safe_extract_edk(output, extract_dir)
                flags = (root / "cmake.cflags").read_text(encoding="utf-8")
                self.assertIn("${CMAKE_CURRENT_LIST_DIR}/include/build/host", flags)
                self.assertNotIn(str(extract_dir), flags)

    def test_rejects_archive_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            archive_path = directory / "unsafe.tar.xz"
            with tarfile.open(archive_path, "w:xz") as archive:
                info = tarfile.TarInfo(f"{EDK_ROOT_NAME}/../escape")
                data = b"unsafe\n"
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))

            with self.assertRaisesRegex(EdkReleaseError, "unsafe EDK archive path"):
                safe_extract_edk(archive_path, directory / "extract")

    def test_materializes_safe_symlink_and_rejects_escaping_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            archive_path = directory / "links.tar.xz"
            with tarfile.open(archive_path, "w:xz") as archive:
                for relative, contents in (
                    (f"{EDK_ROOT_NAME}/cmake.cflags", b"fixture\n"),
                    (f"{EDK_ROOT_NAME}/Makefile.cflags", b"fixture\n"),
                    (f"{EDK_ROOT_NAME}/include/source.h", b"header\n"),
                ):
                    info = tarfile.TarInfo(relative)
                    info.size = len(contents)
                    archive.addfile(info, io.BytesIO(contents))
                link = tarfile.TarInfo(f"{EDK_ROOT_NAME}/include/copy.h")
                link.type = tarfile.SYMTYPE
                link.linkname = "source.h"
                archive.addfile(link)

            root = safe_extract_edk(archive_path, directory / "safe-extract")
            self.assertEqual(
                (root / "include/copy.h").read_bytes(),
                (root / "include/source.h").read_bytes(),
            )

            unsafe_path = directory / "unsafe-link.tar.xz"
            with tarfile.open(unsafe_path, "w:xz") as archive:
                link = tarfile.TarInfo(f"{EDK_ROOT_NAME}/escape")
                link.type = tarfile.SYMTYPE
                link.linkname = "../../outside"
                archive.addfile(link)
            with self.assertRaisesRegex(EdkReleaseError, "unsafe EDK symlink target"):
                safe_extract_edk(unsafe_path, directory / "unsafe-extract")

    def test_rejects_wrong_root_duplicate_and_hardlink_members(self):
        cases = (
            ("wrong-root", "outside", "regular", "outside llext-edk/"),
            (
                "duplicate",
                f"{EDK_ROOT_NAME}/duplicate",
                "duplicate",
                "duplicate EDK archive member",
            ),
            (
                "hardlink",
                f"{EDK_ROOT_NAME}/hardlink",
                "hardlink",
                "unsupported EDK archive member type",
            ),
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, member_name, kind, message in cases:
                with self.subTest(name=name):
                    archive_path = directory / f"{name}.tar.xz"
                    with tarfile.open(archive_path, "w:xz") as archive:
                        count = 2 if kind == "duplicate" else 1
                        for _ in range(count):
                            info = tarfile.TarInfo(member_name)
                            if kind == "hardlink":
                                info.type = tarfile.LNKTYPE
                                info.linkname = f"{EDK_ROOT_NAME}/target"
                                archive.addfile(info)
                            else:
                                data = b"fixture\n"
                                info.size = len(data)
                                archive.addfile(info, io.BytesIO(data))
                    with self.assertRaisesRegex(EdkReleaseError, message):
                        safe_extract_edk(archive_path, directory / f"extract-{name}")

    def test_rejects_missing_generated_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = EdkFixture(Path(temporary))
            fixture.populate(missing_generated=True)
            with self.assertRaisesRegex(
                EdkReleaseError, "generated dependency meshbus/foo.pb.h"
            ):
                filter_edk_tree(fixture.root)

    def test_rejects_dangling_include_flag(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = EdkFixture(Path(temporary))
            fixture.populate(dangling_flag=True)
            with self.assertRaisesRegex(EdkReleaseError, "missing paths"):
                filter_edk_tree(fixture.root)


if __name__ == "__main__":
    unittest.main()
