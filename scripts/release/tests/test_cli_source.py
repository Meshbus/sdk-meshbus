# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Verify minimal CLI inputs against a separately frozen source graph."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import cli_source


class CliSource(unittest.TestCase):
    def git(self, root, *args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

    def repository(self, path):
        path.mkdir(parents=True)
        self.git(path, 'init', '-q')
        (path / 'west.yml').write_text('manifest: {}\n')
        self.git(path, 'add', '.')
        self.git(path, '-c', 'user.name=Fixture', '-c', 'user.email=test@example.invalid',
                 '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')
        return self.git(path, 'rev-parse', 'HEAD')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'meshbus'
        sha = self.repository(self.source)
        self.proto = self.root / 'modules/lib/meshbus-protobufs'
        proto_sha = self.repository(self.proto)
        self.git(self.proto, 'remote', 'add', 'origin', 'https://example.invalid/protobufs')
        self.snapshot = self.root / 'snapshot'
        self.snapshot.mkdir()
        self.project = dict(name='meshbus-protobufs', path='modules/lib/meshbus-protobufs',
                            url='https://example.invalid/protobufs', revision=proto_sha)
        (self.snapshot / 'source.json').write_text(json.dumps(dict(
            source_revision=sha, manifest_sha256=cli_source.digest(self.source / 'west.yml'))))
        (self.snapshot / 'west-frozen.yml').write_text(yaml.safe_dump(
            {'manifest': {'projects': [self.project]}}))
        self.provenance = dict(firmware=dict(revision=sha, dirty=False), off_manifest=[],
                              projects={'meshbus': dict(revision=sha, dirty=False),
                                        'meshbus-protobufs': dict(revision=proto_sha, dirty=False)})
        cli_source.record(self.snapshot, self.provenance)

    def test_minimal_workspace_verifies_without_west_or_other_dependencies(self):
        self.assertFalse((self.root / '.west').exists())
        source, proto = cli_source.verify(self.snapshot, self.source, self.root)
        self.assertEqual(source, self.provenance)
        self.assertEqual(proto, self.proto.resolve())

    def test_minimal_setup_fetches_only_schema_and_does_not_initialize_west(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'ci'))
        import cli_workspace
        remote = self.root / 'schema-remote'
        self.proto.rename(remote)
        env = {'GIT_CONFIG_COUNT': '2',
               'GIT_CONFIG_KEY_0': f'url.{remote.as_uri()}.insteadOf',
               'GIT_CONFIG_VALUE_0': self.project['url'],
               'GIT_CONFIG_KEY_1': 'protocol.file.allow', 'GIT_CONFIG_VALUE_1': 'always'}
        original_run = subprocess.run
        def fetch_locally(command, **kwargs):
            if 'fetch' in command:
                kwargs['env'] = dict(os.environ, **env)
            return original_run(command, **kwargs)
        with patch.object(cli_workspace.subprocess, 'run', side_effect=fetch_locally):
            actual = cli_workspace.setup(self.root, self.snapshot)
        self.assertEqual(actual, self.proto.resolve())
        self.assertFalse((self.root / '.west').exists())
        self.assertEqual(cli_source.verify(self.snapshot, self.source, self.root)[0], self.provenance)

    def test_changed_source_manifest_or_graph_is_rejected(self):
        for path in [self.source / 'west.yml', self.snapshot / 'west-frozen.yml']:
            original = path.read_bytes()
            path.write_bytes(original + b'# changed\n')
            with self.assertRaisesRegex(ValueError, 'snapshot'):
                cli_source.verify(self.snapshot, self.source, self.root)
            path.write_bytes(original)

    def test_dirty_or_wrong_schema_is_rejected(self):
        (self.proto / 'untracked.proto').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'schema'):
            cli_source.verify(self.snapshot, self.source, self.root)
        (self.proto / 'untracked.proto').unlink()
        self.git(self.proto, 'remote', 'set-url', 'origin', 'https://example.invalid/wrong')
        with self.assertRaisesRegex(ValueError, 'schema'):
            cli_source.verify(self.snapshot, self.source, self.root)

    def test_receipt_cannot_disagree_with_frozen_project(self):
        path = self.snapshot / 'cli-source.json'
        record = json.loads(path.read_text())
        record['provenance']['projects']['meshbus-protobufs']['revision'] = 'f' * 40
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'snapshot'):
            cli_source.verify(self.snapshot, self.source, self.root)

    def test_schema_destination_cannot_escape_workspace(self):
        self.project['path'] = '../outside'
        (self.snapshot / 'west-frozen.yml').write_text(yaml.safe_dump(
            {'manifest': {'projects': [self.project]}}))
        cli_source.record(self.snapshot, self.provenance)
        with self.assertRaisesRegex(ValueError, 'path'):
            cli_source.verify(self.snapshot, self.source, self.root)


if __name__ == '__main__':
    unittest.main()
