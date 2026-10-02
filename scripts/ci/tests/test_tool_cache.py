# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Pinned source-tool archives remain verified across cache hits and repairs."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


INSTALLER = Path(__file__).resolve().parents[3] / '.github/docker/install-tools.py'
SPEC = importlib.util.spec_from_file_location('ci_cached_tools', INSTALLER)
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class ToolCache(unittest.TestCase):
    URL = 'https://example.invalid/releases/tool.tar.gz'
    CONTENT = b'pinned archive bytes'
    DIGEST = hashlib.sha256(CONTENT).hexdigest()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cache_dir = self.root / 'cache'
        self.target = self.root / 'download.tar.gz'
        self.cached = self.cache_dir / f'{self.DIGEST}.archive'

    def serve_archive(self, args, **kwargs):
        self.assertEqual(args[0], 'curl')
        self.assertTrue(kwargs['check'])
        self.assertEqual(args[-1], self.URL)
        Path(args[args.index('--output') + 1]).write_bytes(self.CONTENT)
        return subprocess.CompletedProcess(args, 0)

    def test_miss_populates_cache_and_hit_uses_verified_bytes(self):
        with patch.object(installer.subprocess, 'run', side_effect=self.serve_archive) as fetch:
            installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            self.assertEqual(self.cached.read_bytes(), self.CONTENT)
            self.target.unlink()
            installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            self.assertEqual(self.target.read_bytes(), self.CONTENT)
            self.assertEqual(fetch.call_count, 1)
            self.assertEqual(list(self.cache_dir.iterdir()), [self.cached])

    def test_cache_is_verified_again_after_a_previous_success(self):
        with patch.object(installer.subprocess, 'run', side_effect=self.serve_archive) as fetch:
            installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            self.cached.write_bytes(b'corrupted restored cache')
            installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            self.assertEqual(fetch.call_count, 2)
            self.assertEqual(self.target.read_bytes(), self.CONTENT)
            self.assertEqual(self.cached.read_bytes(), self.CONTENT)

    def test_bad_cache_and_bad_redownload_fail_without_retaining_either(self):
        self.cache_dir.mkdir()
        self.cached.write_bytes(b'corrupted restored cache')

        def serve_corrupt(args, **kwargs):
            self.assertFalse(self.cached.exists())
            self.target.write_bytes(b'untrusted downloaded bytes')
            return subprocess.CompletedProcess(args, 0)

        with patch.object(installer.subprocess, 'run', side_effect=serve_corrupt) as fetch:
            with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            fetch.assert_called_once()
        self.assertFalse(self.target.exists())
        self.assertFalse(self.cached.exists())
        self.assertEqual(list(self.cache_dir.iterdir()), [])

    def test_failed_download_does_not_cache_partial_output(self):
        def interrupted(args, **kwargs):
            self.target.write_bytes(b'partial')
            raise subprocess.CalledProcessError(22, args)

        with patch.object(installer.subprocess, 'run', side_effect=interrupted):
            with self.assertRaises(subprocess.CalledProcessError):
                installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.cached.exists())

    def test_changed_pin_does_not_reuse_old_archive(self):
        self.cache_dir.mkdir()
        old = self.cache_dir / f'{"0" * 64}.archive'
        old.write_bytes(b'older release')
        with patch.object(installer.subprocess, 'run', side_effect=self.serve_archive) as fetch:
            installer.download(self.URL, self.DIGEST, self.target, cache_dir=self.cache_dir)
            fetch.assert_called_once()
        self.assertEqual(self.cached.read_bytes(), self.CONTENT)
        self.assertEqual(old.read_bytes(), b'older release')

    def test_no_cache_keeps_download_digest_verification(self):
        with patch.object(installer.subprocess, 'run', side_effect=self.serve_archive) as fetch:
            installer.download(self.URL, self.DIGEST, self.target)
            installer.download(self.URL, self.DIGEST, self.target)
            self.assertEqual(fetch.call_count, 2)
        self.assertFalse(self.cache_dir.exists())
        with patch.object(installer.subprocess, 'run', side_effect=self.serve_archive):
            with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                installer.download(self.URL, '0' * 64, self.target)
        self.assertFalse(self.target.exists())

    def test_light_cli_passes_optional_cache_to_only_source_tools(self):
        with patch.object(sys, 'argv', ['install-tools.py', '--light', '--prefix', str(self.root / 'tools'),
                                       '--cache-dir', str(self.cache_dir)]), \
                patch.object(installer, 'download') as download, \
                patch.object(installer.subprocess, 'run') as extract:
            installer.main()
        self.assertEqual(download.call_count, 2)
        self.assertIn('actionlint', download.call_args_list[0].args[0])
        self.assertIn('gitleaks', download.call_args_list[1].args[0])
        self.assertTrue(all(call.kwargs == {'cache_dir': self.cache_dir}
                            for call in download.call_args_list))
        self.assertEqual(extract.call_count, 2)


if __name__ == '__main__':
    unittest.main()
