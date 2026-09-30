# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Release identity and draft visibility boundaries, without remote writes."""
import json
import io
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest
import urllib.request
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import alpha
import publish


SHA = 'a' * 40
TAG = 'v1.0.0-alpha.1'
UF2_NAME = 'meshbus-1.0.0-alpha.1-mesh_probe_r1_nrf52840.uf2'


class Identity(unittest.TestCase):
    def test_canonical_alpha_versions(self):
        self.assertEqual(alpha.version(TAG), '1.0.0-alpha.1')
        for tag in ('1.0.0-alpha.1', 'v1.0.0', 'v1.0.0-alpha.0',
                    'v01.0.0-alpha.1', 'v1.0.0-alpha.01', 'v1.0.0-alpha.1+build'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                alpha.version(tag)

    def test_source_and_version_must_match(self):
        text = ('VERSION_MAJOR = 1\nVERSION_MINOR = 0\nPATCHLEVEL = 0\n'
                'VERSION_TWEAK = 0\nEXTRAVERSION = alpha.1\n')
        alpha.identity(TAG, SHA, SHA, text)
        for source, content in (('b' * 40, text), (SHA, text.replace('alpha.1', 'alpha.2'))):
            with self.assertRaises(ValueError):
                alpha.identity(TAG, SHA, source, content)


class FakeGitHub:
    def __init__(self):
        self.release = None
        self.data = {}
        self.writes = []
        self.fail_upload = None
        self.corrupt = None
        self.public_failure = False
        self.expose_response_lost = False
        self.sha = SHA

    def tag_sha(self, tag):
        return self.sha

    def find(self, tag):
        return self.release

    def assets(self, release):
        return [{'name': name, 'size': len(data), 'state': 'uploaded'}
                for name, data in self.data.items()]

    def download(self, asset, anonymous=False):
        if anonymous and self.public_failure:
            raise ValueError('public download unavailable')
        data = self.data[asset['name']]
        return data + b'corrupt' if self.corrupt == asset['name'] else data

    def create(self, tag, sha, body):
        self.writes.append('create-draft')
        self.release = dict(tag_name=tag, target_commitish=sha, body=body,
                            id=1, author={'login': 'github-actions[bot]'}, draft=True,
                            prerelease=True, html_url='https://github.com/Meshbus/sdk-meshbus/releases/1')
        return self.release

    def upload(self, release, name, data):
        if name == self.fail_upload:
            raise ValueError('upload failed')
        self.writes.append('upload:' + name)
        self.data[name] = data

    def expose(self, release):
        self.writes.append('publish')
        self.release['draft'] = False
        if self.expose_response_lost:
            raise ValueError('response lost')
        return self.release

    def latest_id(self):
        return None


class Visibility(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / 'assets'
        self.assets.mkdir()
        self.manifest = {'schema': 1, 'tag': TAG, 'source_revision': SHA,
                         'version': TAG[1:], 'target': alpha.TARGET,
                         'repository': 'Meshbus/sdk-meshbus', 'run_id': '123',
                         'qualification': {'alpha_eligible': True, 'production_qualified': False},
                         'assets': [alpha.entry(name, b'fixture') for name in sorted(alpha.payload_names(TAG))]}
        for name in alpha.payload_names(TAG):
            (self.assets / name).write_bytes(b'fixture')
        alpha.art.write_json(self.assets / 'release-manifest.json', self.manifest)
        alpha.art.checksums(self.assets)
        self.inventory = alpha.inventory(self.assets)
        self.github = FakeGitHub()
        quiet = patch.object(publish, 'summary')
        quiet.start()
        self.addCleanup(quiet.stop)

    def publish(self):
        return publish.publish(self.github, self.assets, self.inventory, TAG, SHA, '123')

    def test_only_verified_complete_draft_becomes_public(self):
        self.publish()
        self.assertFalse(self.github.release['draft'])
        self.assertEqual(self.github.writes[-1], 'publish')

    def test_upload_failure_retains_draft_and_resume_preserves_bytes(self):
        self.github.fail_upload = 'release-manifest.json'
        with self.assertRaises(ValueError):
            self.publish()
        self.assertTrue(self.github.release['draft'])
        original = list(self.github.writes)
        self.github.fail_upload = None
        self.publish()
        self.assertEqual(self.github.writes.count('upload:' + UF2_NAME), 1)
        self.assertEqual(self.github.writes[:len(original)], original)

    def test_corrupt_download_never_publishes(self):
        self.github.corrupt = UF2_NAME
        with self.assertRaises(ValueError):
            self.publish()
        self.assertTrue(self.github.release['draft'])
        self.assertNotIn('publish', self.github.writes)

    def test_public_failure_reports_already_published(self):
        self.github.public_failure = True
        with self.assertRaisesRegex(ValueError, 'already public'):
            self.publish()
        self.assertFalse(self.github.release['draft'])

    def test_publication_response_loss_is_reported_without_deleting_assets(self):
        self.github.expose_response_lost = True
        with self.assertRaisesRegex(ValueError, 'already public'):
            self.publish()
        self.assertFalse(self.github.release['draft'])
        self.assertEqual(set(self.github.data), {p['name'] for p in self.inventory})

    def test_published_retry_is_read_only_and_rejects_tag_movement(self):
        self.publish()
        self.github.writes.clear()
        publish.preflight(self.github, TAG, SHA)
        self.assertEqual(self.github.writes, [])
        self.github.sha = 'b' * 40
        with self.assertRaises(ValueError):
            publish.preflight(self.github, TAG, SHA)

    def test_foreign_draft_and_conflicting_assets_fail_without_overwrite(self):
        self.github.release = dict(tag_name=TAG, target_commitish=SHA, body='handmade', draft=True, prerelease=True)
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.github.writes, [])
        self.github.release = None
        self.github.fail_upload = 'release-manifest.json'
        with self.assertRaises(ValueError):
            self.publish()
        self.github.data[UF2_NAME] = b'wrong'
        self.github.fail_upload = None
        before = list(self.github.writes)
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.github.writes, before)

    def test_owned_draft_cannot_start_a_fresh_build(self):
        self.github.fail_upload = 'release-manifest.json'
        with self.assertRaises(ValueError):
            self.publish()
        with self.assertRaisesRegex(ValueError, 'original failed publication job'):
            publish.preflight(self.github, TAG, SHA)

    def test_unexpected_assets_or_retained_inventory_conflicts_block_visibility(self):
        (self.assets / 'unexpected.txt').write_text('extra')
        alpha.art.checksums(self.assets)
        with self.assertRaises(ValueError):
            self.publish()
        self.assertIsNone(self.github.release)


class Collection(unittest.TestCase):
    def setUp(self):
        import release
        self.release = release
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.candidate = self.root / 'candidate'
        self.snapshot = self.root / 'snapshot'
        self.snapshot.mkdir()
        alpha.art.write_json(self.snapshot / 'source.json', {'source_revision': SHA, 'manifest_sha256': 'd' * 64})
        (self.snapshot / 'west-frozen.yml').write_text('manifest:\n  projects:\n  - name: zephyr\n    revision: ' + 'b' * 40 + '\n')
        (self.snapshot / 'image.txt').write_text('ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'c' * 64)
        provenance = {'firmware': {'revision': SHA, 'dirty': False}, 'off_manifest': [],
                      'projects': {'meshbus': {'revision': SHA, 'dirty': False},
                                   'zephyr': {'revision': 'b' * 40, 'dirty': False}}}
        self.matrix = [{'board': target, 'id': target.split('/')[0]} for target in
                       (alpha.TARGET, 'mesh_probe_r2/soc', 'devkit/soc', 'wio/soc')]
        self.records = [dict(target=t['board'], id=t['id'], version=TAG[1:], engineering=False,
                             provenance=provenance, validation={'build': 'passed', 'hardware': 'not-run'})
                        for t in self.matrix]
        self.record = self.records[0]
        binary = bytes(range(256))
        uf2 = struct.pack('<8I', 0x0A324655, 0x9E5D5157, 0x2000, 0x27000, 256, 0, 1, 0xada52840)
        uf2 += binary + bytes(220) + struct.pack('<I', 0x0AB16F30)
        image = {'domain': 'app', 'address': 0x27000, 'partition_address': 0x27000,
                 'partition_size': 4096, 'sha256': alpha.art.digest(binary)}
        self.record.update(format='uf2', authentication='none', capabilities={'llext': True}, images=[image],
                           uf2=release.verify_uf2(uf2, binary, 0x27000, 0x28000, 0xada52840),
                           build={'toolchain': 'zephyr', 'compiler_version': '14.2.0', 'west_version': '1.5.0'},
                           edk_tool={'version': '1.0.0', 'binary_sha256': 'e' * 64})
        self.part = self.candidate / 'firmware' / 'r1'
        self.part.mkdir(parents=True)
        (self.part / 'app.bin').write_bytes(binary)
        (self.part / 'app.uf2').write_bytes(uf2)
        alpha.art.write_json(self.part / 'candidate-validation.json', {'target': alpha.TARGET, 'source_revision': SHA,
                             'checks': ['production-packaging', 'edk-verify', 'edk-c-and-cxx-qualify']})
        self.firmware = self.root / 'firmware'
        self.firmware.mkdir()
        (self.firmware / 'app.bin').write_bytes(binary)
        (self.firmware / 'app.uf2').write_bytes(uf2)
        alpha.art.write_json(self.firmware / 'flash-map.json', self.record)
        (self.firmware / 'LICENSE.txt').write_text('license fixture')
        (self.firmware / 'NOTICE.txt').write_text('application-only fixture notice')
        (self.firmware / 'licenses/meshbus').mkdir(parents=True)
        (self.firmware / 'licenses/meshbus/LICENSE').write_text('Apache-2.0 license fixture')
        alpha.art.write_json(self.firmware / 'license-materials.json', {'selection': 'spdx-source-components',
            'components': [{'component': 'meshbus', 'materials': ['licenses/meshbus/LICENSE']}], 'fonts': []})
        (self.firmware / 'SBOM.spdx').write_text('SPDXVersion: SPDX-2.3\nPackageName: meshbus-firmware\n'
            'PackageVersion: 1.0.0-alpha.1\nPackageName: meshbus-sdk\nPackageVersion: ' + SHA + '\n'
            'PackageLicenseDeclared: Apache-2.0\nPackageLicenseConcluded: Apache-2.0\n')
        self.edk_manifest = {'publishable': True, 'target': alpha.TARGET,
                            'host': {'version': TAG[1:], 'source-revision': SHA},
                            'edk': {'header-policy': 'meshbus-public-v1'}, 'provenance': provenance}
        self.edk_manifest['provenance'] = {key: value for key, value in provenance.items() if key != 'off_manifest'}
        edk = self.root / 'edk'
        (edk / 'LICENSES').mkdir(parents=True)
        for name in ('LICENSE.txt', 'LICENSES/Apache-2.0.txt', 'NOTICE.txt'):
            (edk / name).write_text('Apache-2.0 license fixture')
        edk_path = self.part / 'app-1.0.0-alpha.1-mesh_probe_r1-nrf52840-edk.tar.xz'
        with tarfile.open(edk_path, 'w:xz') as stream:
            stream.add(edk, arcname='llext-edk')
        alpha.art.sidecar(edk_path)
        self.repack()
        self.review = self.root / 'review.json'
        self.output = self.root / 'public'
        self.evidence = self.root / 'evidence'
        self.patchers = [patch.object(release, 'targets', return_value=self.matrix),
                         patch.object(release, 'cli_json', side_effect=lambda *args: {'manifest': self.edk_manifest})]
        for p in self.patchers:
            p.start()
            self.addCleanup(p.stop)

    def refresh(self):
        alpha.art.write_json(self.part / 'release-part.json', self.record)
        alpha.art.checksums(self.part)
        assets = [{'path': p.relative_to(self.candidate).as_posix(), 'size': p.stat().st_size,
                   'sha256': alpha.art.digest(alpha.art.read(p))} for p in alpha.art.files(self.candidate)
                  if p.parent != self.candidate]
        alpha.art.write_json(self.candidate / 'release.json', {'schema': 1, 'publishable': False,
            'firmware_version': TAG[1:], 'products': self.records, 'assets': assets,
            'unverified_gates': ['hardware-qualification', 'public-publication']})
        alpha.art.checksums(self.candidate)

    def repack(self):
        alpha.art.checksums(self.firmware)
        archive = self.part / 'meshbus-1.0.0-alpha.1-mesh_probe_r1_nrf52840-firmware.tar.gz'
        alpha.art.pack(self.firmware, archive, 'firmware')
        self.refresh()

    def collect(self):
        alpha.collect(self.candidate, self.snapshot, self.output, self.evidence, TAG, SHA, '123', self.review)

    def approve(self):
        with self.assertRaisesRegex(ValueError, 'missing reviewed'):
            self.collect()
        scope = json.loads((self.evidence / 'license-review-input.json').read_text())
        alpha.art.write_json(self.review, {'schema': 1, 'approved': True, 'scope_sha256': scope['scope_sha256'],
            'components': {'meshbus-sdk': {'selected_license': 'Apache-2.0', 'evidence': 'fixture only',
                                         'obligations': 'retain fixture notices'}},
            'runtime_review': 'fixture only', 'generated_inputs_review': 'fixture only', 'edk_review': 'fixture only'})

    def test_complete_public_set_reuses_original_archives_and_checksums(self):
        self.approve()
        self.collect()
        expected = alpha.payload_names(TAG) | {'release-manifest.json', 'SHA256SUMS'}
        self.assertEqual({p.name for p in self.output.iterdir()}, expected)
        for p in list(self.part.glob('*.tar.gz')) + list(self.part.glob('*.tar.xz')):
            self.assertEqual(p.read_bytes(), (self.output / p.name).read_bytes())
        alpha.art.verify_checksums(self.output)
        inventory = json.loads((self.evidence / 'inventory.json').read_text())
        publish.validate_staging(self.output, inventory, TAG, SHA)
        self.assertFalse(json.loads((self.output / 'release-manifest.json').read_text())['qualification']['production_qualified'])

    def test_manual_candidate_can_supply_review_inputs_without_public_export(self):
        alpha.collect(self.candidate, self.snapshot, self.output, self.evidence, TAG, SHA, '123', self.review, True)
        self.assertTrue((self.evidence / 'license-review-input.json').is_file())
        self.assertFalse(self.output.exists())

    def test_unreviewed_or_changed_license_materials_block_public_export(self):
        self.approve()
        (self.firmware / 'licenses/meshbus/LICENSE').write_text('changed notice')
        self.repack()
        with self.assertRaisesRegex(ValueError, 'stale or unresolved'):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_missing_edk_notice_blocks_review_and_public_export(self):
        self.approve()
        edk = self.root / 'edk'
        (edk / 'NOTICE.txt').unlink()
        archive = next(self.part.glob('*-edk.tar.xz'))
        with tarfile.open(archive, 'w:xz') as stream:
            stream.add(edk, arcname='llext-edk')
        alpha.art.sidecar(archive)
        self.refresh()
        with self.assertRaises(FileNotFoundError):
            self.collect()

    def test_restricted_fonts_cannot_be_approved_by_a_generic_review(self):
        root = self.firmware / 'licenses/u8g2/fonts'
        root.mkdir(parents=True)
        font = dict(symbol='u8g2_font_fixture', family='fixture', group='fixture', license='GPL-3.0-only', status='restricted')
        alpha.art.write_json(root / 'selected-fonts.json', [font | {'attribution': 'fixture'}])
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        materials['fonts'] = [font]
        alpha.art.write_json(self.firmware / 'license-materials.json', materials)
        self.repack()
        with self.assertRaisesRegex(ValueError, 'restricted/unreviewed'):
            self.collect()

    def test_review_cannot_select_gpl3_without_an_applicable_exception(self):
        self.approve()
        review = json.loads(self.review.read_text())
        review['components']['meshbus-sdk']['selected_license'] = 'GPL-3.0-only'
        alpha.art.write_json(self.review, review)
        with self.assertRaisesRegex(ValueError, 'inadmissible selected permission'):
            self.collect()

    def test_missing_notices_and_unexpected_archive_files_are_rejected(self):
        (self.firmware / 'licenses/meshbus/LICENSE').unlink()
        self.repack()
        with self.assertRaises((FileNotFoundError, ValueError)):
            self.collect()
        (self.firmware / 'licenses/meshbus/LICENSE').write_text('fixture')
        (self.firmware / 'private.pem').write_text('unexpected fixture')
        self.repack()
        with self.assertRaisesRegex(ValueError, 'unexpected firmware archive file'):
            self.collect()

    def test_missing_matrix_product_and_edk_version_conflict_fail(self):
        self.records.pop()
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'assembled products'):
            self.collect()
        self.records.append(dict(self.records[-1], target='wio/soc'))
        self.refresh()
        self.edk_manifest['host']['version'] = '1.0.0-alpha.2'
        with self.assertRaisesRegex(ValueError, 'EDK version/source'):
            self.collect()

    def test_archive_uf2_disagreement_fails_even_with_valid_checksums(self):
        (self.firmware / 'app.uf2').write_bytes(b'different fixture')
        self.repack()
        with self.assertRaisesRegex(ValueError, 'archive UF2/APP'):
            self.collect()

    def test_dirty_sources_and_missing_qualification_are_rejected(self):
        self.record['provenance']['firmware']['dirty'] = True
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'dirty/off-manifest'):
            self.collect()
        self.record['provenance']['firmware']['dirty'] = False
        (self.part / 'candidate-validation.json').unlink()
        self.refresh()
        with self.assertRaises(FileNotFoundError):
            self.collect()

    def test_symlink_and_traversal_archives_are_rejected(self):
        for name, kind in (('../escape', tarfile.REGTYPE), ('firmware/link', tarfile.SYMTYPE)):
            archive = self.root / 'unsafe.tar.gz'
            with tarfile.open(archive, 'w:gz') as stream:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = '../escape' if kind == tarfile.SYMTYPE else ''
                stream.addfile(member, io.BytesIO())
            with self.assertRaises(ValueError):
                alpha.unpack(archive, self.root / 'extracted')


class Workflow(unittest.TestCase):
    def test_required_candidate_jobs_cannot_be_missing_failed_or_skipped(self):
        names = ['validation', 'products', 'assemble']
        needs = {n: {'result': 'success'} for n in names}
        alpha.require_jobs(needs, names)
        for name in names:
            for state in ('failure', 'skipped', 'cancelled'):
                with self.subTest(name=name, state=state), self.assertRaises(ValueError):
                    alpha.require_jobs(needs | {name: {'result': state}}, names)
        with self.assertRaises(ValueError):
            alpha.require_jobs({'validation': {'result': 'success'}}, names)

    def test_only_publisher_has_write_permission_and_candidate_remains_complete(self):
        import yaml
        root = Path(__file__).resolve().parents[3]
        workflow = yaml.safe_load((root / '.github/workflows/alpha-release.yml').read_text())
        self.assertEqual(workflow['permissions'], {'contents': 'read'})
        self.assertFalse(workflow['concurrency']['cancel-in-progress'])
        jobs = workflow['jobs']
        self.assertEqual([name for name, job in jobs.items() if job.get('permissions') == {'contents': 'write'}], ['publish'])
        self.assertEqual(set(jobs['publish']['needs']), {'preflight', 'candidate', 'stage'})
        candidate = yaml.safe_load((root / '.github/workflows/candidates.yml').read_text())
        self.assertIn('workflow_call', candidate[True])
        self.assertIn('workflow_dispatch', candidate[True])
        self.assertEqual(candidate['jobs']['validation']['with']['full'], True)
        self.assertEqual(candidate['jobs']['validation']['with']['strict'], True)
        self.assertEqual(set(candidate['jobs']['complete']['needs']), {'validation', 'products', 'assemble'})


class Transport(unittest.TestCase):
    def test_release_api_sets_draft_prerelease_and_disables_latest(self):
        github = publish.GitHub('fixture-token')
        with patch.object(github, 'request', return_value={'id': 1}) as request:
            github.create(TAG, SHA, 'notes')
            body = request.call_args.args[2]
            self.assertTrue(body['draft'])
            self.assertTrue(body['prerelease'])
            self.assertEqual(body['make_latest'], 'false')
            self.assertEqual(body['target_commitish'], SHA)
            github.expose({'id': 1})
            self.assertEqual(request.call_args.args[2], {'draft': False, 'prerelease': True, 'make_latest': 'false'})

    def test_cross_host_asset_redirect_drops_credentials(self):
        request = urllib.request.Request('https://api.github.com/asset', headers={'Authorization': 'Bearer fixture'})
        redirected = publish.SafeRedirect().redirect_request(
            request, None, 302, 'Found', {}, 'https://release-assets.githubusercontent.com/fixture')
        self.assertIsNone(redirected.get_header('Authorization'))
        with self.assertRaises(ValueError):
            publish.SafeRedirect().redirect_request(request, None, 302, 'Found', {}, 'http://example.invalid/fixture')

    def test_draft_lookup_and_asset_pagination_do_not_truncate_inventory(self):
        github = publish.GitHub('fixture-token')
        with patch.object(github, 'optional', return_value=None), \
                patch.object(github, 'request', side_effect=[list(range(100)), []]) as request:
            self.assertEqual(len(github.assets({'id': 1})), 100)
            self.assertTrue(request.call_args.args[0].endswith('page=2'))
        draft = {'tag_name': TAG, 'draft': True}
        with patch.object(github, 'optional', return_value=None), patch.object(github, 'pages', return_value=[draft]):
            self.assertEqual(github.find(TAG), draft)


if __name__ == '__main__':
    unittest.main()
