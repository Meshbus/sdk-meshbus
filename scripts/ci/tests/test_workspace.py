# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Exercise real west/Git updates using temporary local remotes and pinned imports."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import workspace


class WorkspaceSync(unittest.TestCase):
    def git(self, path, *args):
        return subprocess.check_output(['git', *args], cwd=path, text=True,
                                       stderr=subprocess.DEVNULL).strip()

    def commit(self, path):
        self.git(path, 'add', '.')
        self.git(path, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')
        return self.git(path, 'rev-parse', 'HEAD')

    def setUp(self):
        environment = patch.dict(os.environ, {'MESHBUS_WEST_SEED': ''})
        environment.start()
        self.addCleanup(environment.stop)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.remotes = self.root / 'remotes'
        self.remote = self.remotes / 'dep'
        self.remote.mkdir(parents=True)
        self.git(self.remote, 'init', '-q', '-b', 'main')
        (self.remote / 'value').write_text('old')
        self.old = self.commit(self.remote)
        self.url = 'https://fixture.invalid/dep'
        self.project = dict(name='dep', path='dep', url=self.url, revision=self.old)
        (self.remote / 'value').write_text('new')
        self.new = self.commit(self.remote)
        self.job = self.root / 'job'
        self.source = self.job / 'meshbus'
        self.source.mkdir(parents=True)
        self.git(self.source, 'init', '-q')
        self.write_manifest([self.project])
        self.snapshot = self.root / 'snapshot'
        original_run = workspace.run
        self.updates = 0

        def local_run(*args, **kwargs):
            if args[:2] == ('west', 'zephyr-export'):
                return  # Fixture has no Zephyr CMake package.
            if args[:2] == ('west', 'update'):
                self.updates += 1
                env = kwargs['env'].copy()
                env.update(GIT_CONFIG_COUNT='4',
                           GIT_CONFIG_KEY_2=f'url.{self.remotes.as_uri()}/.insteadOf',
                           GIT_CONFIG_VALUE_2='https://fixture.invalid/',
                           GIT_CONFIG_KEY_3='protocol.file.allow', GIT_CONFIG_VALUE_3='always')
                kwargs['env'] = env
            return original_run(*args, **kwargs)
        patcher = patch.object(workspace, 'run', side_effect=local_run)
        patcher.start()
        self.addCleanup(patcher.stop)

    def write_manifest(self, projects):
        (self.source / 'west.yml').write_text(yaml.safe_dump({'manifest': {
            'projects': projects, 'self': {'path': 'meshbus'}}}))
        self.commit(self.source)

    def add_import(self, revision):
        importer = self.remotes / 'importer'
        importer.mkdir()
        self.git(importer, 'init', '-q')
        (importer / 'west.yml').write_text(yaml.safe_dump({'manifest': {
            'self': {'import': 'submanifests'}}}))
        (importer / 'submanifests').mkdir()
        (importer / 'submanifests/deps.yml').write_text(yaml.safe_dump({'manifest': {
            'projects': [dict(self.project, revision=revision)]}}))
        sha = self.commit(importer)
        self.write_manifest([dict(name='importer', url='https://fixture.invalid/importer',
                                  revision=sha, **{'import': True})])

    def image_seed(self):
        seed = self.root / 'image-seed'
        shutil.copytree(self.source, seed / 'meshbus')
        workspace.setup(seed)
        self.assertEqual(self.git(seed / 'dep', 'rev-parse', '--is-shallow-repository'), 'true')
        return seed

    def test_image_shallow_clone_works_without_remote_access(self):
        seed = self.image_seed()
        self.remotes.rename(self.root / 'offline-remotes')
        with patch.dict(os.environ, {'MESHBUS_WEST_SEED': str(seed)}):
            workspace.setup(self.job, self.snapshot)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', 'HEAD'), self.old)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', '--is-shallow-repository'), 'true')
        self.assertEqual(self.git(self.job / 'dep', 'remote', 'get-url', 'origin'), self.url)
        self.assertFalse((self.job / 'dep/.git/objects/info/alternates').exists())
        self.assertEqual(self.git(seed / 'dep', 'status', '--porcelain'), '')

    def test_image_cache_covers_imports_without_remote_access(self):
        self.add_import(self.old)
        seed = self.image_seed()
        self.remotes.rename(self.root / 'offline-remotes')
        with patch.dict(os.environ, {'MESHBUS_WEST_SEED': str(seed)}):
            workspace.setup(self.job, self.snapshot)
        for name in ('dep', 'importer'):
            self.assertEqual(self.git(self.job / name, 'rev-parse', 'HEAD'),
                             self.git(seed / name, 'rev-parse', 'HEAD'))

    def test_missing_revision_is_fetched_without_changing_image_seed(self):
        seed = self.image_seed()
        self.write_manifest([dict(self.project, revision=self.new)])
        with patch.dict(os.environ, {'MESHBUS_WEST_SEED': str(seed)}):
            workspace.setup(self.job, self.snapshot)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', 'HEAD'), self.new)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', '--is-shallow-repository'), 'true')
        self.assertEqual(self.git(seed / 'dep', 'rev-parse', 'HEAD'), self.old)
        self.assertEqual(self.git(seed / 'dep', 'status', '--porcelain'), '')

    def test_new_project_path_falls_back_to_shallow_fetch(self):
        seed = self.image_seed()
        self.write_manifest([dict(self.project, path='modules/dep')])
        with patch.dict(os.environ, {'MESHBUS_WEST_SEED': str(seed)}):
            workspace.setup(self.job, self.snapshot)
        self.assertEqual(self.git(self.job / 'modules/dep', 'rev-parse', 'HEAD'), self.old)
        self.assertEqual(self.git(self.job / 'modules/dep', 'rev-parse', '--is-shallow-repository'), 'true')
        self.assertFalse((self.job / 'dep').exists())

    def test_independent_jobs_use_the_same_sha_even_when_branch_moves(self):
        workspace.setup(self.job, self.snapshot)
        other = self.root / 'other-job'
        shutil.copytree(self.source, other / 'meshbus')
        (self.remote / 'value').write_text('newest')
        self.commit(self.remote)
        workspace.setup(other, reference=self.snapshot)
        for job in (self.job, other):
            self.assertEqual(self.git(job / 'dep', 'rev-parse', 'HEAD'), self.old)
            self.assertEqual(self.git(job / 'dep', 'rev-parse', '--is-shallow-repository'), 'true')
            self.assertEqual(workspace.run('west', 'config', 'manifest.path', cwd=job, capture=True), 'meshbus')
        self.assertEqual(self.updates, 2)
        self.assertEqual({p.name for p in self.snapshot.iterdir()}, {'source.json', 'west-frozen.yml'})

    def test_updates_existing_workspace_from_changed_source_manifest(self):
        workspace.setup(self.job)
        self.write_manifest([dict(self.project, revision=self.new)])
        workspace.setup(self.job, self.snapshot)
        self.assertEqual((self.job / 'dep/value').read_text(), 'new')
        versions = yaml.safe_load((self.snapshot / 'west-frozen.yml').read_text())
        self.assertEqual(versions['manifest']['projects'][0]['revision'], self.new)

    def test_root_floating_revision_is_rejected_before_update(self):
        self.write_manifest([dict(self.project, revision='main')])
        with self.assertRaisesRegex(ValueError, 'full commit SHAs'):
            workspace.setup(self.job, self.snapshot)
        self.assertEqual(self.updates, 0)
        self.assertFalse(self.snapshot.exists())

    def test_imports_and_nested_self_imports_are_pinned(self):
        self.add_import(self.old)
        workspace.setup(self.job, self.snapshot)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', 'HEAD'), self.old)
        other = self.root / 'other-job'
        shutil.copytree(self.source, other / 'meshbus')
        workspace.setup(other, reference=self.snapshot)
        self.assertEqual(self.git(other / 'dep', 'rev-parse', 'HEAD'), self.old)

    def test_floating_import_is_rejected_without_recording_success(self):
        self.add_import('main')
        with self.assertRaisesRegex(ValueError, 'full commit SHAs.*dep'):
            workspace.setup(self.job, self.snapshot)
        self.assertFalse(self.snapshot.exists())

    def test_inactive_nested_projects_do_not_affect_active_workspace(self):
        inactive = [dict(self.project, name='optional', path='dep/optional', groups=['optional']),
                    dict(self.project, name='child', path='dep/optional/child', groups=['optional'])]
        data = {'manifest': {'projects': [self.project, *inactive],
                             'group-filter': ['-optional'], 'self': {'path': 'meshbus'}}}
        (self.source / 'west.yml').write_text(yaml.safe_dump(data))
        self.commit(self.source)
        frozen = workspace.setup(self.job, self.snapshot)
        self.assertEqual([p['name'] for p in frozen['manifest']['projects']], ['dep'])
        self.assertFalse((self.job / 'dep/optional').exists())

    def test_wrong_source_record_is_rejected_before_update(self):
        workspace.setup(self.job, self.snapshot)
        self.write_manifest([dict(self.project, revision=self.new)])
        with self.assertRaisesRegex(ValueError, 'source identity'):
            workspace.setup(self.job, reference=self.snapshot)
        self.assertEqual(self.updates, 1)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', 'HEAD'), self.old)

    def test_modified_manifest_is_rejected_even_with_same_source_commit(self):
        workspace.setup(self.job, self.snapshot)
        with (self.source / 'west.yml').open('a') as stream:
            stream.write('# local edit\n')
        with self.assertRaisesRegex(ValueError, 'source identity'):
            workspace.setup(self.job, reference=self.snapshot)
        self.assertEqual(self.updates, 1)

    def test_record_cannot_override_source_dependency_version(self):
        workspace.setup(self.job, self.snapshot)
        versions = self.snapshot / 'west-frozen.yml'
        data = yaml.safe_load(versions.read_text())
        data['manifest']['projects'][0]['revision'] = self.new
        versions.write_text(yaml.safe_dump(data))
        with self.assertRaisesRegex(ValueError, 'recorded graph'):
            workspace.setup(self.job, reference=self.snapshot)
        self.assertEqual(self.git(self.job / 'dep', 'rev-parse', 'HEAD'), self.old)

    def test_failed_update_does_not_change_manifest_configuration(self):
        self.write_manifest([dict(self.project, revision='f' * 40)])
        with self.assertRaises(subprocess.CalledProcessError):
            workspace.setup(self.job, self.snapshot)
        self.assertEqual(workspace.run('west', 'config', 'manifest.path', cwd=self.job, capture=True), 'meshbus')
        self.assertFalse(self.snapshot.exists())

    def test_dirty_dependency_is_not_replaced(self):
        workspace.setup(self.job)
        (self.job / 'dep/value').write_text('local change')
        with self.assertRaisesRegex(ValueError, 'dirty dependency'):
            workspace.setup(self.job)
        self.assertEqual((self.job / 'dep/value').read_text(), 'local change')
        self.assertEqual(self.updates, 1)

    def test_shared_workspace_manifest_is_not_changed(self):
        workspace.init(self.job)
        workspace.run('west', 'config', 'manifest.path', 'another-product', cwd=self.job)
        with self.assertRaisesRegex(ValueError, 'manifest checkout'):
            workspace.setup(self.job)
        self.assertEqual(workspace.run('west', 'config', 'manifest.path', cwd=self.job, capture=True), 'another-product')
        self.assertEqual(self.updates, 0)


if __name__ == '__main__':
    unittest.main()
