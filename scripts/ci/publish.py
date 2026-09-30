# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Publish verified Alpha assets through an owned draft; never replace bytes."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.error
import urllib.parse
import urllib.request

import alpha


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, url):
        alpha.art.require(urllib.parse.urlsplit(url).scheme == 'https', 'insecure download redirect')
        redirected = super().redirect_request(request, fp, code, message, headers, url)
        if urllib.parse.urlsplit(request.full_url).netloc != urllib.parse.urlsplit(url).netloc:
            redirected.remove_header('Authorization')
        return redirected


class GitHub:
    def __init__(self, token):
        alpha.art.require(bool(token), 'GITHUB_TOKEN is required')
        self.token = token
        self.base = 'https://api.github.com/repos/' + alpha.REPOSITORY
        self.opener = urllib.request.build_opener(SafeRedirect())

    def request(self, url, method='GET', value=None, binary=False, anonymous=False):
        headers = {'Accept': 'application/octet-stream' if binary else 'application/vnd.github+json',
                   'User-Agent': 'meshbus-alpha-ci', 'X-GitHub-Api-Version': '2022-11-28'}
        if not anonymous:
            alpha.art.require(urllib.parse.urlsplit(url).hostname in ('api.github.com', 'uploads.github.com'),
                              'refusing to send credentials to a download host')
            headers['Authorization'] = 'Bearer ' + self.token
        data = value
        if isinstance(value, dict):
            data = json.dumps(value).encode()
            headers['Content-Type'] = 'application/json'
        elif isinstance(value, bytes):
            headers['Content-Type'] = 'application/octet-stream'
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        with self.opener.open(request, timeout=120) as response:
            limit = alpha.MAX_ASSET if binary else 8 * 1024 * 1024
            body = response.read(limit + 1)
            alpha.art.require(len(body) <= limit, 'GitHub response exceeds bound')
            return body if binary else json.loads(body)

    def optional(self, path):
        try:
            return self.request(self.base + path)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise

    def pages(self, path):
        values = []
        for page in range(1, 101):
            batch = self.request(f'{self.base}{path}?per_page=100&page={page}')
            values.extend(batch)
            if len(batch) < 100:
                return values
        raise ValueError('GitHub inventory pagination exceeds bound')

    def tag_sha(self, tag):
        obj = self.request(self.base + '/git/ref/tags/' + urllib.parse.quote(tag, safe=''))['object']
        for _ in range(8):
            if obj['type'] == 'commit':
                return obj['sha']
            alpha.art.require(obj['type'] == 'tag', 'tag must resolve to a commit')
            obj = self.request(self.base + '/git/tags/' + obj['sha'])['object']
        raise ValueError('tag nesting exceeds bound')

    def find(self, tag):
        published = self.optional('/releases/tags/' + urllib.parse.quote(tag, safe=''))
        if published:
            return published
        # The tag endpoint returns published releases; draft recovery needs listing.
        matches = [r for r in self.pages('/releases') if r['tag_name'] == tag]
        alpha.art.require(len(matches) <= 1, 'conflicting duplicate releases')
        return matches[0] if matches else None

    def assets(self, release):
        return self.pages(f'/releases/{release["id"]}/assets')

    def download(self, asset, anonymous=False):
        url = asset['browser_download_url'] if anonymous else self.base + f'/releases/assets/{asset["id"]}'
        if anonymous:
            alpha.art.require(urllib.parse.urlsplit(url).hostname == 'github.com', 'unexpected public download URL')
        return self.request(url, binary=True, anonymous=anonymous)

    def create(self, tag, sha, body):
        return self.request(self.base + '/releases', 'POST', {
            'tag_name': tag, 'target_commitish': sha, 'name': 'Meshbus ' + alpha.version(tag),
            'body': body, 'draft': True, 'prerelease': True, 'make_latest': 'false'})

    def upload(self, release, name, data):
        url = f'https://uploads.github.com/repos/{alpha.REPOSITORY}/releases/{release["id"]}/assets?'
        self.request(url + urllib.parse.urlencode({'name': name}), 'POST', data)

    def expose(self, release):
        return self.request(self.base + f'/releases/{release["id"]}', 'PATCH',
                            {'draft': False, 'prerelease': True, 'make_latest': 'false'})

    def latest_id(self):
        latest = self.optional('/releases/latest')
        return latest['id'] if latest else None


def inventory_hash(inventory):
    return alpha.art.digest(json.dumps(inventory, sort_keys=True, separators=(',', ':')).encode())


def marker(tag, sha, run_id, inventory):
    alpha.version(tag)
    alpha.art.require(re.fullmatch(r'[0-9a-f]{40}', sha) and re.fullmatch(r'[1-9][0-9]*', run_id), 'invalid release identity')
    return f'<!-- meshbus-alpha-ci:{tag}:{sha}:{run_id}:{inventory_hash(inventory)} -->'


def notes(tag, sha, run_id, inventory):
    return (marker(tag, sha, run_id, inventory) + '\n\n'
            f'Mesh Probe R1 Alpha {alpha.version(tag)}. CI build, package integrity and EDK compiler checks passed.\n\n'
            'APP-only UF2: requires an existing compatible UF2 bootloader and SoftDevice. '
            'Use the firmware archive for flash-map.json, applicable licenses, notices and SBOM. '
            'No device, RF, recovery, memory/soak or MBA runtime qualification was performed. '
            'UF2 is structurally verified and has no cryptographic image authentication. '
            'This Alpha is not production-qualified. CLI packages have an independent version and are not included.\n\n'
            f'Source: `{sha}`. [CI evidence](https://github.com/{alpha.REPOSITORY}/actions/runs/{run_id}). '
            'Preserve storage and existing boot components; this release does not authorize erase-all. '
            'Use a new Alpha number for content corrections.\n')


def owned(release, tag, sha):
    pattern = rf'<!-- meshbus-alpha-ci:{re.escape(tag)}:{sha}:([1-9][0-9]*):([0-9a-f]{{64}}) -->'
    match = re.match(pattern, release.get('body') or '')
    alpha.art.require(release['tag_name'] == tag and release['target_commitish'] == sha and
                      release['prerelease'] is True and match and
                      release.get('author', {}).get('login') == 'github-actions[bot]', 'release ownership/source conflict')
    return match.group(1), match.group(2)


def verify_remote(github, release, expected, anonymous=False, partial=False):
    assets = github.assets(release)
    wanted = {p['name']: p for p in expected}
    names = [a['name'] for a in assets]
    alpha.art.require(len(set(names)) == len(names) and set(names) <= set(wanted) and
                      (partial or set(names) == set(wanted)), 'missing/unexpected release asset')
    for asset in assets:
        alpha.art.require(asset['state'] == 'uploaded' and asset['size'] == wanted[asset['name']]['size'],
                          'release asset size/state conflict')
        alpha.art.require(alpha.entry(asset['name'], github.download(asset, anonymous)) == wanted[asset['name']],
                          'release download digest conflict')
    return set(names)


def validate_staging(assets, inventory, tag, sha):
    alpha.art.verify_checksums(assets)
    alpha.art.require(alpha.inventory(assets) == inventory, 'retained staging inventory conflict')
    manifest = json.loads(alpha.art.read(assets / 'release-manifest.json'))
    validate_manifest(manifest, tag, sha)
    alpha.art.require(manifest['assets'] == [p for p in inventory if p['name'] not in ('release-manifest.json', 'SHA256SUMS')],
                      'publication manifest payload inventory conflict')
    return manifest


def validate_manifest(manifest, tag, sha):
    alpha.art.require(manifest['version'] == alpha.version(tag) and manifest['target'] == alpha.TARGET,
                      'public firmware version/target conflict')
    alpha.art.require({p['name'] for p in manifest['assets']} == alpha.payload_names(tag) and
                      len(manifest['assets']) == 4, 'public payload allowlist conflict')
    alpha.portable(manifest)
    alpha.art.require(manifest['schema'] == 1 and manifest['tag'] == tag and manifest['source_revision'] == sha and
                      manifest['repository'] == alpha.REPOSITORY and
                      manifest['qualification']['alpha_eligible'] is True and
                      manifest['qualification']['production_qualified'] is False, 'public release manifest conflict')


def preflight(github, tag, sha):
    alpha.version(tag)
    alpha.art.require(github.tag_sha(tag) == sha, 'remote tag source conflict')
    release = github.find(tag)
    if not release:
        return False
    original_run, expected_hash = owned(release, tag, sha)
    if release['draft']:
        # Full reruns create different archives; rerun the original failed jobs.
        raise ValueError('owned draft exists: rerun only the original failed publication job; do not rebuild')
    assets = github.assets(release)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for asset in assets:
            alpha.art.relative(asset['name'])
            alpha.art.require('/' not in asset['name'] and not (root / asset['name']).exists(), 'unsafe/duplicate public asset')
            (root / asset['name']).write_bytes(github.download(asset, anonymous=True))
        manifest = json.loads(alpha.art.read(root / 'release-manifest.json'))
        validate_manifest(manifest, tag, sha)
        expected = alpha.inventory(root)
        validate_staging(root, expected, tag, sha)
        alpha.art.require(manifest['run_id'] == original_run and inventory_hash(expected) == expected_hash,
                          'published inventory ownership conflict')
        verify_remote(github, release, expected, anonymous=True)
    alpha.art.require(github.latest_id() != release['id'], 'Alpha must not be latest')
    summary('Already public; original assets verified without rebuilding.', release)
    return True


def publish(github, assets, inventory, tag, sha, run_id):
    manifest = validate_staging(assets, inventory, tag, sha)
    alpha.art.require(manifest['run_id'] == run_id and github.tag_sha(tag) == sha, 'retained source/run conflict')
    release = github.find(tag)
    if release and not release['draft']:
        preflight(github, tag, sha)
        return release
    if release:
        alpha.art.require((release.get('body') or '').startswith(marker(tag, sha, run_id, inventory)), 'conflicting retained draft inventory')
        owned(release, tag, sha)
    else:
        release = github.create(tag, sha, notes(tag, sha, run_id, inventory))
        owned(release, tag, sha)
    summary('Draft retained until all uploads and authenticated downloads pass.', release)
    present = verify_remote(github, release, inventory, partial=True)
    for item in inventory:
        if item['name'] not in present:
            github.upload(release, item['name'], alpha.art.read(assets / item['name'], alpha.MAX_ASSET))
    verify_remote(github, release, inventory)
    alpha.art.require(github.tag_sha(tag) == sha, 'tag moved before public publication')
    try:
        release = github.expose(release)
    except Exception as error:
        try:
            current = github.find(tag)
        except Exception:
            summary('FAILED: publication response lost; visibility is unknown. Inspect the Release before recovery.')
            raise ValueError('Publication state is unknown; inspect the Release and preserve its assets.') from error
        if current and not current['draft']:
            summary('FAILED: publication response lost; Release is already public. Preserve assets.', current)
            raise ValueError('Release is already public; publication response was lost. Preserve its assets.') from error
        summary('FAILED: publication transition failed; draft retained.', current)
        raise
    # PATCH may have succeeded even if its response was interrupted. A rerun's
    # preflight detects the published state and verifies the original bytes.
    summary('Release is public; verifying anonymous downloads.', release)
    try:
        alpha.art.require(release['draft'] is False and release['prerelease'] is True, 'unexpected public release state')
        verify_remote(github, release, inventory, anonymous=True)
        alpha.art.require(github.latest_id() != release['id'], 'Alpha must not be latest')
    except Exception as error:
        summary('FAILED after publication: release is already public; preserve assets.', release)
        raise ValueError('Release is already public; anonymous verification failed. Preserve its assets.') from error
    summary('Public Prerelease and anonymous asset checksums verified.', release)
    return release


def summary(message, release=None):
    url = release.get('html_url', '') if release else ''
    line = message + (f' [Release]({url})' if url else '')
    print(line)
    if path := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(path, 'a') as stream:
            stream.write(line + '\n\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('preflight', 'publish'))
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--inventory', type=Path)
    args = parser.parse_args()
    tag, sha = os.environ['RELEASE_TAG'], os.environ['GITHUB_SHA']
    alpha.art.require(os.environ['GITHUB_REPOSITORY'] == alpha.REPOSITORY, 'publication only runs in the owning repository')
    alpha.checkout_identity(Path(__file__).resolve().parents[2], tag, sha)
    github = GitHub(os.environ['GITHUB_TOKEN'])
    if args.command == 'preflight':
        done = preflight(github, tag, sha)
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            stream.write(f'published={str(done).lower()}\n')
    else:
        publish(github, args.assets, json.loads(alpha.art.read(args.inventory)), tag, sha, os.environ['GITHUB_RUN_ID'])


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, urllib.error.URLError) as error:
        summary(f'FAILED: {error}. Check the Release state before recovery; no assets were deleted or replaced.')
        raise SystemExit(1) from None
