# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Exercise cleanup decisions without deleting any SDK or requiring Linux."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'prepare-disk.sh'


class DiskBudget(unittest.TestCase):
    def cleanup(self, free, after=0):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # The shell invokes only these stubs; no privileged deletion occurs.
            (root / 'df').write_text('#!/bin/sh\nif [ "$1" = -Pk ]; then\n'
                                    f' if [ -f "{root}/cleaned" ]; then n={after}; else n={free}; fi\n'
                                    ' printf "Filesystem blocks Used Available Capacity Mounted\\n/dev/test 1 1 %s 0%% /\\n" "$n"\n'
                                    'fi\n')
            (root / 'sudo').write_text(f'#!/bin/sh\necho "$*" >> "{root}/calls"\ntouch "{root}/cleaned"\n')
            for path in (root / 'df', root / 'sudo'):
                path.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       RUNNER_ENVIRONMENT='github-hosted', RUNNER_OS='Linux')
            result = subprocess.run(['bash', str(SCRIPT), 'host', '8'], env=env, capture_output=True, text=True)
            calls = (root / 'calls').read_text().splitlines() if (root / 'calls').exists() else []
            return result, calls

    def test_sufficient_space_skips_cleanup(self):
        result, calls = self.cleanup(9 * 1024 * 1024)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [])

    def test_cleanup_stops_after_headroom_is_reached(self):
        result, calls = self.cleanup(1024, 9 * 1024 * 1024)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 1)

    def test_insufficient_space_fails(self):
        result, calls = self.cleanup(1024, 1024)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 5)


if __name__ == '__main__':
    unittest.main()
