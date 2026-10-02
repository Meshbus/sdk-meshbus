# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Check changed Git files by default, or audit all eligible files with --full."""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[2]
METADATA = ('missing_copyright_info', 'missing_licensing_info')
REQUIRED_FIELDS = {*METADATA, 'bad_licenses', 'deprecated_licenses',
                   'licenses_without_extension', 'missing_licenses',
                   'unused_licenses', 'read_errors'}


def scoped_exemption(root, name):
    """Exclude Markdown, board documentation images and plain version metadata."""
    path = PurePosixPath(name)
    if (path.is_absolute() or '..' in path.parts or path.as_posix() != name):
        return None
    source = root.resolve() / name
    if source.resolve() != source or not source.is_file():
        return None
    if path.suffix.lower() == '.md':
        return {'category': 'markdown-documentation',
                'reason': 'Markdown documents do not require per-file SPDX metadata.'}
    if (len(path.parts) >= 5 and path.parts[0] == 'boards' and path.parts[3] == 'doc'
            and path.suffix.lower() in {'.png', '.jpg', '.jpeg', '.webp', '.gif', '.bmp', '.ico'}):
        return {'category': 'board-documentation-image',
                'reason': 'Raster images in board documentation do not require per-file metadata.'}
    version_file = (path.name == 'VERSION' and path.parts[0] in {'apps', 'samples', 'tests'})
    scalar_file = name == 'subsys/llext/METADATA_VERSION'
    if not (version_file or scalar_file):
        return None
    try:
        content = source.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        return None
    if scalar_file:
        plain_version = re.fullmatch(r'\s*[0-9]+\s*', content) is not None
    else:
        fields = {}
        for line in content.splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            match = re.fullmatch(
                r'\s*(VERSION_MAJOR|VERSION_MINOR|PATCHLEVEL|VERSION_TWEAK|EXTRAVERSION)'
                r'\s*=\s*([a-z0-9.\-]*)\s*', line)
            if not match or match[1] in fields:
                return None
            fields[match[1]] = match[2]
        numeric = {'VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL', 'VERSION_TWEAK'}
        plain_version = (fields.keys() == numeric | {'EXTRAVERSION'}
                         and all(re.fullmatch(r'[0-9]+', fields[key]) for key in numeric))
    if plain_version:
        return {'category': 'version-values',
                'reason': 'Plain version fields contain no program logic; version changes remain exempt.'}
    return None


def exemptions(root, policy):
    """Only exact reviewed bytes qualify; changed/deleted files are inactive."""
    if policy.get('version') != 1:
        raise ValueError('unsupported license policy version')
    root = root.resolve()
    active, inactive, seen = {}, {}, set()
    for group in policy['exemptions']:
        category, reason = group['category'], group['reason']
        if (not isinstance(category, str) or not category.strip()
                or not isinstance(reason, str) or not reason.strip()):
            raise ValueError('each exemption group needs a category and reason')
        for name, digest in group['files'].items():
            path = PurePosixPath(name)
            if (not name or name == '.' or path.is_absolute() or '..' in path.parts
                    or path.as_posix() != name or any(char in name for char in '*?[\\')
                    or name in seen or not isinstance(digest, str)
                    or not re.fullmatch('[0-9a-f]{64}', digest)):
                raise ValueError(f'invalid or duplicate exemption: {name}')
            seen.add(name)
            source = root / name
            if source.resolve() != source:
                raise ValueError(f'exemption must not follow symlinks: {name}')
            if not source.is_file():
                inactive[name] = 'file no longer exists'
            elif hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                inactive[name] = 'content changed; metadata exemption not applied'
            else:
                active[name] = {'category': category, 'reason': reason, 'sha256': digest}
    return active, inactive


def evaluate(report, root, policy, *, incremental=False):
    """Filter missing metadata only, retaining all other REUSE failure classes."""
    issues = report['non_compliant']
    if (report['lint_version'] != '1.0' or not isinstance(issues, dict)
            or not REQUIRED_FIELDS.issubset(issues)
            or not all(isinstance(issues[field], list) for field in REQUIRED_FIELDS)
            or type(report['summary']['compliant']) is not bool
            or report['summary']['compliant'] != (not any(issues.values()))):
        raise ValueError('incomplete or inconsistent REUSE report')
    for field in METADATA:
        if not isinstance(issues[field], list) or not all(isinstance(p, str) for p in issues[field]):
            raise ValueError(f'invalid REUSE field: {field}')
    active, inactive = exemptions(root, policy)
    for name in sorted(set().union(*(issues[field] for field in METADATA))):
        entry = scoped_exemption(root, name)
        if entry:
            active[name] = entry
    effective = dict(issues)
    distribution = policy.get('distribution_licenses', {})
    for identifier, reason in distribution.items():
        if (not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+-]*', identifier)
                or not isinstance(reason, str) or not reason.strip()):
            raise ValueError('invalid distribution license exception')
        path = root / 'LICENSES' / f'{identifier}.txt'
        if not path.is_file() or path.is_symlink() or not path.read_bytes().strip():
            raise ValueError(f'missing distribution license text: {identifier}')
    # Only the unused-text finding is filtered. Missing/bad licenses and the
    # scanner's original report remain untouched; source files get no new grant.
    effective['unused_licenses'] = sorted(set(issues['unused_licenses']) - distribution.keys())
    advisory = {}
    if incremental:
        # A subset cannot establish that a text is unused throughout the repository.
        advisory['unused_licenses'] = effective['unused_licenses']
        effective['unused_licenses'] = []
    excluded = {}
    for field in METADATA:
        effective[field] = sorted(set(issues[field]) - active.keys())
        for name in sorted(set(issues[field]) & active.keys()):
            entry = excluded.setdefault(name, {**active[name], 'waived_findings': []})
            entry['waived_findings'].append(field)
    remaining = sorted(set().union(*(effective[field] for field in METADATA)))
    return {
        'scope': 'repository policy; not full REUSE compliance',
        'raw_summary': report['summary'],
        'policy_compliant': not any(effective.values()),
        'non_compliant': effective,
        'advisory_findings': advisory,
        'exempted_files': excluded,
        'distribution_license_texts': {key: distribution[key] for key in issues['unused_licenses'] if key in distribution},
        'inactive_exemptions': inactive,
        'remaining_files': remaining,
    }


def write_reports(result, output):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'license-policy.json').write_text(json.dumps(result, indent=2) + '\n')
    missing = result['non_compliant']
    copyright_paths, licensing_paths = (set(missing[field]) for field in METADATA)
    for name, paths in [('license-remaining-copyright.txt', copyright_paths),
                        ('license-remaining-licensing.txt', licensing_paths),
                        ('license-exempted-files.txt', result['exempted_files'])]:
        (output / name).write_text(''.join(p + '\n' for p in sorted(paths)))
    lines = [
        '# License metadata remaining after repository policy', '',
        'This is the repository policy result, not a full REUSE compliance result.',
        'The original scanner output is retained unchanged in `reuse.json`.', '',
        f'- Files exempted from missing-metadata findings: {len(result["exempted_files"])}',
        f'- Remaining files missing copyright metadata: {len(copyright_paths)}',
        f'- Remaining files missing licensing metadata: {len(licensing_paths)}',
        f'- Remaining unique files: {len(result["remaining_files"])}', '',
        '## Other REUSE findings (not exempted)', '',
    ]
    if 'scan' in result:
        scope = result['scan']
        lines[6:6] = [f'- Scan mode: {scope["mode"]}',
                      f'- Selection reason: {scope["reason"]}',
                      '- Temporarily excluded tree: `web/`', '']
    for field, value in missing.items():
        if field not in METADATA and value:
            lines.append(f'- `{field}`: `{json.dumps(value)}`')
    if result['distribution_license_texts']:
        lines += ['', '## Distribution-only standard texts', '']
        lines += [f'- `{key}`: {reason}' for key, reason in result['distribution_license_texts'].items()]
    if result['advisory_findings']:
        lines += ['', '## Advisory findings for the selected subset', '']
        lines += [f'- `{key}`: `{json.dumps(value)}`'
                  for key, value in result['advisory_findings'].items() if value]
    lines += ['', '## Remaining files', '',
              '| Repository path | Missing copyright | Missing license |',
              '| --- | --- | --- |']
    for path in result['remaining_files']:
        lines.append(f'| `{path}` | {"yes" if path in copyright_paths else ""} | '
                     f'{"yes" if path in licensing_paths else ""} |')
    lines += ['', '## Applied exemptions', '',
              'Existing notices and all non-metadata findings remain in scope.', '',
              '| Repository path | Exemption category |', '| --- | --- |']
    for path, item in sorted(result['exempted_files'].items()):
        lines.append(f'| `{path}` | {item["category"]} |')
    if result['inactive_exemptions']:
        lines += ['', '## Inactive exemptions', '']
        for path, reason in sorted(result['inactive_exemptions'].items()):
            lines.append(f'- `{path}`: {reason}')
    (output / 'license-remaining-files.md').write_text('\n'.join(lines) + '\n')


def git_paths(root, *args):
    output = subprocess.check_output(['git', *args], cwd=root)
    return {os.fsdecode(path) for path in output.split(b'\0') if path}


def licensing_context(name):
    path = PurePosixPath(name)
    return (path.name == 'REUSE.toml' or name == '.reuse/dep5'
            or path.suffix == '.license' or 'LICENSES' in path.parts
            or path.name.startswith(('LICENSE', 'COPYING')))


def without_website_annotations(metadata):
    """Compare effective root attribution without provably web-only tables."""
    annotations = metadata.get('annotations', [])
    if not isinstance(annotations, list) or not all(isinstance(a, dict) for a in annotations):
        raise ValueError('invalid REUSE annotations')
    retained = []
    for annotation in annotations:
        paths = annotation.get('path')
        if isinstance(paths, str):
            paths = [paths]
        website_only = (isinstance(paths, list) and bool(paths) and all(
            isinstance(name, str) and name.startswith('web/') and '\\' not in name
            and PurePosixPath(name).as_posix() == name and '..' not in PurePosixPath(name).parts
            for name in paths))
        if not website_only:
            retained.append(annotation)
    return {**metadata, 'annotations': retained}


def root_attribution_unchanged(root, base):
    """Only unchanged effective TOML may avoid a complete root-context audit."""
    source = root / 'REUSE.toml'
    if not source.is_file() or source.is_symlink():
        return False
    previous = subprocess.run(['git', 'show', f'{base}:REUSE.toml'], cwd=root,
                              capture_output=True)
    if previous.returncode:
        return False
    try:
        old = tomllib.loads(previous.stdout.decode('utf-8'))
        current = tomllib.loads(source.read_text(encoding='utf-8'))
        return without_website_annotations(old) == without_website_annotations(current)
    except (OSError, UnicodeError, ValueError):
        return False


def select_files(root, *, base=None, full=False):
    """Git enumerates files without the directory optimization used by REUSE."""
    root = root.resolve()
    available = git_paths(root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z')
    available = {name for name in available if not name.startswith('web/')
                 and (root / name).is_file() and (root / name).resolve() == root / name}
    reference = base or 'HEAD'
    reason = 'explicit complete audit' if full else f'changes against {reference}'
    if not full:
        resolved = subprocess.run(['git', 'rev-parse', '--verify', '--quiet', '--end-of-options',
                                   f'{reference}^{{commit}}'], cwd=root, capture_output=True)
        if resolved.returncode:
            full, reason = True, f'comparison base unavailable: {reference}'
        else:
            revision = resolved.stdout.decode().strip()
            changed = git_paths(root, 'diff', '--name-only', '--no-renames', '-z',
                                revision, '--')
            changed |= git_paths(root, 'ls-files', '--others', '--exclude-standard', '-z')
            expanded = set(changed)
            shared, local_context = [], []
            for name in sorted(changed):
                if name.startswith('web/'):
                    continue
                path = PurePosixPath(name)
                if name == 'REUSE.toml':
                    if not root_attribution_unchanged(root, revision):
                        shared.append(name)
                elif 'LICENSES' in path.parts or path.name.startswith(('LICENSE', 'COPYING')):
                    shared.append(name)
                elif path.name == 'REUSE.toml':
                    prefix = path.parent.as_posix() + '/'
                    expanded.update(p for p in available if p.startswith(prefix))
                    local_context.append(f'{name} -> {prefix}')
                elif path.suffix == '.license':
                    counterpart = name.removesuffix('.license')
                    expanded.add(counterpart)
                    local_context.append(f'{name} -> {counterpart}')
                elif (licensing_context(name) or name in {
                        '.github/license-policy.toml', 'scripts/ci/license_policy.py'}):
                    shared.append(name)
            if shared:
                full, reason = True, 'shared licensing inputs changed: ' + ', '.join(shared)
            elif local_context:
                reason += '; attribution scope expanded: ' + ', '.join(local_context)
    selected = available if full else available & expanded
    context = {name for name in available if licensing_context(name)}
    return {
        'mode': 'full' if full else 'incremental', 'base': reference if not full else None,
        'reason': reason, 'files': sorted(selected),
        'context_files': sorted(context - selected), 'excluded_trees': ['web/'],
    }


def reuse_report(root, output):
    """Retain the unmodified scanner report and fail on invalid scanner output."""
    with (output / 'reuse.json').open('w') as stdout, (output / 'reuse.stderr').open('w') as stderr:
        process = subprocess.run(['reuse', '--root', '.', 'lint', '--json'],
                                 cwd=root, stdout=stdout, stderr=stderr)
    if process.returncode not in (0, 1):
        raise subprocess.CalledProcessError(process.returncode, process.args)
    report = json.loads((output / 'reuse.json').read_text())
    if (process.returncode == 0) != report['summary']['compliant']:
        raise ValueError('REUSE exit status disagrees with its report')
    return report


def scan(root, output, *, base=None, full=False):
    root, output = root.resolve(), output.resolve()
    scope = select_files(root, base=base, full=full)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'license-policy.json').unlink(missing_ok=True)
    (output / 'license-scan.json').write_text(json.dumps(scope, indent=2) + '\n')
    with tempfile.TemporaryDirectory(prefix='meshbus-license-') as temporary:
        snapshot = Path(temporary)
        for name in scope['files'] + scope['context_files']:
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / name, target)
        report = reuse_report(snapshot, output)
    with (root / '.github/license-policy.toml').open('rb') as stream:
        policy = tomllib.load(stream)
    result = evaluate(report, root, policy, incremental=scope['mode'] == 'incremental')
    result['scan'] = scope
    write_reports(result, output)
    counts = Counter(item['category'] for item in result['exempted_files'].values())
    print(f'License scan: {scope["mode"]}; {len(scope["files"])} selected files; '
          f'web/ excluded; {scope["reason"]}')
    print(f'Metadata exemptions: {len(result["exempted_files"])} ({dict(counts)})')
    print(f'Remaining metadata findings: {len(result["remaining_files"])} files; '
          f'repository license policy passed: {result["policy_compliant"]}')
    return result['policy_compliant']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--base', help='Compare current files with this commit (default: HEAD)')
    selection.add_argument('--full', action='store_true', help='Audit all eligible Git files except web/')
    args = parser.parse_args()
    raise SystemExit(0 if scan(ROOT, args.output, base=args.base, full=args.full) else 1)
