# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Bind Alpha downloads to complete, verified candidate bytes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import stat
import zipfile
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'release'))
import artifacts as art  # noqa: E402
import licensing  # noqa: E402
import shutil

REPOSITORY = 'Meshbus/sdk-meshbus'
TARGET = 'mesh_probe_r1/nrf52840'
MAX_ASSET = 256 * 1024 * 1024


def version(tag):
    art.require(re.fullmatch(r'v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)-alpha\.[1-9][0-9]*', tag),
                'expected a canonical vMAJOR.MINOR.PATCH-alpha.N tag')
    return tag[1:]


def identity(tag, expected_sha, actual_sha, text):
    expected = version(tag)
    art.require(re.fullmatch(r'[0-9a-f]{40}', expected_sha) and actual_sha == expected_sha,
                'tag/source revision conflict')
    fields = dict(re.findall(r'^([A-Z_]+)\s*=\s*(.*?)\s*$', text, re.M))
    actual = '.'.join(str(int(fields[key])) for key in ('VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL'))
    actual += '-' + fields['EXTRAVERSION']
    art.require(actual == expected and int(fields['VERSION_TWEAK']) == 0, 'committed VERSION differs from tag')


def checkout_identity(root, tag, sha):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()
    identity(tag, sha, git('rev-parse', 'HEAD'), git('show', 'HEAD:apps/meshbus/VERSION'))
    art.require(not git('status', '--porcelain'), 'release checkout must be clean')


def require_jobs(needs, expected):
    art.require(set(needs) == set(expected) and all(needs[name]['result'] == 'success' for name in expected),
                'complete candidate jobs did not succeed')


def entry(name, data):
    art.relative(name)
    art.require('/' not in name, 'public asset must have a flat filename')
    return {'name': name, 'size': len(data), 'sha256': art.digest(data)}


def payload_names(tag, products=None, cli=None, schema=None):
    value = version(tag)
    schema = schema or (1 if products is None else 3)
    if products is None:  # Published schema 1 R1 pilot.
        products = [{'target': TARGET, 'format': 'uf2', 'capabilities': {'llext': True}}]
    names = set()
    for product in products:
        target = product['target']
        art.require(re.fullmatch(r'[a-z0-9_]+(?:/[a-z0-9_]+)+', target), 'invalid product target')
        art.require(product['format'] in ('uf2', 'mcuboot'), 'unknown product format')
        if schema < 3:
            stem = f'meshbus-{value}-{target.replace("/", "_")}'
            names.update((stem + '-firmware.tar.gz', stem + '-SBOM.spdx',
                          stem + ('.uf2' if product['format'] == 'uf2' else '.bin')))
            if product['capabilities']['llext']:
                names.add(f'app-{value}-{target.replace("/", "-")}-edk.tar.xz')
        else:
            board = target.split('/')[0]
            name = f'meshbus-{value}-{board}.tar.gz'
            art.require(name not in names, 'asset name collision: board basename')
            names.add(name)
            if product['capabilities']['llext']:
                names.add(f'meshbus-edk-{value}-{board}.tar.xz')
    if cli:
        art.require(re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?', cli['version']), 'invalid CLI version')
        for row in cli['targets']:
            art.require(row['target'] in art.CLIENTS, 'unexpected CLI target')
            stem = f'meshbus-{cli["version"]}' if schema < 3 else f'meshbus-cli-{value}'
            names.add(f'{stem}-{row["target"]}.' +
                      ('zip' if 'windows' in row['target'] else 'tar.gz'))
    return names


def product_profiles():
    root = Path(__file__).resolve().parents[2] / 'apps/meshbus/boards'
    return {p.stem for p in root.glob('*/*/*.conf') if not p.stem.endswith('_mcuboot')}


def inventory(root):
    result = []
    for path in art.files(root):
        art.require(path.parent == root, 'unexpected nested public asset')
        result.append(entry(path.name, art.read(path, MAX_ASSET)))
    return result


def portable(value):
    text = json.dumps(value) if not isinstance(value, str) else value
    art.require(not re.search(r'(?:/home/|/Users/|/work/|/__w/|/opt/|file://|https?://[^\s/]*@)', text),
                'public metadata contains a private path or credential')


def unpack(archive, destination, prefix=None):
    """Reject unsafe entries before extracting any archive content."""
    total, names = 0, set()
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as stream:
            members = stream.infolist()
            for member in members:
                name = member.filename.rstrip('/') if member.is_dir() else member.filename
                art.relative(name)
                mode = member.external_attr >> 16
                art.require(name not in names and not stat.S_ISLNK(mode) and
                            (not stat.S_IFMT(mode) or stat.S_ISREG(mode) or stat.S_ISDIR(mode)),
                            'unsafe/duplicate archive entry')
                names.add(name)
                total += member.file_size
                art.require(total <= 512 * 1024 * 1024 and len(members) <= 100000, 'archive exceeds extraction bound')
            roots = {name.split('/')[0] for name in names}
            art.require(len(roots) == 1 and (prefix is None or roots == {prefix}), 'unexpected archive root')
            stream.extractall(destination)
        return destination / next(iter(roots))
    with tarfile.open(archive) as stream:
        members = []
        for member in stream:
            name = member.name.rstrip('/') if member.isdir() else member.name
            art.relative(name)
            art.require(name not in names and (member.isfile() or member.isdir()), 'unsafe/duplicate archive entry')
            names.add(name)
            members.append(member)
            total += member.size
            art.require(total <= 512 * 1024 * 1024 and len(members) <= 100000, 'archive exceeds extraction bound')
        roots = {m.name.split('/')[0] for m in members}
        art.require(len(roots) == 1 and (prefix is None or roots == {prefix}), 'unexpected archive root')
        stream.extractall(destination, filter='data')
    return destination / next(iter(roots))


def clean_source(record, sha, revisions):
    source = record['provenance']
    art.require(source['firmware'] == {'revision': sha, 'dirty': False} and not source.get('off_manifest', []),
                'dirty/off-manifest candidate source')
    actual = source['projects']
    art.require(set(actual) == set(revisions) | {'meshbus'}, 'candidate dependency inventory differs from snapshot')
    art.require(actual['meshbus'] == {'revision': sha, 'dirty': False}, 'SDK source conflict')
    for name, revision in revisions.items():
        art.require(re.fullmatch(r'[0-9a-f]{40}', revision) and actual[name] == {'revision': revision, 'dirty': False},
                    'candidate dependency revision conflict: ' + name)


def license_scope(firmware, edk, record, edk_manifest, evidence=None):
    """Check delivered materials and describe their inputs without approving rights."""
    evidence = evidence or firmware
    materials = json.loads(art.read(evidence / 'license-materials.json'))
    art.require(materials['selection'] == 'spdx-source-components', 'partial license collection cannot be published')
    retained = set()
    for component in materials['components']:
        art.require(component['materials'], 'component has no required license material')
        for name in component['materials']:
            art.relative(name)
            art.require(name.startswith('licenses/') and art.read(evidence / name).strip(), 'missing/empty license material')
            retained.add(name)
    art.require(retained, 'missing component notices')
    runtimes = materials.get('toolchain_runtimes', [])
    art.require(runtimes and 'app' in {row['domain'] for row in runtimes} and
                len({row['domain'] for row in runtimes}) == len(runtimes) and
                {row['domain'] for row in runtimes} == {row['domain'] for row in record['images']},
                'missing selected R1 toolchain runtime evidence' if record['target'] == TARGET
                else 'missing selected toolchain runtime evidence')
    groups = {'libc.a': 'toolchain-picolibc', 'libm.a': 'toolchain-picolibc',
              'libgcc.a': 'toolchain-gcc-runtime', 'libstdc++.a': 'toolchain-gcc-runtime',
              'libsupc++.a': 'toolchain-gcc-runtime'}
    notices = {'toolchain-gcc-runtime': ('COPYING3', 'COPYING.RUNTIME'),
               'toolchain-picolibc': ('COPYING.picolibc', 'COPYING.NEWLIB', 'COPYING.GPL2')}
    for runtime in runtimes:
        libraries = runtime['libraries']
        names = {Path(row['path']).name for row in libraries}
        art.require(runtime['sdk_version'] and libraries and names <= groups.keys() and
                    len({row['path'] for row in libraries}) == len(libraries) and
                    set(runtime['components']) == {groups[name] for name in names}, 'incomplete runtime linker inventory')
        if record['target'] == TARGET:
            art.require({'libc.a', 'libgcc.a'} <= names, 'incomplete runtime linker inventory')
        for row in libraries:
            art.relative(row['path'])
            art.require(re.fullmatch(r'[0-9a-f]{64}', row['sha256']), 'invalid runtime archive digest')
        for component in runtime['components']:
            art.require({f'licenses/{component}/{name}' for name in notices[component]} <= retained,
                        'missing selected runtime notice')
    fonts = materials['fonts']
    if fonts:
        font_root = evidence / 'licenses/u8g2/fonts'
        selected = json.loads(art.read(font_root / 'selected-fonts.json'))
        art.require([{k: v for k, v in row.items() if k != 'attribution'} for row in selected] == fonts,
                    'selected font notices differ from material inventory')
        for font in fonts:
            art.require(font['status'] == 'documented', 'restricted/unreviewed selected font')
        for notice in json.loads(art.read(font_root / 'sources.json'))['notices']:
            art.require(art.digest(art.read(font_root / (notice['id'] + '.txt'))) == notice['sha256'],
                        'corrupt required font notice')
    # Include the font supplemental texts, not just component material lists.
    retained |= {p.relative_to(evidence).as_posix() for p in art.files(evidence / 'licenses')}
    sbom = art.read(firmware / 'SBOM.spdx').decode()
    portable(sbom)
    art.require('SPDXVersion: SPDX-2.3' in sbom and 'FileName:' not in sbom, 'expected curated public SBOM')
    packages = []
    for block in re.split(r'(?=^PackageName: )', sbom, flags=re.M)[1:]:
        fields = dict(re.findall(r'^(PackageName|PackageVersion|PackageLicenseDeclared|PackageLicenseConcluded): (.*)$', block, re.M))
        if fields['PackageName'] == 'meshbus-firmware':
            art.require(fields['PackageVersion'] == record['version'], 'SBOM version conflict')
            continue
        if fields['PackageName'] == 'meshbus-sdk':
            fields.pop('PackageVersion', None)  # Source identity is retained in the release manifest.
        packages.append(fields)
    art.require(packages, 'public SBOM contains no components')
    edk_notices = []
    if edk is not None:
        if edk_manifest.get('schema', 1) == 2:
            art.require(art.digest(art.read(edk / 'NOTICE.txt')) == edk_manifest['edk']['notice-sha256'],
                        'EDK notice digest conflict')
        else:
            for name in ('LICENSE.txt', 'LICENSES/Apache-2.0.txt', 'NOTICE.txt'):
                art.require(art.read(edk / name).strip(), 'missing required EDK notice')
            art.require(art.read(edk / 'LICENSE.txt') == art.read(edk / 'LICENSES/Apache-2.0.txt'), 'EDK license text conflict')
        edk_notices = [p for p in art.files(edk) if p.name.endswith('NOTICES.md') or
                       p.relative_to(edk).as_posix() in ('LICENSE.txt', 'LICENSES/Apache-2.0.txt', 'NOTICE.txt')]
    return {'target': record['target'], 'components': sorted(packages, key=lambda p: p['PackageName']),
            'dependencies': {name: value['revision'] for name, value in record['provenance']['projects'].items()
                             if name != 'meshbus'},
            'notice_hashes': {name: art.digest(art.read(evidence / name)) for name in sorted(retained)},
            'fonts': fonts, 'toolchain': record['build'], 'toolchain_runtimes': runtimes,
            'edk_header_policy': edk_manifest['edk']['header-policy'] if edk_manifest else None,
            'edk_notices': {p.relative_to(edk).as_posix(): art.digest(art.read(p)) for p in edk_notices}}



def collect_product(record, part, tag, sha, revisions, temporary):
    import release
    target = record['target']
    llext = record['capabilities']['llext']
    art.verify_checksums(part)
    qualification = json.loads(art.read(part / 'candidate-validation.json'))
    checks = ['production-packaging'] + (['edk-verify', 'edk-c-and-cxx-qualify'] if llext else [])
    art.require(qualification['source_revision'] == sha and qualification['target'] == target and
                qualification['checks'] == checks, 'missing successful EDK qualification evidence')
    image = next(i for i in record['images'] if i['domain'] == 'app')
    binary = art.read(part / 'app.bin')
    art.require(art.digest(binary) == image['sha256'], 'retained APP differs from image')
    art.require(record['authentication'] == 'none', 'unexpected authenticated Alpha product')
    if record['format'] == 'uf2':
        uf2 = art.read(part / 'app.uf2')
        art.require(release.verify_uf2(uf2, binary, image['address'], image['partition_address'] + image['partition_size'],
                    record['uf2']['family_id'], art.read(part / 'app.hex') if 'hex_sha256' in record['uf2'] else None)
                    == record['uf2'], 'UF2 metadata conflict')
    else:
        art.require(record['format'] == 'mcuboot', 'unexpected product format')
        release.verify_mcuboot_image(binary, record['authentication'])
        art.require(image['address'] == image['partition_address'] and len(binary) <= image['partition_size'],
                    'MCUboot application exceeds recorded partition')
    firmware_archives, edk_archives = list(part.glob('*.tar.gz')), list(part.glob('*.tar.xz'))
    art.require(len(firmware_archives) == 1 and len(edk_archives) == int(llext), 'missing firmware/EDK archives')
    for archive in firmware_archives + edk_archives:
        art.verify_sidecar(archive)
    firmware_archive = firmware_archives[0]
    edk_manifest, edk = None, None
    if llext:
        edk_manifest = release.cli_json('edk', 'verify', edk_archives[0])['manifest']
        art.require(edk_manifest['target'] == target and edk_manifest['host']['version'] == version(tag) and
                    edk_manifest['host']['source-revision'] == sha and edk_manifest['publishable'] is True,
                    'EDK version/source conflict')
        clean_source(edk_manifest, sha, revisions)
        image_digest = edk_manifest['host'].get('image-sha256')
        if image_digest:
            art.require(image_digest == art.digest(binary), 'EDK image identity conflict')
        edk = unpack(edk_archives[0], temporary / 'edk')
        portable(edk_manifest)
        for path in art.files(edk):
            art.require('spdx-private' not in path.parts and path.name != 'release-source.json',
                        'private evidence in public EDK')
            if path.suffix == '.json':
                portable(art.read(path).decode())
    firmware = unpack(firmware_archive, temporary / 'fw', 'firmware')
    art.verify_checksums(firmware)
    art.require(json.loads(art.read(firmware / 'flash-map.json')) == record and
                art.read(firmware / 'app.bin') == binary and
                (record['format'] != 'uf2' or art.read(firmware / 'app.uf2') == uf2),
                'archive UF2/APP disagrees with retained product')
    compact = record.get('schema') == 2
    materials = part / 'material-evidence' if compact else firmware
    for name in ('NOTICE.txt', 'SBOM.spdx'):
        art.require(art.read(firmware / name).strip(), 'missing required firmware material')
    if compact:
        notice = licensing.combined_notice(materials)
        art.require(art.read(firmware / 'NOTICE.txt') == notice and record['license_materials'] ==
                    {'notice': 'NOTICE.txt', 'sha256': art.digest(notice)}, 'firmware notice/material conflict')
    else:
        art.require(art.read(firmware / 'LICENSE.txt').strip(), 'missing required firmware material')
        if 'license_materials' in record:
            art.require(record['license_materials'] == {'manifest': 'license-materials.json',
                        'selection': json.loads(art.read(materials / 'license-materials.json'))['selection']},
                        'firmware material inventory differs from product')
    allowed = {'app.bin', 'app.hex', 'LICENSE.txt', 'NOTICE.txt', 'flash-map.json',
               'SHA256SUMS', 'SBOM.spdx', 'license-materials.json'}
    if record['format'] == 'uf2':
        allowed.add('app.uf2')
    else:
        merged = 'firmware' if compact else 'full'
        allowed |= {'mcuboot.bin', 'mcuboot.hex', merged + '.bin', merged + '.hex'}
        segments = []
        for row in record['images']:
            data = art.read(firmware / row['file'])
            art.require(len(data) == row['size'] and art.digest(data) == row['sha256'] and
                        row['address'] >= row['partition_address'] and
                        row['address'] + len(data) <= row['partition_address'] + row['partition_size'],
                        'archived image/partition conflict')
            segments.append((row['address'], data))
        segments.sort()
        art.require(all(a + len(data) <= b for (a, data), (b, _) in zip(segments, segments[1:])),
                    'overlapping archived images')
        first, end = segments[0][0], segments[-1][0] + len(segments[-1][1])
        art.require(0 < end - first <= release.MAX_IMAGE and
                    record['full_bin'] == {'file': merged + '.bin', 'address': first, 'fill': 255},
                    'merged image layout conflict')
        full = bytearray(b'\xff' * (end - first))
        for address, data in segments:
            full[address - first:address - first + len(data)] = data
        art.require(art.read(firmware / (merged + '.bin')) == full and
                    art.read(firmware / (merged + '.hex')).decode() == release.full_hex(segments), 'merged image content conflict')
    for path in art.files(firmware):
        relative = path.relative_to(firmware).as_posix()
        art.require((not compact and relative.startswith('licenses/')) or relative in (allowed - ({'LICENSE.txt', 'license-materials.json'} if compact else set())), 'unexpected firmware archive file')
        art.require('spdx-private' not in path.parts, 'private SPDX inventory in public archive')
        if path.suffix in ('.json', '.spdx'):
            portable(art.read(path).decode())
    scope = license_scope(firmware, edk, record, edk_manifest, materials)
    board = target.split('/')[0]
    if not compact:
        notice = licensing.combined_notice(materials)
        public_record = json.loads(json.dumps(record))
        public_record['schema'] = 2
        public_record['license_materials'] = {'notice': 'NOTICE.txt', 'sha256': art.digest(notice)}
        if record['format'] == 'mcuboot':
            for extension in ('bin', 'hex'):
                (firmware / ('full.' + extension)).rename(firmware / ('firmware.' + extension))
            public_record['full_bin']['file'] = 'firmware.bin'
        shutil.rmtree(firmware / 'licenses')
        (firmware / 'license-materials.json').unlink()
        (firmware / 'LICENSE.txt').unlink()
        (firmware / 'NOTICE.txt').write_bytes(notice)
        art.write_json(firmware / 'flash-map.json', public_record)
        art.checksums(firmware)
    archive = temporary / f'meshbus-{version(tag)}-{board}.tar.gz'
    art.pack(firmware, archive, 'firmware')
    payload = {archive.name: art.read(archive, MAX_ASSET)}
    if edk is not None:
        if edk_manifest.get('schema', 1) != 2:
            terms = licensing.edk_terms(edk, art.read(edk / 'LICENSE.txt').decode())
            notice = licensing.edk_notice(edk, terms['Apache-2.0'], terms)
            for name in ('LICENSE.txt', 'LICENSES', 'ZUI-NOTICES.md', 'U8G2-NOTICES.md'):
                path = edk / name
                if path.is_dir():
                    shutil.rmtree(path)
                elif path.exists():
                    path.unlink()
            (edk / 'NOTICE.txt').write_bytes(notice)
            edk_manifest = json.loads(json.dumps(edk_manifest))
            edk_manifest['schema'] = 2
            edk_manifest['edk']['notice-sha256'] = art.digest(notice)
            edk_manifest['edk']['license-texts'] = {name: art.digest(text.encode()) for name, text in terms.items()}
            art.write_json(edk / 'edk-release.json', edk_manifest)
        archive = temporary / f'meshbus-edk-{version(tag)}-{board}.tar.xz'
        art.pack(edk, archive, 'llext-edk')
        release.cli_json('edk', 'verify', archive)
        payload[archive.name] = art.read(archive, MAX_ASSET)
    art.require(set(payload) == payload_names(tag, [record]), 'unexpected public payload filename')
    description = {key: record[key] for key in ('target', 'format', 'authentication', 'capabilities')}
    description.update(toolchain=record['build'], edk_tool=record.get('edk_tool'),
                       assets=sorted(payload), qualification=record['validation'])
    return description, scope, payload


def collect_cli(parts, native, sha, revisions, temporary, tag):
    import artifact
    import tomllib
    expected_version = tomllib.loads((Path(__file__).resolve().parents[1] / 'meshbus/Cargo.toml').read_text())['package']['version']
    records = list(parts.rglob('release-part.json'))
    proofs = [json.loads(art.read(p)) for p in art.files(native)]
    art.require(len(records) == len(proofs) == len(art.CLIENTS) and
                {p['target'] for p in proofs} == art.CLIENTS, 'missing/duplicate CLI native evidence')
    clients, scopes, payload, targets = [], [], {}, set()
    expected_checks = ['archive-checksums', 'binary-architecture', 'native-version', 'help', 'signed-fixture', 'tamper-rejection']
    for path in records:
        part = path.parent
        art.verify_checksums(part)
        record = json.loads(art.read(path))
        target = record['target']
        art.require(target in art.CLIENTS and target not in targets and record['schema'] in (1, 2) and
                    record['kind'] == 'cli' and record['publishable'] is False and
                    record['version'] == expected_version and record['development'] is False and
                    record['build_profile'] == 'release', 'CLI target/version/profile conflict')
        targets.add(target)
        clean_source(record, sha, revisions)
        compact = record['schema'] == 2
        if compact:
            art.require(record['release_version'] == version(tag), 'CLI release version conflict')
        stem = f'meshbus-cli-{version(tag)}' if compact else f'meshbus-{expected_version}'
        archive_name = f'{stem}-{target}.' + ('zip' if 'windows' in target else 'tar.gz')
        archives = list(part.glob('*.zip')) + list(part.glob('*.tar.gz'))
        art.require([p.name for p in archives] == [archive_name], 'CLI archive identity conflict')
        archive = archives[0]
        art.verify_sidecar(archive)
        root = unpack(archive, temporary / target, 'meshbus')
        art.verify_checksums(root)
        art.require(json.loads(art.read(root / 'manifest.json')) == record, 'CLI archive manifest conflict')
        executable = 'meshbus.exe' if 'windows' in target else 'meshbus'
        art.require(record['binary']['file'] == executable, 'CLI executable identity conflict')
        data = art.read(root / executable, MAX_ASSET)
        art.require(art.digest(data) == record['binary']['sha256'], 'CLI binary digest conflict')
        artifact.verify_architecture(data, target)
        proof = next(p for p in proofs if p['target'] == target)
        art.require(proof['result'] == 'passed' and proof['binary_sha256'] == record['binary']['sha256'] and
                    proof['checks'] == expected_checks and proof['hardware'] == 'not-run', 'CLI native proof conflict')
        materials = part / 'material-evidence' if compact else root
        art.require(art.read(materials / 'THIRD-PARTY-NOTICES.txt').strip(), 'missing CLI notices')
        dependencies = json.loads(art.read(materials / 'dependencies.json'))
        art.require(dependencies, 'missing CLI dependency materials')
        for dependency in dependencies:
            directory = materials / 'licenses' / art.relative(dependency['name'] + '-' + dependency['version'])
            files = art.files(directory)
            art.require(files and all(art.read(p).strip() for p in files), 'missing/empty CLI dependency notice')
        generated = json.loads(art.read(materials / 'generated-materials.json'))['protobuf_descriptor']
        art.require(generated['component'] == 'meshbus-protobufs' and generated['materials'] and
                    generated['sha256'] == record['protobuf_descriptor_sha256'], 'CLI generated material conflict')
        for name in generated['materials']:
            art.relative(name)
            art.require(name.startswith('licenses/') and art.read(materials / name).strip(), 'missing CLI generated notice')
        for p in art.files(materials):
            art.require('spdx-private' not in p.parts and p.name != 'release-source.json', 'private evidence in public CLI')
            if p.suffix == '.json':
                portable(art.read(p).decode())
        material_hashes = {p.relative_to(materials).as_posix(): art.digest(art.read(p)) for p in art.files(materials / 'licenses')}
        material_hashes['THIRD-PARTY-NOTICES.txt'] = art.digest(art.read(materials / 'THIRD-PARTY-NOTICES.txt'))
        scopes.append({'target': target, 'dependencies': dependencies, 'generated_materials': generated,
                       'notice_hashes': material_hashes, 'native_evidence_sha256': art.digest(json.dumps(proof, sort_keys=True).encode())})
        notice = licensing.combined_notice(materials)
        if compact:
            art.require(art.read(root / 'NOTICE.txt') == notice and record['license_materials'] ==
                        {'notice': 'NOTICE.txt', 'sha256': art.digest(notice)}, 'CLI notice/material conflict')
        else:
            for path in list(root.iterdir()):
                if path.name not in (executable, 'manifest.json', 'SHA256SUMS'):
                    if path.is_dir():
                        shutil.rmtree(path)
                    else:
                        path.unlink()
            (root / 'NOTICE.txt').write_bytes(notice)
            public_record = {**record, 'schema': 2, 'release_version': version(tag),
                             'license_materials': {'notice': 'NOTICE.txt', 'sha256': art.digest(notice)}}
            art.write_json(root / 'manifest.json', public_record)
            art.checksums(root)
        art.require({p.name for p in art.files(root)} == {executable, 'manifest.json', 'SHA256SUMS', 'NOTICE.txt'},
                    'unexpected CLI archive file')
        archive_name = f'meshbus-cli-{version(tag)}-{target}.' + ('zip' if 'windows' in target else 'tar.gz')
        archive = temporary / archive_name
        art.pack(root, archive, 'meshbus')
        clients.append({'target': target, 'archive': archive_name, 'binary_sha256': record['binary']['sha256'],
                        'cargo_lock_sha256': record['cargo_lock_sha256'],
                        'protobuf_descriptor_sha256': record['protobuf_descriptor_sha256'],
                        'qualification': {**record['validation'], 'native': 'passed'}, 'native_checks': proof['checks']})
        payload[archive_name] = art.read(archive, MAX_ASSET)
    art.require(targets == art.CLIENTS, 'missing CLI platform')
    return {'version': expected_version, 'release_version': version(tag), 'targets': sorted(clients, key=lambda p: p['target'])}, scopes, payload

def collect(candidate, snapshot, output, evidence, tag, sha, run_id, evidence_only=False, cli=None, native=None):
    import yaml
    import release
    art.require(cli is not None and native is not None, 'complete CLI parts and native evidence are required')
    art.verify_checksums(candidate)
    assembled = json.loads(art.read(candidate / 'release.json'))
    art.require(assembled['schema'] == 1 and assembled['publishable'] is False and
                assembled['firmware_version'] == version(tag), 'invalid assembled candidate')
    source = json.loads(art.read(snapshot / 'source.json'))
    actual_assets = [{'path': p.relative_to(candidate).as_posix(), 'size': p.stat().st_size,
                      'sha256': art.digest(art.read(p))} for p in art.files(candidate)
                     if p.name not in ('release.json', 'SHA256SUMS') or p.parent != candidate]
    art.require(assembled['assets'] == actual_assets, 'assembled inventory conflicts with candidate bytes')
    art.require(source['source_revision'] == sha, 'snapshot source conflict')
    plan = json.loads(art.read(snapshot / 'plan.json'))
    art.require(plan['full'] is True, 'Alpha requires complete candidate validation')
    if plan['sdk'] is True:
        sdk_validation = {'mode': 'fresh', 'run_id': run_id,
                          'run_url': f'https://github.com/{REPOSITORY}/actions/runs/{run_id}'}
    else:
        import baseline
        sdk_validation = baseline.verify(snapshot)
        art.require(plan.get('twister_baseline') == sdk_validation, 'Twister reuse plan/provenance conflict')
        if plan.get('product_builds') is False:
            art.require(sdk_validation['schema'] == 2, 'product reuse requires complete schema 2 evidence')
    revisions = {p['name']: p['revision'] for p in yaml.safe_load((snapshot / 'west-frozen.yml').read_text())['manifest']['projects']}
    revisions.pop('meshbus', None)
    products = assembled['products']
    art.require({p['target'] for p in products} == {t['board'] for t in release.targets()} and
                len(products) == len(release.targets()), 'missing/duplicate assembled products')
    for record in products:
        clean_source(record, sha, revisions)
        art.require(record['version'] == version(tag) and record['engineering'] is False and
                    record['validation']['build'] == 'passed', 'unqualified production-profile candidate')
    descriptions, scopes, payload = [], [], {}
    with tempfile.TemporaryDirectory() as temporary:
        temp = Path(temporary)
        for index, record in enumerate(sorted(products, key=lambda p: p['target'])):
            parts = [p.parent for p in candidate.rglob('release-part.json') if json.loads(art.read(p)) == record]
            art.require(len(parts) == 1, 'missing/duplicate selected product part')
            description, scope, assets = collect_product(record, parts[0], tag, sha, revisions, temp / str(index))
            art.require(not set(payload) & assets.keys(), 'asset name collision')
            descriptions.append(description)
            scopes.append(scope)
            payload.update(assets)
        clients, cli_scopes, cli_payload = collect_cli(cli, native, sha, revisions, temp / 'cli', tag)
        art.require(not set(payload) & cli_payload.keys(), 'asset name collision')
        payload.update(cli_payload)
    scope = {'products': scopes, 'cli': cli_scopes}
    evidence.mkdir(parents=True, exist_ok=True)
    art.write_json(evidence / 'license-evidence.json', {'schema': 2, 'scope': scope,
        'scope_sha256': art.digest(json.dumps(scope, sort_keys=True, separators=(',', ':')).encode())})
    if evidence_only:
        return
    art.require(set(payload) == payload_names(tag, descriptions, clients), 'unexpected public payload filename')
    image = (snapshot / 'image.txt').read_text().strip()
    art.require(re.fullmatch(r'ghcr.io/meshbus/sdk-meshbus-builder@sha256:[0-9a-f]{64}', image), 'builder must be immutable')
    manifest = {'schema': 3, 'tag': tag, 'version': version(tag), 'repository': REPOSITORY,
        'source_revision': sha, 'dependencies': revisions, 'manifest_sha256': source['manifest_sha256'],
        'builder_image': image, 'run_id': run_id, 'sdk_validation': sdk_validation,
        'run_url': f'https://github.com/{REPOSITORY}/actions/runs/{run_id}',
        'products': descriptions, 'cli': clients,
        'assets': [entry(name, data) for name, data in sorted(payload.items())],
        'qualification': {'alpha_eligible': True, 'production_qualified': False, 'hardware': 'not-run',
                          'candidate_unverified_gates': assembled['unverified_gates']}}
    if (snapshot / 'candidate-reuse.json').exists():
        candidate_validation = json.loads(art.read(snapshot / 'candidate-reuse.json'))
        art.require(candidate_validation['frozen_manifest_sha256'] == art.digest(art.read(snapshot / 'west-frozen.yml')),
                    'Candidate dependency qualification conflict')
        manifest['candidate_validation'] = candidate_validation
    portable(manifest)
    import publish
    publish.validate_manifest(manifest, tag, sha)
    for name, data in payload.items():
        art.relative(name)
        art.require(len(data) <= MAX_ASSET, 'public asset exceeds size limit')
    art.clean_destination(output)
    for name, data in payload.items():
        (output / name).write_bytes(data)
    art.write_json(output / 'release-manifest.json', manifest)
    art.write_json(evidence / 'inventory.json', inventory(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--evidence-only', action='store_true', help='Check and retain materials without staging public assets')
    args = parser.parse_args()
    tag = os.environ.get('RELEASE_TAG') or 'v' + json.loads(art.read(args.candidate / 'release.json'))['firmware_version']
    if args.evidence_only and '-alpha.' not in tag:
        print('Alpha license evidence is only generated for Alpha candidates.')
        return
    checkout_identity(Path(__file__).resolve().parents[2], tag, os.environ['GITHUB_SHA'])
    collect(args.candidate, args.snapshot, args.output, args.evidence, tag,
            os.environ['GITHUB_SHA'], os.environ['GITHUB_RUN_ID'], args.evidence_only, cli=args.cli, native=args.native)


if __name__ == '__main__':
    main()
