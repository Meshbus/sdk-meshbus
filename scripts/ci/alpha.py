# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Bind Alpha downloads to complete, verified candidate bytes."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'release'))
import artifacts as art  # noqa: E402

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


def payload_names(tag):
    value = version(tag)
    return {f'meshbus-{value}-mesh_probe_r1_nrf52840.uf2',
            f'meshbus-{value}-mesh_probe_r1_nrf52840-firmware.tar.gz',
            f'app-{value}-mesh_probe_r1-nrf52840-edk.tar.xz',
            f'meshbus-{value}-mesh_probe_r1_nrf52840-SBOM.spdx'}


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


def license_scope(firmware, edk, record, edk_manifest):
    """Produce exact notice/permission inputs for a separately reviewed decision."""
    materials = json.loads(art.read(firmware / 'license-materials.json'))
    art.require(materials['selection'] == 'spdx-source-components', 'partial license collection cannot be published')
    retained = set()
    for component in materials['components']:
        art.require(component['materials'], 'component has no required license material')
        for name in component['materials']:
            art.relative(name)
            art.require(name.startswith('licenses/') and art.read(firmware / name).strip(), 'missing/empty license material')
            retained.add(name)
    art.require(retained, 'missing component notices')
    runtimes = materials.get('toolchain_runtimes', [])
    art.require(len(runtimes) == 1 and runtimes[0]['domain'] == 'app' and runtimes[0]['sdk_version'] and
                set(runtimes[0]['components']) == {'toolchain-gcc-runtime', 'toolchain-picolibc'},
                'missing selected R1 toolchain runtime evidence')
    libraries = runtimes[0]['libraries']
    art.require({'libc.a', 'libgcc.a'} <= {Path(row['path']).name for row in libraries} and
                len({row['path'] for row in libraries}) == len(libraries), 'incomplete runtime linker inventory')
    for row in libraries:
        art.relative(row['path'])
        art.require(re.fullmatch(r'[0-9a-f]{64}', row['sha256']), 'invalid runtime archive digest')
    art.require({'licenses/toolchain-gcc-runtime/COPYING3', 'licenses/toolchain-gcc-runtime/COPYING.RUNTIME',
                 'licenses/toolchain-picolibc/COPYING.picolibc', 'licenses/toolchain-picolibc/COPYING.NEWLIB',
                 'licenses/toolchain-picolibc/COPYING.GPL2'} <= retained, 'missing selected runtime notice')
    fonts = materials['fonts']
    if fonts:
        font_root = firmware / 'licenses/u8g2/fonts'
        selected = json.loads(art.read(font_root / 'selected-fonts.json'))
        art.require([{k: v for k, v in row.items() if k != 'attribution'} for row in selected] == fonts,
                    'selected font notices differ from material inventory')
        for font in fonts:
            art.require(font['status'] == 'documented', 'restricted/unreviewed selected font')
        for notice in json.loads(art.read(font_root / 'sources.json'))['notices']:
            art.require(art.digest(art.read(font_root / (notice['id'] + '.txt'))) == notice['sha256'],
                        'corrupt required font notice')
    # Include the font supplemental texts, not just component material lists.
    retained |= {p.relative_to(firmware).as_posix() for p in art.files(firmware / 'licenses')}
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
            fields.pop('PackageVersion', None)  # First-party source moves with the reviewed release commit.
        packages.append(fields)
    art.require(packages, 'public SBOM contains no components')
    for name in ('LICENSE.txt', 'LICENSES/Apache-2.0.txt', 'NOTICE.txt'):
        art.require(art.read(edk / name).strip(), 'missing required EDK notice')
    art.require(art.read(edk / 'LICENSE.txt') == art.read(edk / 'LICENSES/Apache-2.0.txt'), 'EDK license text conflict')
    edk_notices = [p for p in art.files(edk) if p.name.endswith('NOTICES.md') or
                   p.relative_to(edk).as_posix() in ('LICENSE.txt', 'LICENSES/Apache-2.0.txt', 'NOTICE.txt')]
    return {'target': TARGET, 'components': sorted(packages, key=lambda p: p['PackageName']),
            'dependencies': {name: value['revision'] for name, value in record['provenance']['projects'].items()
                             if name != 'meshbus'},
            'notice_hashes': {name: art.digest(art.read(firmware / name)) for name in sorted(retained)},
            'fonts': fonts, 'toolchain': record['build'],
            'toolchain_runtimes': runtimes,
            'edk_header_policy': edk_manifest['edk']['header-policy'],
            'edk_notices': {p.relative_to(edk).as_posix(): art.digest(art.read(p)) for p in edk_notices}}


def approve_license(scope, review):
    expected = art.digest(json.dumps(scope, sort_keys=True, separators=(',', ':')).encode())
    art.require(review.get('schema') == 1 and review.get('scope_sha256') == expected and
                review.get('approved') is True, 'selected license review is missing, stale or unresolved')
    components = {p['PackageName'] for p in scope['components']}
    decisions = review.get('components', {})
    art.require(set(decisions) == components and all(p.get('selected_license') and p.get('evidence') and
                p.get('obligations') for p in decisions.values()), 'incomplete component permission decisions')
    for decision in decisions.values():
        selected = decision['selected_license']
        art.require('NOASSERTION' not in selected and 'NONE' != selected and
                    (not re.search(r'\b(?:A?GPL)-3', selected) or
                     selected == 'GPL-3.0-or-later WITH GCC-exception-3.1'), 'inadmissible selected permission')
    art.require(review.get('runtime_review') and review.get('generated_inputs_review') and review.get('edk_review'),
                'runtime/generated/EDK license review is incomplete')
    return expected


def collect(candidate, snapshot, output, evidence, tag, sha, run_id, review_path, review_only=False):
    import yaml
    import release
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
    revisions = {p['name']: p['revision'] for p in yaml.safe_load((snapshot / 'west-frozen.yml').read_text())['manifest']['projects']}
    revisions.pop('meshbus', None)
    products = assembled['products']
    art.require({p['target'] for p in products} == {t['board'] for t in release.targets()} and
                len(products) == len(release.targets()), 'missing/duplicate assembled products')
    for record in products:
        clean_source(record, sha, revisions)
        art.require(record['version'] == version(tag) and record['engineering'] is False and
                    record['validation']['build'] == 'passed', 'unqualified production-profile candidate')
    selected = next(p for p in products if p['target'] == TARGET)
    art.require(selected['format'] == 'uf2' and selected['authentication'] == 'none' and
                selected['capabilities']['llext'] is True, 'unexpected R1 product profile')
    parts = [p.parent for p in candidate.rglob('release-part.json')
             if json.loads(art.read(p)) == selected]
    art.require(len(parts) == 1, 'missing/duplicate selected product part')
    part = parts[0]
    art.verify_checksums(part)
    qualification = json.loads(art.read(part / 'candidate-validation.json'))
    art.require(qualification['source_revision'] == sha and qualification['target'] == TARGET and
                qualification['checks'] == ['production-packaging', 'edk-verify', 'edk-c-and-cxx-qualify'],
                'missing successful EDK qualification evidence')
    image = next(i for i in selected['images'] if i['domain'] == 'app')
    binary, uf2 = art.read(part / 'app.bin'), art.read(part / 'app.uf2')
    art.require(art.digest(binary) == image['sha256'], 'retained APP differs from image')
    art.require(release.verify_uf2(uf2, binary, image['address'], image['partition_address'] + image['partition_size'],
                selected['uf2']['family_id'], art.read(part / 'app.hex') if 'hex_sha256' in selected['uf2'] else None)
                == selected['uf2'], 'UF2 metadata conflict')
    firmware_archives, edk_archives = list(part.glob('*-firmware.tar.gz')), list(part.glob('*-edk.tar.xz'))
    art.require(len(firmware_archives) == len(edk_archives) == 1, 'missing firmware/EDK archives')
    firmware_archive, edk_archive = firmware_archives[0], edk_archives[0]
    for archive in (firmware_archive, edk_archive):
        art.verify_sidecar(archive)
    edk_manifest = release.cli_json('edk', 'verify', edk_archive)['manifest']
    art.require(edk_manifest['target'] == TARGET and edk_manifest['host']['version'] == version(tag) and
                edk_manifest['host']['source-revision'] == sha and edk_manifest['publishable'] is True,
                'EDK version/source conflict')
    clean_source(edk_manifest, sha, revisions)
    image_digest = edk_manifest['host'].get('image-sha256')
    if image_digest:
        art.require(image_digest == art.digest(binary), 'EDK image identity conflict')
    with tempfile.TemporaryDirectory() as temporary:
        temp = Path(temporary)
        firmware = unpack(firmware_archive, temp / 'fw', 'firmware')
        edk = unpack(edk_archive, temp / 'edk')
        art.verify_checksums(firmware)
        art.require(json.loads(art.read(firmware / 'flash-map.json')) == selected and
                    art.read(firmware / 'app.uf2') == uf2 and art.read(firmware / 'app.bin') == binary,
                    'archive UF2/APP disagrees with retained product')
        for name in ('LICENSE.txt', 'NOTICE.txt', 'SBOM.spdx', 'license-materials.json'):
            art.require(art.read(firmware / name).strip(), 'missing required firmware material')
        for p in art.files(firmware):
            relative = p.relative_to(firmware).as_posix()
            art.require(relative.startswith('licenses/') or relative in {
                'app.bin', 'app.uf2', 'app.hex', 'LICENSE.txt', 'NOTICE.txt', 'flash-map.json',
                'SHA256SUMS', 'SBOM.spdx', 'license-materials.json'}, 'unexpected firmware archive file')
            art.require('spdx-private' not in p.parts, 'private SPDX inventory in public archive')
            if p.suffix in ('.json', '.spdx'):
                portable(art.read(p).decode())
        portable(edk_manifest)
        for path in art.files(edk):
            art.require('spdx-private' not in path.parts and path.name != 'release-source.json',
                        'private evidence in public EDK')
            if path.suffix == '.json':
                portable(art.read(path).decode())
        scope = license_scope(firmware, edk, selected, edk_manifest)
        evidence.mkdir(parents=True, exist_ok=True)
        art.write_json(evidence / 'license-review-input.json', {'schema': 1, 'scope': scope,
            'scope_sha256': art.digest(json.dumps(scope, sort_keys=True, separators=(',', ':')).encode())})
        if review_only:
            return
        art.require(review_path.is_file(), 'missing reviewed scripts/ci/alpha-license-review.json; inspect license-review-input.json')
        review_sha = approve_license(scope, json.loads(art.read(review_path)))
        art.clean_destination(output)
        stem = f'meshbus-{version(tag)}-mesh_probe_r1_nrf52840'
        (output / (stem + '.uf2')).write_bytes(uf2)
        (output / (stem + '-SBOM.spdx')).write_bytes(art.read(firmware / 'SBOM.spdx'))
    for archive in (firmware_archive, edk_archive):
        art.relative(archive.name)
        shutil.copyfile(archive, output / archive.name)
    art.require({p.name for p in art.files(output)} == payload_names(tag), 'unexpected public payload filename')
    image = (snapshot / 'image.txt').read_text().strip()
    art.require(re.fullmatch(r'ghcr.io/meshbus/sdk-meshbus-builder@sha256:[0-9a-f]{64}', image), 'builder must be immutable')
    manifest = {'schema': 1, 'tag': tag, 'version': version(tag), 'repository': REPOSITORY,
        'source_revision': sha, 'dependencies': revisions, 'manifest_sha256': source['manifest_sha256'],
        'builder_image': image, 'run_id': run_id,
        'sdk_validation': sdk_validation,
        'run_url': f'https://github.com/{REPOSITORY}/actions/runs/{run_id}', 'target': TARGET,
        'toolchain': selected['build'], 'edk_tool': selected['edk_tool'],
        'license_review_scope_sha256': review_sha, 'assets': inventory(output),
        'qualification': {'alpha_eligible': True, 'production_qualified': False,
                          'authentication': selected['authentication'], 'hardware': 'not-run',
                          'candidate_validation': selected['validation'],
                          'candidate_unverified_gates': assembled['unverified_gates']}}
    portable(manifest)
    art.write_json(output / 'release-manifest.json', manifest)
    art.checksums(output)
    art.write_json(evidence / 'inventory.json', inventory(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--review', type=Path, required=True)
    parser.add_argument('--review-only', action='store_true', help='Retain review inputs without approving or staging public assets')
    args = parser.parse_args()
    tag = os.environ.get('RELEASE_TAG') or 'v' + json.loads(art.read(args.candidate / 'release.json'))['firmware_version']
    if args.review_only and '-alpha.' not in tag:
        print('Alpha review input is only generated for Alpha candidates.')
        return
    checkout_identity(Path(__file__).resolve().parents[2], tag, os.environ['GITHUB_SHA'])
    collect(args.candidate, args.snapshot, args.output, args.evidence, tag,
            os.environ['GITHUB_SHA'], os.environ['GITHUB_RUN_ID'], args.review, args.review_only)


if __name__ == '__main__':
    main()
