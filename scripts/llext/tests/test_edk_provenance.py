# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from edk_provenance import (  # noqa: E402
    HostRelease,
    artifact_name,
    read_metadata_version,
    validate_release_mode,
)
from edk_release import EdkReleaseError  # noqa: E402


def _host(root: Path) -> HostRelease:
    return HostRelease(
        application="meshbus_client",
        version="0.1.0",
        build_revision="abc1234",
        source_revision="a" * 40,
        source_dirty=False,
        target="idea_mesh_tracker_c2/nrf54l15/cpuapp",
        profile="app",
        metadata_version=1,
        zephyr_version="4.2.0",
        zephyr_revision="b" * 40,
        zephyr_dirty=False,
        toolchain_name="zephyr",
        toolchain_identity="sdk/arm-zephyr-eabi-gcc-14.2.0",
        compiler_name="arm-zephyr-eabi-gcc",
        source_root=root,
        workspace_root=root,
        zephyr_root=root,
        toolchain_root=root,
    )


class EdkProvenanceTests(unittest.TestCase):
    def test_metadata_version_accepts_one_positive_integer(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "METADATA_VERSION"
            path.write_text("1\n", encoding="utf-8")
            self.assertEqual(read_metadata_version(path), 1)

    def test_metadata_version_rejects_invalid_contents(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "METADATA_VERSION"
            for value in ("", "0\n", "1 trailing\n", "-1\n", "4294967296\n"):
                path.write_text(value, encoding="utf-8")
                with self.subTest(value=value):
                    with self.assertRaises(EdkReleaseError):
                        read_metadata_version(path)

    def test_formal_release_requires_clean_sources_but_no_baseline(self):
        with tempfile.TemporaryDirectory() as temporary:
            host = _host(Path(temporary))
            validate_release_mode(host, development=False, force=False)
            with self.assertRaisesRegex(EdkReleaseError, "clean source"):
                validate_release_mode(
                    replace(host, source_dirty=True),
                    development=False,
                    force=False,
                )

    def test_force_is_development_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            host = _host(Path(temporary))
            with self.assertRaisesRegex(EdkReleaseError, "development"):
                validate_release_mode(host, development=False, force=True)
            validate_release_mode(host, development=True, force=True)

    def test_artifact_identity_uses_firmware_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            host = _host(Path(temporary))
            self.assertEqual(
                artifact_name(host, development=False),
                "meshbus-client-0.1.0-idea-mesh-tracker-c2-nrf54l15-cpuapp-app-edk.tar.xz",
            )


if __name__ == "__main__":
    unittest.main()
