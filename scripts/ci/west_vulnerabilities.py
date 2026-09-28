# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Report west dependency CPE matches without treating them as CI blockers."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

import yaml
from west.manifest import Manifest

import workspace as source_workspace


def inventory(workspace):
    manifest = Manifest.from_topdir(str(workspace))
    components, coverage = [], []
    for project in manifest.projects[1:]:
        if not manifest.is_active(project):
            continue
        metadata = Path(project.abspath) / 'zephyr/module.yml'
        record = yaml.safe_load(metadata.read_text()) if metadata.exists() else {}
        references = (record or {}).get('security', {}).get('external-references', [])
        cpes = []
        for reference in references:
            parts = re.split(r'(?<!\\):', reference)
            if reference.startswith('cpe:2.3:') and len(parts) == 13 and parts[5] not in ('*', '-', ''):
                cpes.append((reference, parts[5]))
        coverage.append({'name': project.name, 'revision': project.sha('HEAD'),
                         'references': references,
                         'assessment': 'CPE-advisory-match' if cpes else 'manual-review-required'})
        for index, (cpe, version) in enumerate(cpes):
            components.append({'type': 'library', 'bom-ref': f'{project.name}:{index}',
                               'name': project.name, 'version': version, 'cpe': cpe,
                               'properties': [{'name': 'meshbus:source-sha', 'value': project.sha('HEAD')}]})
    return {'bomFormat': 'CycloneDX', 'specVersion': '1.6', 'version': 1,
            'components': components}, coverage


def findings(report):
    return {(match['artifact']['name'], match['artifact']['version'], match['vulnerability']['id']):
            match['vulnerability']['severity'].upper() for match in report.get('matches', [])}


def compare(current, base):
    new, old = findings(current), findings(base)
    return {'policy': 'report-only',
            'introduced_high_or_critical': [list(key) for key, severity in new.items()
                                           if key not in old and severity in ('HIGH', 'CRITICAL')],
            'total': len(new), 'preexisting': sum(key in old for key in new),
            'applicability': 'CPE metadata matches require source/build applicability review'}


def scan(workspace, output, name):
    sbom, coverage = inventory(workspace)
    path = output / f'west-{name}.cdx.json'
    path.write_text(json.dumps(sbom, indent=2) + '\n')
    (output / f'west-{name}-coverage.json').write_text(json.dumps(coverage, indent=2) + '\n')
    report = output / f'west-{name}-vulnerabilities.json'
    with report.open('w') as stream:
        subprocess.run(['grype', f'sbom:{path}', '--by-cve', '-o', 'json'], stdout=stream, check=True)
    return json.loads(report.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--base', required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(['grype', 'db', 'update'], check=True)
    with (output / 'grype-db.json').open('w') as stream:
        subprocess.run(['grype', 'db', 'status', '-o', 'json'], stdout=stream, check=True)
    # Both scans must see exactly this DB, including scheduled runs.
    import os
    os.environ['GRYPE_DB_AUTO_UPDATE'] = 'false'
    os.environ['GRYPE_MATCH_STOCK_USING_CPES'] = 'true'
    current = scan(workspace, output, 'current')
    base = {'matches': []}
    if args.base != '0' * 40:
        with tempfile.TemporaryDirectory(prefix='meshbus-vulnerability-base-') as temporary:
            base_workspace = Path(temporary)
            base_source = base_workspace / 'meshbus'
            subprocess.run(['git', 'worktree', 'add', '--detach', str(base_source), args.base],
                           cwd=workspace / 'meshbus', check=True)
            try:
                source_workspace.setup(base_workspace, output / 'west-base-snapshot')
                base = scan(base_workspace, output, 'base')
            finally:
                subprocess.run(['git', 'worktree', 'remove', str(base_source)],
                               cwd=workspace / 'meshbus', check=True)
    summary = compare(current, base)
    (output / 'west-vulnerabilities-delta.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    # CPE matches describe upstream source versions, not confirmed product
    # exposure. Report every finding; scanner/collection errors still propagate.


if __name__ == '__main__':
    main()
