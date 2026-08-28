# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from edk import _refresh_standard_edk, _temporary_west_build_info  # noqa: E402
from edk_release import EdkReleaseError, write_sha256_sidecar  # noqa: E402
from qualify_edk import _verify_sidecar  # noqa: E402


class EdkCommandTests(unittest.TestCase):
    def test_sysbuild_child_west_metadata_is_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            build_dir = Path(temporary)
            build_info = build_dir / "build_info.yml"
            original = b"version: 0.1.0\ncmake:\n  application: {}\n"
            build_info.write_bytes(original)

            with patch("edk._west_topdir", return_value="/workspace"):
                with _temporary_west_build_info(build_dir):
                    self.assertEqual(
                        build_info.read_text(encoding="utf-8").splitlines()[-2:],
                        ["west:", "  topdir: /workspace"],
                    )

            self.assertEqual(build_info.read_bytes(), original)

    def test_standard_edk_is_refreshed_even_when_an_archive_exists(self):
        with tempfile.TemporaryDirectory() as temporary:
            build_dir = Path(temporary)
            archive = build_dir / "zephyr" / "llext-edk.tar.xz"
            archive.parent.mkdir(parents=True)
            archive.write_bytes(b"stale")

            def generate(command, *, check):
                self.assertTrue(check)
                self.assertEqual(command[-2:], ["-t", "llext-edk"])
                self.assertFalse(archive.exists())
                archive.write_bytes(b"fresh")

            with patch("edk.subprocess.run", side_effect=generate) as run:
                self.assertEqual(_refresh_standard_edk(build_dir), archive)

            run.assert_called_once()
            self.assertEqual(archive.read_bytes(), b"fresh")

    def test_qualification_sidecar_must_match_the_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "fixture.tar.xz"
            archive.write_bytes(b"archive")
            write_sha256_sidecar(archive)
            _verify_sidecar(archive)

            archive.write_bytes(b"changed")
            with self.assertRaisesRegex(EdkReleaseError, "sidecar does not match"):
                _verify_sidecar(archive)


if __name__ == "__main__":
    unittest.main()
