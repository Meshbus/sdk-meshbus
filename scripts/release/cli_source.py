# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Bind a minimal CLI checkout to provenance captured by full workspace prepare."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

import yaml


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def record(snapshot, provenance):
    source = json.loads((snapshot / 'source.json').read_text())
    receipt = dict(schema=1, **source, frozen_manifest_sha256=digest(snapshot / 'west-frozen.yml'),
                   provenance=provenance)
    (snapshot / 'cli-source.json').write_text(json.dumps(receipt, indent=2) + '\n')


def read(snapshot, source):
    receipt = json.loads((snapshot / 'cli-source.json').read_text())
    identity = json.loads((snapshot / 'source.json').read_text())
    if (receipt['schema'] != 1 or identity != dict(source_revision=receipt['source_revision'],
                                                 manifest_sha256=receipt['manifest_sha256'])
            or receipt['source_revision'] != git(source, 'rev-parse', 'HEAD')
            or receipt['manifest_sha256'] != digest(source / 'west.yml')
            or receipt['frozen_manifest_sha256'] != digest(snapshot / 'west-frozen.yml')):
        raise ValueError('CLI snapshot source or graph conflict')
    graph = yaml.safe_load((snapshot / 'west-frozen.yml').read_text())['manifest']['projects']
    provenance = receipt['provenance']
    expected = {p['name']: p['revision'] for p in graph} | {'meshbus': receipt['source_revision']}
    actual = {name: p['revision'] for name, p in provenance['projects'].items()}
    if (actual != expected or provenance['firmware']['revision'] != receipt['source_revision']
            or provenance['off_manifest'] or provenance['firmware']['dirty']
            or any(p['dirty'] for p in provenance['projects'].values())):
        raise ValueError('CLI snapshot provenance differs from frozen graph')
    matches = [p for p in graph if p['name'] == 'meshbus-protobufs']
    if len(matches) != 1:
        raise ValueError('CLI snapshot must identify one schema project')
    project = matches[0]
    if (not re.fullmatch(r'[0-9a-f]{40}', project['revision'])
            or not project['url'].startswith('https://') or project.get('submodules')):
        raise ValueError('CLI snapshot schema must use pinned HTTPS inputs without submodules')
    return receipt, project


def destination(workspace, source, project):
    relative = Path(project['path'])
    path = (workspace / relative).resolve()
    if (relative.is_absolute() or '..' in relative.parts or not path.is_relative_to(workspace.resolve())
            or path == workspace.resolve() or path.is_relative_to(source.resolve())
            or source.resolve().is_relative_to(path)):
        raise ValueError('unsafe CLI schema path')
    return path


def verify(snapshot, source, workspace):
    receipt, project = read(snapshot, source)
    proto = destination(workspace, source, project)
    if (git(proto, 'rev-parse', 'HEAD') != project['revision']
            or git(proto, 'status', '--porcelain', '--untracked-files=normal')
            or git(proto, 'remote', 'get-url', 'origin') != project['url']):
        raise ValueError('CLI schema checkout differs from snapshot')
    provenance = receipt['provenance']
    # Keep current dirty state visible to the existing candidate gate.
    provenance['firmware']['dirty'] = bool(git(source, 'status', '--porcelain', '--untracked-files=normal'))
    provenance['projects']['meshbus'] = dict(provenance['firmware'])
    return provenance, proto
