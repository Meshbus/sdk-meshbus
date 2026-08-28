# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import gen_metadata  # noqa: E402


class MetadataTests(unittest.TestCase):
    def test_service_v1_records_edk_version_and_preserves_layout(self):
        blob = gen_metadata.build_metadata_blob(
            {
                "type": "service",
                "id": "fixture",
                "name": "Fixture",
                "description": "Metadata fixture",
                "version": "1.0.0",
                "entry-point": "fixture_entry",
                "stack-size": 1024,
            },
            "fixture",
            metadata_version=1,
            edk_version="0.1.0",
            target="board/cpuapp",
            heap_size=4096,
        )

        self.assertEqual(len(blob), 400)
        self.assertEqual(struct.unpack_from("<I", blob, 4)[0], 1)
        self.assertEqual(blob[260:276], b"0.1.0\0" + bytes(10))
        self.assertEqual(blob[276:324], bytes(48))
        self.assertEqual(blob[324:337], b"board/cpuapp\0")

    def test_app_v1_preserves_target_and_icon_offsets(self):
        icon = bytes(range(gen_metadata.APP_ICON_DATA_MAX_LEN))
        with mock.patch.object(gen_metadata, "read_app_icon_data", return_value=icon):
            blob = gen_metadata.build_metadata_blob(
                {
                    "type": "app",
                    "id": "fixture",
                    "name": "Fixture",
                    "version": "1.0.0",
                    "entry-point": "fixture_entry",
                    "stack-size": 1024,
                },
                "fixture",
                metadata_version=1,
                edk_version="0.1.0",
                target="board/cpuapp",
                heap_size=4096,
                source_dir=Path.cwd(),
            )

        self.assertEqual(len(blob), 368)
        self.assertEqual(struct.unpack_from("<I", blob, 4)[0], 1)
        self.assertEqual(blob[200:216], b"0.1.0\0" + bytes(10))
        self.assertEqual(blob[216:264], bytes(48))
        self.assertEqual(blob[264:277], b"board/cpuapp\0")
        self.assertEqual(blob[328:348], icon)

    def test_source_cannot_override_generated_fields(self):
        with self.assertRaisesRegex(ValueError, "unsupported or build-injected"):
            gen_metadata.build_metadata_blob(
                {
                    "type": "service",
                    "id": "fixture",
                    "name": "Fixture",
                    "version": "1.0.0",
                    "entry-point": "fixture_entry",
                    "stack-size": 1024,
                    "edk-version": "9.0.0",
                },
                "fixture",
                metadata_version=1,
                edk_version="0.1.0",
                target="board/cpuapp",
                heap_size=4096,
            )

    def test_generated_versions_are_validated(self):
        metadata = {
            "type": "service",
            "id": "fixture",
            "name": "Fixture",
            "version": "1.0.0",
            "entry-point": "fixture_entry",
            "stack-size": 1024,
        }
        with self.assertRaisesRegex(ValueError, "metadata-version"):
            gen_metadata.build_metadata_blob(
                metadata,
                "fixture",
                metadata_version=0,
                edk_version="0.1.0",
                target="board/cpuapp",
                heap_size=4096,
            )
        with self.assertRaisesRegex(ValueError, "edk-version"):
            gen_metadata.build_metadata_blob(
                metadata,
                "fixture",
                metadata_version=1,
                edk_version="invalid",
                target="board/cpuapp",
                heap_size=4096,
            )


if __name__ == "__main__":
    unittest.main()
