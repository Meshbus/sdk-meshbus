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
        alpha.art.write_json(self.snapshot / 'plan.json', {'full': True, 'sdk': True})
        alpha.art.write_json(self.snapshot / 'source.json', {'source_revision': SHA, 'manifest_sha256': 'd' * 64})
        (self.snapshot / 'west-frozen.yml').write_text('manifest:\n  projects:\n  - name: zephyr\n    revision: ' + 'b' * 40 + '\n')
        (self.snapshot / 'image.txt').write_text('ghcr.io/meshbus/sdk-meshbus-builder@sha256:' + 'c' * 64)
        provenance = {'firmware': {'revision': SHA, 'dirty': False}, 'off_manifest': [],
                      'projects': {'meshbus': {'revision': SHA, 'dirty': False},
                                   'zephyr': {'revision': 'b' * 40, 'dirty': False}}}
        self.matrix = [{'board': target, 'id': target.split('/')[0]} for target in
                       (alpha.TARGET,)]
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
            'components': [{'component': 'meshbus', 'materials': ['licenses/meshbus/LICENSE']}], 'fonts': [],
            'toolchain_runtimes': [{'domain': 'app', 'sdk_version': '1.0.1',
                'components': ['toolchain-gcc-runtime', 'toolchain-picolibc'],
                'libraries': [{'path': 'lib/' + name, 'sha256': 'f' * 64} for name in ('libc.a', 'libgcc.a')]}]})
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        for component, filenames in (('toolchain-gcc-runtime', ('COPYING3', 'COPYING.RUNTIME')),
                                     ('toolchain-picolibc', ('COPYING.picolibc', 'COPYING.NEWLIB', 'COPYING.GPL2'))):
            paths = []
            for name in filenames:
                path = self.firmware / 'licenses' / component / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('runtime permission fixture')
                paths.append(path.relative_to(self.firmware).as_posix())
            materials['components'].append({'component': component, 'materials': paths})
        alpha.art.write_json(self.firmware / 'license-materials.json', materials)
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
        self.cli, self.native = self.root / 'cli-parts', self.root / 'native'
        self.cli.mkdir()
        self.native.mkdir()
        for target in alpha.art.CLIENTS:
            part = self.cli / target
            part.mkdir()
            root = self.root / ('cli-root-' + target)
            (root / 'licenses/meshbus-cli-1.0.0').mkdir(parents=True)
            (root / 'licenses/meshbus-protobufs').mkdir()
            (root / 'licenses/meshbus-cli-1.0.0/LICENSE').write_text('CLI license fixture')
            (root / 'licenses/meshbus-protobufs/LICENSE').write_text('schema license fixture')
            (root / 'THIRD-PARTY-NOTICES.txt').write_text('CLI attribution fixture')
            arm = target.startswith('aarch64-')
            if 'linux' in target:
                data = bytearray(20)
                data[:6] = b'\x7fELF\x02\x01'
                data[18:20] = (183 if arm else 62).to_bytes(2, 'little')
            elif 'darwin' in target:
                data = b'\xcf\xfa\xed\xfe' + (0x100000c if arm else 0x1000007).to_bytes(4, 'little')
            else:
                data = bytearray(70)
                data[:2] = b'MZ'
                data[60:64] = (64).to_bytes(4, 'little')
                data[64:68] = b'PE\x00\x00'
                data[68:70] = (0xaa64 if arm else 0x8664).to_bytes(2, 'little')
            executable = 'meshbus.exe' if 'windows' in target else 'meshbus'
            (root / executable).write_bytes(data)
            record = {'schema': 1, 'kind': 'cli', 'target': target, 'version': '1.0.0',
                      'publishable': False, 'development': False, 'build_profile': 'release',
                      'provenance': provenance, 'binary': {'file': executable, 'sha256': alpha.art.digest(data)},
                      'cargo_lock_sha256': 'e' * 64, 'protobuf_descriptor_sha256': 'f' * 64,
                      'validation': {'code_signing': 'not-run', 'notarization': 'not-run', 'hardware': 'not-run'}}
            alpha.art.write_json(root / 'manifest.json', record)
            alpha.art.write_json(part / 'release-part.json', record)
            alpha.art.write_json(root / 'dependencies.json', [{'name': 'meshbus-cli', 'version': '1.0.0'}])
            alpha.art.write_json(root / 'generated-materials.json', {'protobuf_descriptor': {
                'component': 'meshbus-protobufs', 'materials': ['licenses/meshbus-protobufs/LICENSE'], 'sha256': 'f' * 64}})
            alpha.art.checksums(root)
            archive = part / (f'meshbus-1.0.0-{target}.' + ('zip' if 'windows' in target else 'tar.gz'))
            alpha.art.pack(root, archive, 'meshbus')
            alpha.art.checksums(part)
            alpha.art.write_json(self.native / (target + '.json'), {'target': target,
                'binary_sha256': record['binary']['sha256'], 'result': 'passed', 'hardware': 'not-run',
                'checks': ['archive-checksums', 'binary-architecture', 'native-version', 'help', 'signed-fixture', 'tamper-rejection']})
        self.output = self.root / 'public'
        self.evidence = self.root / 'evidence'
        self.patchers = [patch.object(release, 'targets', return_value=self.matrix),
                         patch.object(alpha, 'product_profiles', side_effect=lambda: {t['board'].replace('/', '_') for t in self.matrix}),
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

    def collect(self, evidence_only=False):
        alpha.collect(self.candidate, self.snapshot, self.output, self.evidence, TAG, SHA, '123', evidence_only,
                      cli=self.cli, native=self.native)

    def test_complete_public_set_has_compact_archives_and_manifest_integrity(self):
        self.collect()
        expected = alpha.payload_names(TAG, self.records, {'version': '1.0.0', 'targets': [{'target': t} for t in alpha.art.CLIENTS]}) | {'release-manifest.json'}
        self.assertEqual({p.name for p in self.output.iterdir()}, expected)
        with tarfile.open(self.output / 'meshbus-1.0.0-alpha.1-mesh_probe_r1.tar.gz') as archive:
            self.assertEqual({m.name for m in archive if m.isfile()}, {
                'firmware/app.bin', 'firmware/app.uf2', 'firmware/flash-map.json',
                'firmware/SBOM.spdx', 'firmware/NOTICE.txt', 'firmware/SHA256SUMS'})
        self.assertFalse((self.output / 'SHA256SUMS').exists())
        publish.validate_staging(self.output, alpha.inventory(self.output), TAG, SHA)
        inventory = json.loads((self.evidence / 'inventory.json').read_text())
        publish.validate_staging(self.output, inventory, TAG, SHA)
        manifest = json.loads((self.output / 'release-manifest.json').read_text())
        self.assertEqual(manifest['schema'], 3)
        self.assertFalse(manifest['qualification']['production_qualified'])
        self.assertNotIn('license_review_scope_sha256', manifest)
        self.assertFalse((self.root / 'review.json').exists())

    def test_manual_candidate_can_supply_material_evidence_without_public_export(self):
        self.collect(evidence_only=True)
        self.assertTrue((self.evidence / 'license-evidence.json').is_file())
        self.assertFalse(self.output.exists())

    def compact_parts(self):
        self.record['schema'] = 2
        self.record['license_materials'] = alpha.licensing.separate_evidence(self.firmware, self.part)
        alpha.art.write_json(self.firmware / 'flash-map.json', self.record)
        self.repack()
        for target in alpha.art.CLIENTS:
            part = self.cli / target
            root = self.root / ('cli-root-' + target)
            record = json.loads((part / 'release-part.json').read_text())
            record.update(schema=2, release_version=TAG[1:],
                          license_materials=alpha.licensing.separate_evidence(root, part))
            for path in list(part.glob('*.zip')) + list(part.glob('*.tar.gz')):
                path.unlink()
                path.with_name(path.name + '.sha256').unlink()
            alpha.art.write_json(part / 'release-part.json', record)
            alpha.art.write_json(root / 'manifest.json', record)
            alpha.art.checksums(root)
            archive = part / (f'meshbus-cli-{TAG[1:]}-{target}.' + ('zip' if 'windows' in target else 'tar.gz'))
            alpha.art.pack(root, archive, 'meshbus')
            alpha.art.checksums(part)
        self.refresh()

    def test_compact_parts_stage_and_evidence_only_keep_private_materials(self):
        self.compact_parts()
        self.collect(evidence_only=True)
        self.assertFalse(self.output.exists())
        self.collect()
        self.assertTrue((self.part / 'material-evidence/license-materials.json').is_file())
        archive = self.output / f'meshbus-cli-{TAG[1:]}-aarch64-apple-darwin.tar.gz'
        with tarfile.open(archive) as stream:
            self.assertEqual({m.name for m in stream if m.isfile()}, {
                'meshbus/meshbus', 'meshbus/manifest.json', 'meshbus/NOTICE.txt', 'meshbus/SHA256SUMS'})

    def test_compact_notice_tamper_fails_even_with_fresh_archive_checksums(self):
        self.compact_parts()
        (self.firmware / 'NOTICE.txt').write_text('truncated')
        self.repack()
        with self.assertRaisesRegex(ValueError, 'notice/material conflict'):
            self.collect()

    def test_compact_missing_runtime_material_still_blocks(self):
        self.compact_parts()
        (self.part / 'material-evidence/licenses/toolchain-picolibc/COPYING.picolibc').unlink()
        self.refresh()
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.collect()

    def test_schema_three_rejects_outer_checksum_asset(self):
        self.collect()
        alpha.art.checksums(self.output)
        with self.assertRaisesRegex(ValueError, 'payload inventory conflict'):
            publish.validate_staging(self.output, alpha.inventory(self.output), TAG, SHA)

    def test_public_manifest_identifies_reused_twister_baseline(self):
        source = json.loads((self.snapshot / 'source.json').read_text())
        baseline = {'schema': 1, 'mode': 'reused', 'repository': alpha.REPOSITORY,
                    'run_id': '456', 'run_attempt': 1,
                    'run_url': f'https://github.com/{alpha.REPOSITORY}/actions/runs/456',
                    'source_revision': SHA, 'manifest_sha256': source['manifest_sha256'],
                    'builder_image': (self.snapshot / 'image.txt').read_text(),
                    'frozen_manifest_sha256': alpha.art.digest((self.snapshot / 'west-frozen.yml').read_bytes()),
                    'jobs': ['validation / Twister Run (1)', 'validation / Twister Build (1)']}
        alpha.art.write_json(self.snapshot / 'twister-baseline.json', baseline)
        alpha.art.write_json(self.snapshot / 'plan.json', {'full': True, 'sdk': False, 'twister_baseline': baseline})
        self.collect()
        manifest = json.loads((self.output / 'release-manifest.json').read_text())
        self.assertEqual(manifest['sdk_validation'], baseline)
        self.assertEqual(manifest['run_id'], '123')

    def test_alpha_cannot_silently_omit_twister(self):
        alpha.art.write_json(self.snapshot / 'plan.json', {'full': True, 'sdk': False})
        with self.assertRaises(FileNotFoundError):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_conflicting_edk_license_text_blocks_staging(self):
        edk = self.root / 'edk'
        (edk / 'LICENSE.txt').write_text('conflicting license fixture')
        archive = next(self.part.glob('*-edk.tar.xz'))
        with tarfile.open(archive, 'w:xz') as stream:
            stream.add(edk, arcname='llext-edk')
        alpha.art.sidecar(archive)
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'EDK license text conflict'):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_changed_materials_update_evidence_without_reapproval(self):
        self.collect(evidence_only=True)
        before = json.loads((self.evidence / 'license-evidence.json').read_text())
        (self.firmware / 'licenses/meshbus/LICENSE').write_text('changed notice')
        self.repack()
        self.collect()
        after = json.loads((self.evidence / 'license-evidence.json').read_text())
        self.assertNotEqual(before['scope_sha256'], after['scope_sha256'])
        self.assertEqual(after['scope']['products'][0]['notice_hashes']['licenses/meshbus/LICENSE'],
                         alpha.art.digest(b'changed notice'))
        self.assertNotIn('approved', after)

    def test_noassertion_fields_do_not_require_an_approval_file(self):
        path = self.firmware / 'SBOM.spdx'
        path.write_text(path.read_text().replace('Apache-2.0', 'NOASSERTION'))
        self.repack()
        self.collect()
        with tarfile.open(self.output / 'meshbus-1.0.0-alpha.1-mesh_probe_r1.tar.gz') as archive:
            public_sbom = archive.extractfile('firmware/SBOM.spdx').read().decode()
        self.assertIn('PackageLicenseDeclared: NOASSERTION', public_sbom)
        self.assertIn('PackageLicenseConcluded: NOASSERTION', public_sbom)

    def test_corrupt_notice_with_stale_archive_checksum_blocks_staging(self):
        (self.firmware / 'licenses/meshbus/LICENSE').write_text('corrupt fixture')
        archive = next(self.part.glob('*-firmware.tar.gz'))
        alpha.art.pack(self.firmware, archive, 'firmware')
        self.refresh()
        with self.assertRaises(ValueError):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_candidate_without_runtime_notice_inventory_cannot_be_staged(self):
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        materials.pop('toolchain_runtimes')
        alpha.art.write_json(self.firmware / 'license-materials.json', materials)
        self.repack()
        with self.assertRaisesRegex(ValueError, 'missing selected R1 toolchain runtime'):
            self.collect()

    def test_changed_linked_runtime_digest_updates_material_evidence(self):
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        materials['toolchain_runtimes'][0]['libraries'][0]['sha256'] = 'e' * 64
        alpha.art.write_json(self.firmware / 'license-materials.json', materials)
        self.repack()
        self.collect()
        evidence = json.loads((self.evidence / 'license-evidence.json').read_text())
        self.assertEqual(evidence['scope']['products'][0]['toolchain_runtimes'][0]['libraries'][0]['sha256'], 'e' * 64)

    def test_missing_edk_notice_blocks_public_export(self):
        edk = self.root / 'edk'
        (edk / 'NOTICE.txt').unlink()
        archive = next(self.part.glob('*-edk.tar.xz'))
        with tarfile.open(archive, 'w:xz') as stream:
            stream.add(edk, arcname='llext-edk')
        alpha.art.sidecar(archive)
        self.refresh()
        with self.assertRaises(FileNotFoundError):
            self.collect()

    def test_restricted_fonts_block_public_export(self):
        root = self.firmware / 'licenses/u8g2/fonts'
        root.mkdir(parents=True)
        font = dict(symbol='u8g2_font_fixture', family='fixture', group='fixture', license='GPL-3.0-only', status='restricted')
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        for status in ('restricted', 'review-required'):
            with self.subTest(status=status):
                font['status'] = status
                alpha.art.write_json(root / 'selected-fonts.json', [font | {'attribution': 'fixture'}])
                materials['fonts'] = [font]
                alpha.art.write_json(self.firmware / 'license-materials.json', materials)
                self.repack()
                with self.assertRaisesRegex(ValueError, 'restricted/unreviewed'):
                    self.collect()
                self.assertFalse(self.output.exists())

    def test_corrupt_or_missing_font_material_blocks_staging(self):
        root = self.firmware / 'licenses/u8g2/fonts'
        root.mkdir(parents=True)
        font = dict(symbol='u8g2_font_fixture', family='fixture', group='fixture',
                    license='MIT', status='documented')
        alpha.art.write_json(root / 'selected-fonts.json', [font | {'attribution': 'fixture'}])
        alpha.art.write_json(root / 'sources.json', {'notices': [{'id': 'fixture',
                             'sha256': alpha.art.digest(b'original notice')}]})
        materials = json.loads((self.firmware / 'license-materials.json').read_text())
        materials['fonts'] = [font]
        alpha.art.write_json(self.firmware / 'license-materials.json', materials)
        (root / 'fixture.txt').write_text('corrupt notice')
        self.repack()
        with self.assertRaisesRegex(ValueError, 'corrupt required font notice'):
            self.collect()
        (root / 'fixture.txt').unlink()
        self.repack()
        with self.assertRaises(FileNotFoundError):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_missing_or_empty_runtime_notices_block_staging(self):
        path = self.firmware / 'licenses/toolchain-gcc-runtime/COPYING.RUNTIME'
        for content in (None, ''):
            with self.subTest(content=content):
                if content is None:
                    path.unlink()
                else:
                    path.write_text(content)
                self.repack()
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.collect()
                self.assertFalse(self.output.exists())

    def test_cli_evidence_only_and_removed_review_arguments(self):
        import contextlib
        arguments = ['alpha.py', '--candidate', str(self.candidate), '--snapshot', str(self.snapshot),
                     '--output', str(self.output), '--evidence', str(self.evidence),
                     '--cli', str(self.cli), '--native', str(self.native)]
        for obsolete in (['--review', 'review.json'], ['--review-only']):
            with self.subTest(obsolete=obsolete), patch.object(sys, 'argv', arguments + obsolete), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                alpha.main()
            self.assertEqual(result.exception.code, 2)
        environment = {'RELEASE_TAG': TAG, 'GITHUB_SHA': SHA, 'GITHUB_RUN_ID': '123'}
        with patch.object(sys, 'argv', arguments + ['--evidence-only']), \
                patch.dict('os.environ', environment), patch.object(alpha, 'checkout_identity'):
            alpha.main()
        self.assertTrue((self.evidence / 'license-evidence.json').is_file())
        self.assertFalse(self.output.exists())

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
        saved = self.records.pop()
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'assembled products'):
            self.collect()
        self.records.append(saved)
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

    def test_manifest_cannot_omit_board_cli_or_change_native_qualification(self):
        import copy
        self.collect()
        manifest = json.loads((self.output / 'release-manifest.json').read_text())
        for mutation in ('board', 'cli', 'qualification', 'archive'):
            changed = copy.deepcopy(manifest)
            if mutation == 'board':
                changed['products'].pop()
            elif mutation == 'cli':
                changed['cli']['targets'].pop()
            elif mutation == 'qualification':
                changed['cli']['targets'][0]['qualification']['native'] = 'failed'
            else:
                changed['cli']['targets'][0]['archive'] = 'unexpected.zip'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                publish.validate_manifest(changed, TAG, SHA)

    def test_zip_symlink_and_duplicate_members_are_rejected(self):
        import zipfile
        for symlink in (True, False):
            archive = self.root / 'unsafe.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                info = zipfile.ZipInfo('meshbus/entry')
                info.create_system = 3
                info.external_attr = (0o120777 if symlink else 0o100644) << 16
                stream.writestr(info, 'fixture')
                if not symlink:
                    import warnings
                    with warnings.catch_warnings():
                        warnings.simplefilter('ignore', UserWarning)
                        stream.writestr('meshbus/entry', 'duplicate')
            with self.assertRaises(ValueError):
                alpha.unpack(archive, self.root / 'unsafe-output', 'meshbus')

    def repack_cli(self, target):
        part = self.cli / target
        root = self.root / ('cli-root-' + target)
        alpha.art.checksums(root)
        archive = next(iter(list(part.glob('*.zip')) + list(part.glob('*.tar.gz'))))
        alpha.art.pack(root, archive, 'meshbus')
        alpha.art.checksums(part)

    def test_missing_or_duplicate_cli_and_native_evidence_block_staging(self):
        import shutil
        target = sorted(alpha.art.CLIENTS)[0]
        proof = self.native / (target + '.json')
        saved = proof.read_bytes()
        proof.unlink()
        with self.assertRaisesRegex(ValueError, 'CLI native evidence'):
            self.collect()
        proof.write_bytes(saved)
        shutil.copytree(self.cli / target, self.cli / 'duplicate')
        with self.assertRaisesRegex(ValueError, 'CLI native evidence'):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_stale_cli_native_digest_blocks_staging(self):
        path = next(self.native.iterdir())
        proof = json.loads(path.read_text())
        proof['binary_sha256'] = '0' * 64
        alpha.art.write_json(path, proof)
        with self.assertRaisesRegex(ValueError, 'CLI native proof'):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_missing_cli_license_and_generated_materials_block_staging(self):
        target = sorted(alpha.art.CLIENTS)[0]
        root = self.root / ('cli-root-' + target)
        notice = root / 'licenses/meshbus-cli-1.0.0/LICENSE'
        saved = notice.read_bytes()
        notice.unlink()
        self.repack_cli(target)
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.collect()
        notice.write_bytes(saved)
        (root / 'licenses/meshbus-protobufs/LICENSE').unlink()
        self.repack_cli(target)
        with self.assertRaises(FileNotFoundError):
            self.collect()
        self.assertFalse(self.output.exists())

    def test_cli_version_and_source_conflicts_block_staging(self):
        path = next(self.cli.rglob('release-part.json'))
        record = json.loads(path.read_text())
        record['version'] = TAG[1:]
        alpha.art.write_json(path, record)
        alpha.art.checksums(path.parent)
        with self.assertRaisesRegex(ValueError, 'CLI target/version/profile'):
            self.collect()
        record['version'] = '1.0.0'
        record['provenance']['firmware']['revision'] = '0' * 40
        alpha.art.write_json(path, record)
        alpha.art.checksums(path.parent)
        with self.assertRaisesRegex(ValueError, 'dirty/off-manifest'):
            self.collect()

    def test_four_products_include_verified_mcuboot_images_and_all_cli(self):
        import copy
        import shutil
        targets = ['mesh_probe_r2/nrf54l15/cpuapp', 'tracker_t1000_e/nrf52840', 'wio_tracker_l1/nrf52840']
        manifests = {alpha.TARGET: self.edk_manifest}
        for target in targets:
            record = copy.deepcopy(self.record)
            record.update(target=target, id=target.split('/')[0])
            part = self.candidate / 'firmware' / record['id']
            part.mkdir()
            firmware = self.root / ('firmware-' + record['id'])
            shutil.copytree(self.firmware, firmware)
            if target.startswith('mesh_probe_r2/'):
                record.update(format='mcuboot', capabilities={'llext': False})
                record.pop('uf2')
                record.pop('edk_tool')
                (firmware / 'app.uf2').unlink()
                header = struct.pack('<IIHHIIBBHII', 0x96F3B83D, 0, 32, 0, 4, 0, 1, 0, 0, 0, 0)
                body = header + b'app!'
                binary = body + struct.pack('<HHHH', 0x6907, 40, 0x10, 32) + bytes.fromhex(alpha.art.digest(body))
                (firmware / 'app.bin').write_bytes(binary)
                (firmware / 'mcuboot.bin').write_bytes(b'boot')
                record['images'] = [dict(domain=domain, file=domain + '.bin', address=address,
                    partition_address=address, partition_size=4096, size=len(data), sha256=alpha.art.digest(data))
                    for domain, address, data in [('mcuboot', 0x1000, b'boot'), ('app', 0x2000, binary)]]
                segments = [(0x1000, b'boot'), (0x2000, binary)]
                (firmware / 'full.bin').write_bytes(b'boot' + b'\xff' * (4096 - 4) + binary)
                (firmware / 'full.hex').write_text(self.release.full_hex(segments))
                record['full_bin'] = {'file': 'full.bin', 'address': 0x1000, 'fill': 255}
                materials = json.loads((firmware / 'license-materials.json').read_text())
                materials['toolchain_runtimes'].append({**materials['toolchain_runtimes'][0], 'domain': 'mcuboot'})
                alpha.art.write_json(firmware / 'license-materials.json', materials)
                record['license_materials'] = {'manifest': 'license-materials.json', 'selection': 'spdx-source-components'}
            else:
                (part / 'app.uf2').write_bytes((firmware / 'app.uf2').read_bytes())
                manifest = copy.deepcopy(self.edk_manifest)
                manifest['target'] = target
                manifests[target] = manifest
                archive = part / f'app-{TAG[1:]}-{target.replace("/", "-")}-edk.tar.xz'
                shutil.copyfile(next(self.part.glob('*-edk.tar.xz')), archive)
                alpha.art.sidecar(archive)
            (part / 'app.bin').write_bytes((firmware / 'app.bin').read_bytes())
            alpha.art.write_json(firmware / 'flash-map.json', record)
            alpha.art.checksums(firmware)
            alpha.art.pack(firmware, part / f'meshbus-{TAG[1:]}-{target.replace("/", "_")}-firmware.tar.gz', 'firmware')
            alpha.art.write_json(part / 'release-part.json', record)
            alpha.art.write_json(part / 'candidate-validation.json', {'target': target, 'source_revision': SHA,
                'checks': ['production-packaging'] + (['edk-verify', 'edk-c-and-cxx-qualify'] if record['capabilities']['llext'] else [])})
            alpha.art.checksums(part)
            self.records.append(record)
            self.matrix.append({'board': target, 'id': record['id']})
        self.refresh()
        def verify(*args):
            target = next(t for t in manifests if t.split('/')[0] in str(args[-1]))
            return {'manifest': manifests[target]}
        with patch.object(self.release, 'cli_json', side_effect=verify):
            self.collect()
        manifest = json.loads((self.output / 'release-manifest.json').read_text())
        self.assertEqual(len(manifest['products']), 4)
        self.assertEqual(len(manifest['cli']['targets']), 6)
        self.assertEqual(manifest['cli']['version'], '1.0.0')
        self.assertIn('meshbus-1.0.0-alpha.1-mesh_probe_r2.tar.gz', {p.name for p in self.output.iterdir()})
        publish.validate_staging(self.output, alpha.inventory(self.output), TAG, SHA)
        scope = json.loads((self.evidence / 'license-evidence.json').read_text())['scope']
        r2 = next(p for p in scope['products'] if p['target'].startswith('mesh_probe_r2/'))
        self.assertEqual({r['domain'] for r in r2['toolchain_runtimes']}, {'app', 'mcuboot'})
        self.assertFalse(any('full.bin' == p.name for p in self.output.iterdir()))



class MatrixPublication(unittest.TestCase):
    def test_board_basename_collision_is_rejected(self):
        products = [{'target': 'board/soc', 'format': 'uf2', 'capabilities': {'llext': False}},
                    {'target': 'board/other', 'format': 'uf2', 'capabilities': {'llext': False}}]
        with self.assertRaisesRegex(ValueError, 'asset name collision'):
            alpha.payload_names(TAG, products)

    def test_matrix_names_include_mcuboot_and_all_cli_archives(self):
        products = [{'target': alpha.TARGET, 'format': 'uf2', 'capabilities': {'llext': True}},
                    {'target': 'mesh_probe_r2/nrf54l15/cpuapp', 'format': 'mcuboot',
                     'capabilities': {'llext': False}}]
        clients = {'version': '1.0.0', 'targets': [{'target': t} for t in alpha.art.CLIENTS]}
        names = alpha.payload_names(TAG, products, clients)
        self.assertIn('meshbus-1.0.0-alpha.1-mesh_probe_r2.tar.gz', names)
        self.assertIn('meshbus-cli-1.0.0-alpha.1-x86_64-pc-windows-msvc.zip', names)
        self.assertIn('meshbus-cli-1.0.0-alpha.1-aarch64-apple-darwin.tar.gz', names)
        self.assertEqual(len(names), 9)

    def test_matrix_missing_cli_inputs_block_collection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'CLI'):
                alpha.collect(root, root, root / 'out', root / 'evidence', TAG, SHA, '123')


class Workflow(unittest.TestCase):
    def test_workflows_use_material_evidence_interfaces(self):
        import shlex
        import yaml
        root = Path(__file__).resolve().parents[3]
        environment = {'RELEASE_TAG': TAG, 'GITHUB_SHA': SHA, 'GITHUB_RUN_ID': '123'}
        for filename, job, evidence_only in (('candidates.yml', 'assemble', True),
                                            ('alpha-release.yml', 'stage', False)):
            with self.subTest(workflow=filename):
                workflow = yaml.safe_load((root / '.github/workflows' / filename).read_text())
                steps = workflow['jobs'][job]['steps']
                command = next(step['run'] for step in steps if 'scripts/ci/alpha.py' in step.get('run', ''))
                with patch.object(sys, 'argv', shlex.split(command)[1:]), \
                        patch.dict('os.environ', environment), patch.object(alpha, 'checkout_identity'), \
                        patch.object(alpha, 'collect') as collect:
                    alpha.main()
                self.assertEqual(collect.call_args.args[-1], evidence_only)
                if evidence_only:
                    upload = next(step for step in steps if step.get('with', {}).get('name') == 'alpha-license-evidence')
                    self.assertEqual(upload['with']['path'], str(collect.call_args.args[3]))

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
        self.assertEqual(workflow['permissions'], {'contents': 'read', 'actions': 'read'})
        self.assertFalse(workflow['concurrency']['cancel-in-progress'])
        jobs = workflow['jobs']
        self.assertEqual([name for name, job in jobs.items() if job.get('permissions') == {'contents': 'write'}], ['publish'])
        self.assertEqual(set(jobs['publish']['needs']), {'preflight', 'candidate', 'stage'})
        candidate = yaml.safe_load((root / '.github/workflows/candidates.yml').read_text())
        self.assertIn('workflow_call', candidate[True])
        self.assertIn('workflow_dispatch', candidate[True])
        self.assertEqual(candidate['jobs']['validation']['with']['full'], True)
        self.assertEqual(candidate['jobs']['validation']['with']['strict'], True)
        self.assertEqual(candidate['jobs']['validation']['with']['reuse-twister'], True)
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
